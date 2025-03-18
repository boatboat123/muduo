#pragma once

#include<functional>

#include"noncopyable.h"
#include"Socket.h"
#include"Channel.h"

class EventLoop;
class InetAddress;

class Acceptor:noncopyable
{
    public:
    using NewConnectionCallback=std::function<void(int socketfd,const InetAddress &)>;
    Acceptor(EventLoop *loop,const InetAddress &listenAddr,bool reuseport);
    ~Acceptor();
    //设置新连接的回调函数
    void setNewConnectionCallback(const NewConnectionCallback &cb){
        NewConnectionCallback_=cb;
    }
    //判断是否在监听
    bool listenning() const{
        return listenning_;
    }
    //监听本地端口
    void listen();

    private:
    void handleRead();
    EventLoop *loop_;
    Socket acceptSocket_;
    Channel acceptChannel_;
    NewConnectionCallback NewConnectionCallback_;
    bool listenning_;
};
