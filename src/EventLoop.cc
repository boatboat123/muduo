#include <sys/eventfd.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <memory>

#include "EventLoop.h"
#include "Logger.h"
#include "Channel.h"
#include "Poller.h"
__thread EventLoop* t_loopInThisThread=nullptr;
const int kPollTimeMs = 10000;

int creatEventfd(){
    int evtfd=eventfd(0,EFD_NONBLOCK|EFD_CLOEXEC);
    if (evtfd<0)
    {
        LOG_FATAL("eventfd errror:%d\n",errno);
    }
    return evtfd;
}

EventLoop::EventLoop():
looping_(false)
,quit_(false)
,callingPendingFunctors_(false)
,threadId_(CurrentThread::tid())
,poller_(Poller::newDefaultPoller(this))
,wakeupFd_(creatEventfd())
,wakeupChannel_(new Channel(this,wakeupFd_))
{
    LOG_DEBUG("EventLoop created %p in thread %d\n", this, threadId_);
    if (t_loopInThisThread)
    {
        LOG_FATAL("Another EventLoop %p exists in this thread %d\n", t_loopInThisThread, threadId_);
    }
    else
    {
        t_loopInThisThread = this;
    }
    wakeupChannel_->setReadCallback(
        std::bind(&EventLoop::handleRead,this)
    );
    wakeupChannel_->enableReading();
}

EventLoop::~EventLoop(){
    wakeupChannel_->disableAll();// 给Channel移除所有感兴趣的事件
    wakeupChannel_->remove();// 把Channel从EventLoop上删除掉
    ::close(wakeupFd_);
    t_loopInThisThread=nullptr;
}

//开启事件循环
void EventLoop::loop(){
    looping_=true;
    quit_=false;
    LOG_INFO("EventLoop %p start looping\n", this);
    while (!quit)
    {
        activeChannels_.clear();
        pollReturnTime_=poller_->poll(kPollTimeMs,&activeChannels_);
        for (Channel *channel :activeChannels_)
        {   
            channel->handleEvent(pollReturnTime_);
        }
        doPendingFunctors();
    } 
    LOG_INFO("EventLoop %p stop looping.\n", this);
    looping_ = false;
}

void EventLoop::quit(){
    quit_=true;
    if (!isInLoopThread)
    {
        wakeup();
    }
}

void EventLoop::runInLoop(Functor cb){
    if (isInLoopThread)
    {
        cb();
    }
    else{
        queueInLoop(cb);
    }
}

void EventLoop::queueInLoop(Functor cb){
    std::unique_lock<std::mutex> lock(mutex_);
    pendingFunctors_.emplace_back(cb);
    if (!isInLoopThread()||callingPendingFunctors_)
    {
        wakeup();
    }
}

void EventLoop::handleRead(){
    uint64_t one=1;
    ssize_t n = read(wakeupFd_, &one, sizeof(one));
    if (n != sizeof(one))
    {
        LOG_ERROR("EventLoop::handleRead() reads %lu bytes instead of 8\n", n);
    }
}

//用来唤醒loop所在线程 向wakeupfd写一个数据 wakeupchannel就会发生读事件
//当前loop线程就会被唤醒
void EventLoop::wakeup()
{
    uint64_t one = 1;
    ssize_t n = write(wakeupFd_, &one, sizeof(one));
    if (n != sizeof(one))
    {
        LOG_ERROR("EventLoop::wakeup() writes %lu bytes instead of 8\n", n);
    }
}

void EventLoop::updateChannel(Channel *channel){
    poller_->updateChannel(channel);
}

void EventLoop::removeChannel(Channel *channel){
    poller_->removeChannel(channel);
}

void EventLoop::doPendingFunctors(){
    std::vector<Functor> functors;
    callingPendingFunctors_=true;
    {
        std::unique_lock<std::mutex> lock(mutex_);
        functors.swap(pendingFunctors_); // 交换的方式减少了锁的临界区范围 提升效率 同时避免了死锁 如果执行functor()在临界区内 且functor()中调用queueInLoop()就会产生死锁
    }
    for (Functor &functor:functors)
    {
        functor();
    }
    callingPendingFunctors_=false;
}

