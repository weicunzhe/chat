#ifndef REDIS_H
#define REDIS_H

#include <hiredis/hiredis.h>
#include <thread>
#include <functional>
/*
redis作为集群服务器通信的基于发布-订阅消息队列时，会遇到两个难搞的bug问题，参考我的博客详细描述：
https://blog.csdn.net/QIANGWEIYUAN/article/details/97895611
*/

class Redis
{
public:
    Redis(/* args */);
    ~Redis();

    // 连接服务器
    bool connect();

    // 向redis指定通道channel发布消息
    bool publish(int channel, std::string message);

    // 向redis指定的通道subscirbe订阅消息
    bool subscirbe(int channel);

    // 想redis指定的通道unsubscirbe取消订阅消息
    bool unsubscirbe(int channel);

    // 独立线程中接收订阅通道中的消息
    void observer_channel_message();

    // 初始化向业务上报通道消息的回调对象
    void init_notify_handler(std::function<void(int, std::string)> fun);

private:
    // hiredis同步上下文对象,负责publish消息
    redisContext *_publish_context;

    // hiredis同步上下文对象,负责subscribe消息
    redisContext *_subscribe_context;

    // 回调操作,收到订阅消息,给service层上报
    std::function<void(int, std::string)> _notify_message_handler;
};

#endif