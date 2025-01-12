#include "chatservice.hpp"
#include "public.hpp"
#include <muduo/base/Logging.h>
#include <vector>
#include <map>

// 获取单例对象的接口函数
ChatService *ChatService::instance()
{
    static ChatService service;
    return &service;
}

// 注册消息以及对应的Handler操作
ChatService::ChatService()
{

    // 用户基本业务管理相关事件处理回调注册
    _msgHandlerMap.insert({LOGIN_MSG, std::bind(&ChatService::login, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3)});
    _msgHandlerMap.insert({LOGINOUT_MSG, std::bind(&ChatService::loginout, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3)});
    _msgHandlerMap.insert({REG_MSG, std::bind(&ChatService::reg, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3)});
    _msgHandlerMap.insert({ONE_CHAT_MSG, std::bind(&ChatService::oneChat, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3)});
    _msgHandlerMap.insert({ADD_FRIEND_MSG, std::bind(&ChatService::addFriend, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3)});

    // 群组业务管理相关事件处理回调注册
    _msgHandlerMap.insert({CREATE_GROUP_MSG, std::bind(&ChatService::createGroup, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3)});
    _msgHandlerMap.insert({ADD_GROUP_MSG, std::bind(&ChatService::addGroup, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3)});
    _msgHandlerMap.insert({GROUP_CHAT_MSG, std::bind(&ChatService::groupChat, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3)});

    // 连接redis服务器
    if (_redis.connect())
    {
        // 设置上报消息回调
        _redis.init_notify_handler(std::bind(&ChatService::handleRedisSubcribeMessage, this, std::placeholders::_1, std::placeholders::_2));
    }
}

// 服务器异常，业务重置方法
void ChatService::reset()
{
    // 把online状态的用户，设置成offline
    _userModel.resetState();
}

// 获取消息对应的处理器
MsgHandler ChatService::getHandler(int msgid)
{
    std::unordered_map<int, MsgHandler>::iterator it =
        _msgHandlerMap.find(msgid);
    if (it == _msgHandlerMap.end())
    {
        // 返回一个默认的处理器 空操作
        return [=](const muduo::net::TcpConnectionPtr &conn,
                   json &js, muduo::Timestamp time)
        {
            LOG_ERROR << "msgid:" << msgid << " can not find handler!";
        };
    }
    else
    {
        return _msgHandlerMap[msgid];
    }
}

// 处理客户端异常退出
void ChatService::clientCloseException(const muduo::net::TcpConnectionPtr &conn)
{
    User user;
    {
        std::lock_guard<std::mutex> lock(_connMutex);
        for (auto it = _userConnMap.begin(); it != _userConnMap.end(); it++)
        {
            if (it->second == conn)
            {
                // 从map表删除用户的连接信息
                user.setId(it->first);
                _userConnMap.erase(it);
                break;
            }
        }
    }

    // 用户注销，相当于就是下线，在redis中取消订阅通道
    _redis.unsubscribe(user.getId());

    // 更新用户的状态信息
    if (user.getId() != -1)
    {
        user.setState("offline");
        _userModel.updateState(user);
    }
}

// 处理注销业务
void ChatService::loginout(const muduo::net::TcpConnectionPtr &conn, json &js, muduo::Timestamp time)
{
    int userid = js["id"];
    {
        std::lock_guard<std::mutex> lock(_connMutex);
        auto it = _userConnMap.find(userid);
        if (it != _userConnMap.end())
        {
            _userConnMap.erase(it);
        }
    }

    // 用户注销，相当于就是下线，在redis中取消订阅通道
    _redis.unsubscribe(userid);

    // 更新用户的状态信息
    User user(userid);
    user.setState("offline");
    _userModel.updateState(user);
}

// 处理登录业务
void ChatService::login(const muduo::net::TcpConnectionPtr &conn,
                        json &js, muduo::Timestamp time)
{
    int id = js["id"];
    std::string pwd = js["password"];
    User user = _userModel.query(id);
    if (user.getId() == id)
    {
        if (user.getPwd() == pwd)
        {
            if (user.getState() == "online")
            {
                // 该账号已经登录，不允许重复登录
                json response;
                response["msgid"] = LOGIN_MSG_ACK;
                response["errno"] = 3;
                response["errmsg"] = "该账号已经登录，请重新输入新账号";
                conn->send(response.dump());
            }
            else
            {
                // 登录成功 记录用户连接信息
                {
                    std::lock_guard<std::mutex> lock(_connMutex);
                    _userConnMap.insert({id, conn});
                }

                // id用户登录成功以后，向redis订阅channel(id)
                _redis.subscribe(id);

                // 登录成功 更新用户状态信息  state offline=>online
                user.setState("online");
                _userModel.updateState(user);

                json response;
                response["msgid"] = LOGIN_MSG_ACK;
                response["errno"] = 0;
                response["id"] = user.getId();
                response["name"] = user.getName();
                // 查询该用户是否有离线消息
                std::vector<std::string> vec = _offLineMsgModel.query(id);
                if (!vec.empty())
                {
                    response["offLineMsg"] = vec;
                    // 读取该用户的离线消息后，把该用户的所有离线消息删除掉
                    _offLineMsgModel.remove(id);
                }
                // 查询该用户的好友信息并返回
                std::vector<User> userVec = _friendModel.query(id);
                if (!userVec.empty())
                {
                    std::vector<std::string> vec2;
                    for (User &user : userVec)
                    {
                        json js;
                        js["id"] = user.getId();
                        js["name"] = user.getName();
                        js["state"] = user.getState();
                        vec2.push_back(js.dump());
                    }

                    // response["friends"] = js.parse(userVec);
                    response["friends"] = vec2;
                }

                // 查询用户的群组信息
                std::vector<Group> groupuserVec = _groupModel.queryGroups(id);
                if (!groupuserVec.empty())
                {
                    // group:[{groupid:[xxx, xxx, xxx, xxx]}]
                    std::vector<std::string> groupV;
                    for (Group &group : groupuserVec)
                    {
                        json grpjson;
                        grpjson["id"] = group.getId();
                        grpjson["groupname"] = group.getName();
                        grpjson["groupdesc"] = group.getDesc();
                        std::vector<std::string> userV;
                        for (GroupUser &user : group.getUsers())
                        {
                            json js;
                            js["id"] = user.getId();
                            js["name"] = user.getName();
                            js["state"] = user.getState();
                            js["role"] = user.getRole();
                            userV.push_back(js.dump());
                        }
                        grpjson["users"] = userV;
                        groupV.push_back(grpjson.dump());
                    }

                    response["groups"] = groupV;
                }

                conn->send(response.dump());
            }
        }
        else
        {
            // 登录失败吗，用户名或密码错误
            json response;
            response["msgid"] = LOGIN_MSG_ACK;
            response["errno"] = 2;
            response["errmsg"] = "用户名或密码错误";
            conn->send(response.dump());
        }
    }
    else
    {
        // 用户名不存在
        json response;
        response["msgid"] = LOGIN_MSG_ACK;
        response["errno"] = 1;
        response["errmsg"] = "用户名不存在";
        conn->send(response.dump());
    }

    LOG_INFO << "do login service!!!";
}

