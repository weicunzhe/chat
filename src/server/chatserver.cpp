#include "chatserver.hpp"
#include "json.hpp"
#include "chatservice.hpp"

#include <muduo/base/Logging.h>
using json = nlohmann::json;

// 初始化聊天服务器
ChatServer::ChatServer(muduo::net::EventLoop *loop,
                       const muduo::net::InetAddress &listenAddr,
                       const std::string &nameArg)
    : _server(loop, listenAddr, nameArg), _loop(loop)
{
    // 注册连接回调
    this->_server.setConnectionCallback(
        std::bind(&ChatServer::onConnection,
                  this, std::placeholders::_1));
    // 注册读写回调
    this->_server.setMessageCallback(
        std::bind(&ChatServer::onMessage, this,
                  std::placeholders::_1,
                  std::placeholders::_2,
                  std::placeholders::_3));

    // 设置服务器的线程数量 1个I/O线程  3个worker线程
    this->_server.setThreadNum(4);
}

// 启动服务
void ChatServer::start()
{
    this->_server.start();
}

// 上报连接相关信息的回调函数
void ChatServer::onConnection(
    const muduo::net::TcpConnectionPtr &conn)
{
    // 客户端断开连接
    if (!conn->connected())
    {
        ChatService::instance()->clientCloseException(conn);
        conn->shutdown();
    }
}

// 上报读写事件相关信息的回调函数
void ChatServer::onMessage(
    const muduo::net::TcpConnectionPtr &conn,
    muduo::net::Buffer *buffer,
    muduo::Timestamp time)
{
    std::string buf = buffer->retrieveAllAsString();
    try
    {
        // 数据的反序列化
        json js = json::parse(buf);
        // 达到的目的：完全解耦网络模块的代码和业务模块的代码
        // 通过js["msgid"] 获取 =》 业务hander =》 coon js time
        auto msgHandler = ChatService::instance()->getHandler(
            js["msgid"].get<int>());
        // 回调消息绑定好的事件处理器，来执行相应的业务处理
        msgHandler(conn, js, time);
    }
    catch (const std::exception &e)
    {
        LOG_INFO << "js error:"<<buf;
    }
}
