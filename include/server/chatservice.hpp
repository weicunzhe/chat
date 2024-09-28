#ifndef CHATSERVICE_H
#define CHATSERVICE_H

#include <unordered_map>
#include <functional>
#include <muduo/net/TcpConnection.h>
#include <mutex>

#include "usermodel.hpp"
#include "offlinemessagemodel.hpp"
#include "friendmodel.hpp"
#include "groupmodel.hpp"

#include "json.hpp"
using json = nlohmann::json;

// 处理消息事件回调方法类型
using MsgHandler = std::function<void(
    const muduo::net::TcpConnectionPtr &conn,
    json &js, muduo::Timestamp time)>;

// 聊天服务器业务类
class ChatService
{
public:
    // 获取单例对象的接口函数
    static ChatService *instance();
    // 处理登录业务
    void login(const muduo::net::TcpConnectionPtr &conn,
               json &js, muduo::Timestamp time);
    // 处理注册业务
    void reg(const muduo::net::TcpConnectionPtr &conn,
             json &js, muduo::Timestamp time);
    // 一对一聊天业务
    void oneChat(const muduo::net::TcpConnectionPtr &conn,
                 json &js, muduo::Timestamp time);
    // 添加好友业务
    void addFriend(const muduo::net::TcpConnectionPtr &conn,
                   json &js, muduo::Timestamp time);

    // 创建群组业务
    void createGroup(const muduo::net::TcpConnectionPtr &conn,
                     json &js, muduo::Timestamp time);
    // 加入群组业务
    void addGroup(const muduo::net::TcpConnectionPtr &conn,
                  json &js, muduo::Timestamp time);
    // 群组聊天业务
    void groupChat(const muduo::net::TcpConnectionPtr &conn,
                   json &js, muduo::Timestamp time);

    // 处理客户端异常退出
    void clientCloseException(const muduo::net::TcpConnectionPtr &conn);
    // 服务器异常，业务重置方法
    void reset();
    // 获取消息对应的处理器
    MsgHandler getHandler(int msgid);

private:
    ChatService();
    ~ChatService() {};
    ChatService(const ChatService &) = delete;
    ChatService &operator=(const ChatService) = delete;
    ChatService(ChatService &&) = delete;
    ChatService &operator=(ChatService &&) = delete;
    // 存储消息id和其对应的业务处理方法
    std::unordered_map<int, MsgHandler> _msgHandlerMap;

    // 存储在线用户的通信连接
    std::unordered_map<int, muduo::net::TcpConnectionPtr> _userConnMap;

    // 定义互斥锁，保证_userConnMap的线程安全
    std::mutex _connMutex;

    // 数据操作类对象
    UserModel _userModel;
    OffLineMessageModel _offLineMsgModel;
    FriendModel _friendModel;
    GroupModel _groupModel;
};

#endif