// 处理注册业务  name password
void ChatService::reg(const muduo::net::TcpConnectionPtr &conn,
                      json &js, muduo::Timestamp time)
{
    std::string name = js["name"];
    std::string pwd = js["password"];
    User user;
    user.setName(name);
    user.setPwd(pwd);
    bool state = _userModel.insert(user);
    if (state)
    {
        // 注册成功
        json response;
        response["msgid"] = REG_MSG_ACK;
        response["errno"] = 0;
        response["id"] = user.getId();
        conn->send(response.dump());
    }
    else
    {
        // 注册失败
        json response;
        response["msgid"] = REG_MSG_ACK;
        response["errno"] = 1;
        conn->send(response.dump());
    }

    // LOG_INFO << "do reg service!!!";
}

// 一对一聊天业务
void ChatService::oneChat(const muduo::net::TcpConnectionPtr &conn, json &js, muduo::Timestamp time)
{
    int toid = js["to"];
    bool userState = false;
    {
        std::lock_guard<std::mutex> lock(_connMutex);
        auto it = _userConnMap.find(toid);
        if (it != _userConnMap.end())
        {
            // toid在线，转发消息 服务器主动推送消息给toid用户
            it->second->send(js.dump());
            return;
        }
    }

    // 查询toid是否在线
    User user = _userModel.query(toid);
    if (user.getState() == "online")
    {
        _redis.publish(toid, js.dump());
        return;
    }

    // toid 不在线，存储离线消息
    _offLineMsgModel.insert(toid, js.dump());
}

// 添加好友业务 msgid id friendid
void ChatService::addFriend(const muduo::net::TcpConnectionPtr &conn, json &js, muduo::Timestamp time)
{
    int userid = js["id"];
    int friendid = js["friendid"];

    // 存储好友信息
    _friendModel.insert(userid, friendid);
}

// 创建群组业务

void ChatService::createGroup(const muduo::net::TcpConnectionPtr &conn, json &js, muduo::Timestamp time)
{
    int userid = js["id"];
    std::string name = js["groupname"];
    std::string desc = js["groupdesc"];

    // 存储新创建的群组信息
    Group group(-1, name, desc);
    if (_groupModel.createGroup(group))
    {
        // 存储群组创建人信息
        _groupModel.addGroup(userid, group.getId(), "creator");
    }
}

// 加入群组业务
void ChatService::addGroup(const muduo::net::TcpConnectionPtr &conn, json &js, muduo::Timestamp time)
{
    int userid = js["id"];
    int groupid = js["groupid"];
    _groupModel.addGroup(userid, groupid, "normal");
}

// 群组聊天业务
void ChatService::groupChat(const muduo::net::TcpConnectionPtr &conn, json &js, muduo::Timestamp time)
{
    int userid = js["id"];
    int groupid = js["groupid"];
    std::vector<int> useridVec = _groupModel.queryGroupUsers(userid, groupid);
    std::lock_guard<std::mutex> lock(_connMutex);
    for (int id : useridVec)
    {
        auto it = _userConnMap.find(id);
        if (it != _userConnMap.end())
        {
            // 转发群消息
            it->second->send(js.dump());
        }
        else
        {
            // 查询toid是否在线
            User user = _userModel.query(id);
            if (user.getState() == "online")
            {
                _redis.publish(id, js.dump());
            }
            else
            {
                // 存储离线群消息
                _offLineMsgModel.insert(id, js.dump());
            }
        }
    }
}

void ChatService::handleRedisSubcribeMessage(int userid, std::string msg)
{
    std::lock_guard<std::mutex> lock(_connMutex);
    auto it = _userConnMap.find(userid);
    if (it != _userConnMap.end())
    {
        it->second->send(msg);
        return;
    }

    // 存储该用户的离线消息
    _offLineMsgModel.insert(userid, msg);
}
