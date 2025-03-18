#pragma once

#include <functional>
#include <vector>
#include <atomic>
#include <memory>
#include <mutex>
#include "noncopyable.h"
#include "Timestamp.h"
#include "CurrentThread.h"
//Polle负责封装和事件监听有关的成员和方法调用poller：：poll可以返回监听结果
//EventLoop负责循环调用poll 然后调用channel保管的不同处理函数
class Channel;
class Poller;

class EventLoop:noncopyable{
    public:
    using Functor=std::function<void()>;
    EventLoop();
    ~EventLoop();

    //开启事件循环
    void loop();

    //退出事件循环
    void quit();

    Timestamp pollReturnTime(){
        return pollReturnTime_;
    }

    //在当前loop中执行
    void runInLoop(Functor cb);

    //把上层注册的回调函数cb放入队列中 唤醒loop所在的线程执行cb
    void queueInLoop(Functor cb);

    void wakeup();
    //eventloop的方法 =>Poller的方法
    void updateChannel(Channel *channel);
    void removeChannel(Channel *channel);
    bool hasChannel(Channel *channel);
    bool isInLoopThread(){
        return threadId_==CurrentThread::tid();
    }

    private:
    //给eventfd返回的文件描述符wakeupFd_绑定的事件回调 当wakeup()时 即有事件发生时 调用handleRead()读wakeupFd_的8字节 同时唤醒阻塞的epoll_wait
    void handleRead();

    void doPendingFunctors();
    using ChannelList = std::vector<Channel *>;
    std::atomic_bool looping_; // 原子操作 底层通过CAS实现
    std::atomic_bool quit_;    // 标识退出loop循环

    Timestamp pollReturnTime_;
    const pid_t threadId_;
    std::unique_ptr<Poller> poller_;

    // 作用：当mainLoop获取一个新用户的Channel 需通过轮询算法选择一个subLoop 通过该成员唤醒subLoop处理Channel
    int wakeupFd_; 
    std::unique_ptr<Channel> wakeupChannel_;
    // 返回Poller检测到当前有事件发生的所有Channel列表
    ChannelList activeChannels_; 
    std::atomic_bool callingPendingFunctors_; // 标识当前loop是否有需要执行的回调操作
    std::vector<Functor> pendingFunctors_;    // 存储loop需要执行的所有回调操作
    std::mutex mutex_;                        // 互斥锁 用来保护上面vector容器的线程安全操作
};