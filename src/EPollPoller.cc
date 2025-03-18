#include <errno.h>
#include <unistd.h>
#include <string>
#include <cstring>

#include "EPollPoller.h"
#include "Logger.h"
#include "Channel.h"

const int kNew=-1;
const int kAdded=1;
const int kDeleted=2;

EPollPoller::EPollPoller(EventLoop *loop):
Poller(loop)
, epollfd_(::epoll_create1(EPOLL_CLOEXEC)) 
, events_(kInitEventListSize) // vector<epoll_event>(16)
{
    if (epollfd_ < 0)
    {
        LOG_FATAL("epoll_create error:%d \n", errno);
    }
}

EPollPoller::~EPollPoller(){
    ::close(epollfd_);
}

Timestamp EPollPoller::poll(int timeoutMs, ChannelList *activeChannels){
    LOG_INFO("func=%s => fd total count:%lu\n", __FUNCTION__, channels_.size());
    int numEvents=epoll_wait(epollfd_,&*events_.begin(),static_cast<int>(events_.size()),timeoutMs);
    int saveErrno=errno;
    Timestamp now(Timestamp::now());
    if (numEvents>0)
    {
        LOG_INFO("%d events happend\n", numEvents); // LOG_DEBUG最合理
        fillActiveChannels(numEvents,activeChannels);
        if (numEvents==events_.size())
        {
            events_.resize(events_.size()*2);
        }
    }
    else if (numEvents == 0)
    {
        LOG_DEBUG("%s timeout!\n", __FUNCTION__);
    }
    else
    {
        if (saveErrno != EINTR)
        {
            errno = saveErrno;
            LOG_ERROR("EPollPoller::poll() error!");
        }
    }
    return now;
}

// channel update remove => EventLoop updateChannel removeChannel => Poller updateChannel removeChannel
void EPollPoller::updateChannel(Channel *channel){
    const int index=channel->index();
    LOG_INFO("func=%s =>fd=%d events=%d index=%d\n",__FUNCTION__,channel->fd(),channel->events(),index);
    if (index==kNew||index==kDeleted)
    {
        if (index==kNew)
        {
            int fd=channel->fd();
            channels_[fd]=channel;
        }
        else{
        }
        channel->set_index(kAdded);
        update(EPOLL_CTL_ADD,channel);
    }
    else // channel已经在Poller中注册过了
    {
        int fd = channel->fd();
        if (channel->isNoneEvent())
        {
            update(EPOLL_CTL_DEL, channel);
            channel->set_index(kDeleted);
        }
        else
        {
            update(EPOLL_CTL_MOD, channel);
        }
    }
}

void EPollPoller::removeChannel(Channel *channel){
    int fd=channel->fd();
    channels_.erase(fd);
    LOG_INFO("func=%s => fd=%d\n", __FUNCTION__, fd);

    int index=channel->index();
    if (index==kAdded)
    {
        update(EPOLL_CTL_DEL,channel);
    }
    channel->set_index(kNew);
}

void EPollPoller::fillActiveChannels(int numEvents,ChannelList *channels_)const{
    for (int i = 0; i < numEvents; i++)
    {
        Channel *channel=static_cast<Channel*>(events_[i].data.ptr);
        channel->set_revents(events_[i].events);
        channels_->push_back(channel);// EventLoop就拿到了它的Poller给它返回的所有发生事件的channel列表了
    }
}

//更新channel 其实就是调用epoll_ctl add/mod/del
void EPollPoller::update(int operation,Channel *channel){
    epoll_event event;
    ::memset(&event,0,sizeof(event));

    int fd=channel->fd();
    event.events=channel->events();
    event.data.ptr=channel;
    event.data.fd=fd;

    if (::epoll_ctl(epollfd_, operation, fd, &event) < 0)
    {
        if (operation==EPOLL_CTL_DEL)
        {
            LOG_ERROR("epoll_ctl del error:%d\n",errno);
        }
        else{
            LOG_FATAL("epoll_ctl add/mod error:%d\n",errno);
        }
    }
}