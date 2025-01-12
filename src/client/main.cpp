#include <iostream>

#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <thread>
#include <semaphore.h>
#include <atomic>

#include "json.hpp"

#include "user.hpp"
#include "group.hpp"
#include "public.hpp"

// 记录当前系统登录的用户信息
User g_currentUser;
// 记录当前登录用户的好友列表信息
std::vector<User> g_currentFriendList;
// 记录当前登录用户的群组列表信息
std::vector<Group> g_currentGroupList;

// 控制主菜单页面程序
bool isMainMenuRunning = false;

// 用于读写线程之间的通信
sem_t rwsem;
// 记录登录状态
std::atomic_bool g_isLoginSuccess{false};

// 接收线程
void readTaskHandler(int clientfd);
// 获取系统时间（聊天信息需要添加时间信息）
std::string getCurrentTime();
// 主聊天页面程序
void mainMenu(int clientfd);
// 显示当前登录成功用户的基本信息
void showCurrentUserData();

using json = nlohmann::json;

// 聊天客户端程序实现， main线程用作发送线程，子线程用作接收线程
int main(int argc, char **argv)
{
    if (argc < 3)
    {
        std::cerr << "command invalid! example: ./ChatClient 127.0.0.1 6000" << std::endl;
        exit(-1);
    }

    // 解析通过命令行参数传递的ip和port端口
    char *ip = argv[1];
    uint16_t port = atoi(argv[2]);

    // 创建client端的socket
    int clientfd = socket(AF_INET, SOCK_STREAM, 0);
    if (-1 == clientfd)
    {
        std::cerr << "socket create error" << std::endl;
        exit(-1);
    }

    sockaddr_in server;
    server.sin_family = AF_INET;
    server.sin_port = htons(port);
    server.sin_addr.s_addr = inet_addr(ip);

    // client和server进行连接
    if (-1 == connect(clientfd, (const sockaddr *)&server, sizeof(sockaddr_in)))
    {
        std::cerr << "connect server error" << std::endl;
        close(clientfd);
        exit(-1);
    }

    // 初始化读写线程通信用的信号量
    sem_init(&rwsem, 0, 0);

    // 连接服务器成功，启动接收子线程
    std::thread readTask(readTaskHandler, clientfd); // pthread_create
    readTask.detach();                               // pthread_detach

    // main线程用户接收用户输入， 负责发送数据
    for (;;)
    {
        // 显示首页菜单 登录 注册 退出
        std::cout << "======================" << std::endl;
        std::cout << "1. login" << std::endl;
        std::cout << "2. register" << std::endl;
        std::cout << "3. quit" << std::endl;
        std::cout << "======================" << std::endl;
        std::cout << "choice:";
        int choice = 0;
        std::cin >> choice;
        std::cin.get(); // 读掉缓冲区残留的回车

        switch (choice)
        {
        case 1: // login业务
        {
            int id = 0;
            char pwd[50] = {0};
            std::cout << "userid:";
            std::cin >> id;
            std::cin.get(); // 读掉缓冲区残留的回车

            std::cout << "password:";
            std::cin.getline(pwd, 50);

            json js;
            js["msgid"] = LOGIN_MSG;
            js["id"] = id;
            js["password"] = pwd;
            std::string request = js.dump();

            g_isLoginSuccess = false;

            int len = send(clientfd, request.c_str(), strlen(request.c_str()) + 1, 0);
            if (-1 == len)
            {
                std::cout << "send login msg error:" << request << std::endl;
            }

            sem_wait(&rwsem); // 等待信号量，由子线程处理完登录的响应消息后，通知这里

            if (g_isLoginSuccess)
            {
                // 进入聊天主菜单界面
                isMainMenuRunning = true;
                mainMenu(clientfd);
            }
        }
        break;
        case 2: // register业务
        {
            char name[50] = {0};
            char pwd[50] = {0};
            std::cout << "username:";
            std::cin.getline(name, 50);
            std::cout << "password:";
            std::cin.getline(pwd, 50);

            json js;
            js["msgid"] = REG_MSG;
            js["name"] = name;
            js["password"] = pwd;
            std::string request = js.dump();
            int len = send(clientfd, request.c_str(), strlen(request.c_str()) + 1, 0);
            if (len == -1)
            {
                std::cerr << "send reg msg error:" << request << std::endl;
            }
            sem_wait(&rwsem); // 等待信号量，由子线程处理完注册消息会通知这里
        }
        break;
        case 3:
            close(clientfd);
            sem_destroy(&rwsem);
            exit(0);
        default:
            std::cerr << "invalid input!" << std::endl;
            break;
        }
    }

    return 0;
}

// 处理登录响应的逻辑
void doLoginResponse(json &responsejs)
{
    if (0 != responsejs["errno"])
    {
        // 登录失败
        std::cerr << responsejs["errmsg"] << std::endl;
        g_isLoginSuccess = false;
    }
    else
    {
        // 登录成功
        // 记录当前用户的id和name
        g_currentUser.setId(responsejs["id"]);
        g_currentUser.setName(responsejs["name"]);

        // 记录当前用户的好友列表信息
        if (responsejs.contains("friends"))
        {
            // 初始化好友列表
            g_currentFriendList.clear();
            std::vector<std::string> vec = responsejs["friends"];
            for (std::string &str : vec)
            {
                json js = json::parse(str);
                User user;
                user.setId(js["id"]);
                user.setName(js["name"]);
                user.setState(js["state"]);
                g_currentFriendList.push_back(user);
            }
        }

        // 记录当前用户的群组列表信息
        if (responsejs.contains("groups"))
        {
            // 初始化群组信息
            g_currentGroupList.clear();
            std::vector<std::string> vec = responsejs["groups"];
            for (std::string &str : vec)
            {
                json grpjs = json::parse(str);
                Group group;
                group.setId(grpjs["id"]);
                group.setName(grpjs["groupname"]);
                group.setDesc(grpjs["groupdesc"]);
                std::vector<std::string> vec2 = grpjs["users"];

                for (std::string &userstr : vec2)
                {
                    GroupUser user;
                    json js = json::parse(userstr);
                    user.setId(js["id"]);
                    user.setName(js["name"]);
                    user.setState(js["state"]);
                    user.setRole(js["role"]);
                    group.getUsers().push_back(user);
                }
                g_currentGroupList.push_back(group);
            }
        }

        // 显示登录用户的基本信息
        showCurrentUserData();

        // 显示当前用户的离线消息 个人聊天信息或者群组消息
        if (responsejs.contains("offLineMsg"))
        {
            std::vector<std::string> vec = responsejs["offLineMsg"];
            for (std::string &str : vec)
            {
                json js = json::parse(str);
                if (ONE_CHAT_MSG == js["msgid"])
                {
                    std::cout << js["time"] << " [" << js["id"] << "]" << js["name"] << " said: " << js["msg"] << std::endl;
                }
                else
                {
                    std::cout << "群消息[" << js["groupid"] << "]:" << js["time"] << " [" << js["id"] << "]" << js["name"] << " said: " << js["msg"] << std::endl;
                }
            }
        }

        g_isLoginSuccess = true;
    }
}

// 处理注册响应的逻辑
void doRegsponse(json &responsejs)
{

    if (responsejs["errno"] != 0)
    {
        // 注册失败
        std::cerr << " is already exist, register error!" << std::endl;
    }
    else
    {
        // 注册成功
        std::cout << " register sucess, userid is " << responsejs["id"] << ", do not forget it!" << std::endl;
    }
}

// 子线程 - 接收线程
void readTaskHandler(int clientfd)
{
    for (;;)
    {
        char buffer[1024] = {0};
        int len = recv(clientfd, buffer, sizeof(buffer), 0);
        if (-1 == len || 0 == len)
        {
            close(clientfd);
            exit(-1);
        }

        // 接收ChatServer转发的数据 反序列化生成json数据对象
        json js = json::parse(buffer);
        int msgtype = js["msgid"];
        if (ONE_CHAT_MSG == msgtype)
        {
            std::cout << js["time"] << " [" << js["id"] << "]" << js["name"]
                      << " said: " << js["msg"] << std::endl;
        }
        else if (GROUP_CHAT_MSG == msgtype)
        {
            std::cout << "群消息[" << js["groupid"] << "]:" << js["time"] << " ["
                      << js["id"] << "]" << js["name"] << " said: " << js["msg"] << std::endl;
        }
        else if (LOGIN_MSG_ACK == msgtype)
        {
            doLoginResponse(js); // 处理登录响应的业务逻辑
            sem_post(&rwsem);    // 通知主线程，登录结果处理完成
        }
        else if (REG_MSG_ACK == msgtype)
        {
            doRegsponse(js);
            sem_post(&rwsem); // 通知主线程，登录结果处理完成
        }
    }
}

// 显示当前登录成功用户的基本信息
void showCurrentUserData()
{
    std::cout << "======================login user======================" << std::endl;
    std::cout << "current login user => id:" << g_currentUser.getId() << " name:" << g_currentUser.getName() << std::endl;
    std::cout << "----------------------friend list---------------------" << std::endl;
    if (!g_currentFriendList.empty())
    {
        for (User &user : g_currentFriendList)
        {
            std::cout << user.getId() << " " << user.getName() << " " << user.getState() << std::endl;
        }
    }
    std::cout << "----------------------group list----------------------" << std::endl;
    if (!g_currentGroupList.empty())
    {
        for (Group &group : g_currentGroupList)
        {
            std::cout << group.getId() << " " << group.getName() << " " << group.getDesc() << std::endl;
            for (GroupUser &user : group.getUsers())
            {
                std::cout << user.getId() << " " << user.getName() << " " << user.getState()
                          << " " << user.getRole() << std::endl;
            }
        }
    }
    std::cout << "======================================================" << std::endl;
}

// "help" command handler
void help(int fd = 0, std::string str = "");
// "chat" command handler
void chat(int, std::string);
// "addfriend" command handler
void addfriend(int, std::string);
// "creategroup" command handler
void creategroup(int, std::string);
// "addgroup" command handler
void addgroup(int, std::string);
// "groupchat" command handler
void groupchat(int, std::string);
// "loginout" command handler
void loginout(int, std::string);

// 系统支持的客户端命令列表
std::unordered_map<std::string, std::string> commandMap = {
    {"help", "显示所有支持的命令,格式help"},
    {"chat", "一对一聊天,格式chat:friendid:message"},
    {"addfriend", "添加好友,格式addfriend:friendid"},
    {"creategroup", "创建群组,格式creategroup:groupname:groupdesc"},
    {"addgroup", "加入群组,格式addgroup:groupid"},
    {"groupchat", "群聊,格式groupchat:groupid:message"},
    {"loginout", "注销,格式loginout"},
};

// 注册系统支持的客户端命令处理
std::unordered_map<std::string, std::function<void(int, std::string)>> commandHandlerMap = {
    {"help", help},
    {"chat", chat},
    {"addfriend", addfriend},
    {"creategroup", creategroup},
    {"addgroup", addgroup},
    {"groupchat", groupchat},
    {"loginout", loginout},
};

void mainMenu(int clientfd)
{
    help();
    char buffer[1024] = {0};
    while (isMainMenuRunning)
    {
        std::cin.getline(buffer, sizeof(buffer));
        std::string commandBuf(buffer);
        std::string command;
        int idx = commandBuf.find(":");
        if (idx == -1)
        {
            command = commandBuf;
        }
        else
        {
            command = commandBuf.substr(0, idx);
        }
        auto it = commandHandlerMap.find(command);
        if (it == commandHandlerMap.end())
        {
            std::cerr << "invalid input command!" << std::endl;
            continue;
        }

        // 调用相应命令的事件处理回调,manMenu对修改封闭,添加新功能不需要修改该函数
        it->second(clientfd, commandBuf.substr(idx + 1, commandBuf.size() - idx)); // 调用命令处理方法
    }
}

// "help" command handler
void help(int fd, std::string str)
{
    std::cout << "show command list >>>" << std::endl;
    for (auto &p : commandMap)
    {
        std::cout << p.first << ":" << p.second << std::endl;
    }
    std::cout << std::endl;
};

// "addfriend" command handler
void addfriend(int clientfd, std::string str)
{
    int friendid = atoi(str.c_str());
    json js;
    js["msgid"] = ADD_FRIEND_MSG;
    js["id"] = g_currentUser.getId();
    js["friendid"] = friendid;
    std::string buffer = js.dump();

    int len = send(clientfd, buffer.c_str(), buffer.length() + 1, 0);
    if (-1 == len)
    {
        std::cerr << "send addfriend msg error" << buffer << std::endl;
    }
};
// "chat" command handler
void chat(int clientfd, std::string str)
{
    int idx = str.find(":"); // friendid:message
    if (-1 == idx)
    {
        std::cerr << "chat command invalid!" << std::endl;
        return;
    }
    int friendid = atoi(str.substr(0, idx).c_str());
    std::string message = str.substr(idx + 1, str.length() + 1 - idx);

    json js;
    js["msgid"] = ONE_CHAT_MSG;
    js["id"] = g_currentUser.getId();
    js["name"] = g_currentUser.getName();
    js["to"] = friendid;
    js["msg"] = message;
    js["time"] = getCurrentTime();
    std::string buffer = js.dump();

    int len = send(clientfd, buffer.c_str(), buffer.length() + 1, 0);
    if (-1 == len)
    {
        std::cerr << "send chat msg error -> " << buffer << std::endl;
    }
};

// "creategroup" command handler
void creategroup(int clientfd, std::string str)
{
    int idx = str.find(":"); // groupname:groupdesc
    if (-1 == idx)
    {
        std::cerr << "creategroup command invalid!" << std::endl;
        return;
    }
    std::string groupname = str.substr(0, idx);
    std::string groupdesc = str.substr(idx + 1, str.length() + 1 - idx);
    json js;
    js["msgid"] = CREATE_GROUP_MSG;
    js["id"] = g_currentUser.getId();
    js["groupname"] = groupname;
    js["groupdesc"] = groupdesc;
    std::string buffer = js.dump();

    int len = send(clientfd, buffer.c_str(), buffer.length() + 1, 0);
    if (-1 == len)
    {
        std::cerr << "send creategroup msg error -> " << buffer << std::endl;
    }
};
// "addgroup" command handler
void addgroup(int clientfd, std::string str)
{
    int groupid = atoi(str.c_str());
    json js;
    js["msgid"] = ADD_GROUP_MSG;
    js["id"] = g_currentUser.getId();
    js["groupid"] = groupid;

    std::string buffer = js.dump();
    int len = send(clientfd, buffer.c_str(), buffer.length() + 1, 0);
    if (-1 == len)
    {
        std::cerr << "send addgroup msg error -> " << buffer << std::endl;
    }
};
// "groupchat" command handler
void groupchat(int clientfd, std::string str)
{
    int idx = str.find(":");
    if (-1 == idx)
    {
        std::cerr << "groupchat command invalid!" << std::endl;
        return;
    }

    int groupid = atoi(str.substr(0, idx).c_str());
    std::string message = str.substr(idx + 1, str.size() - idx);

    json js;
    js["msgid"] = GROUP_CHAT_MSG;
    js["id"] = g_currentUser.getId();
    js["name"] = g_currentUser.getName();
    js["groupid"] = groupid;
    js["msg"] = message;
    js["time"] = getCurrentTime();
    std::string buffer = js.dump();

    int len = send(clientfd, buffer.c_str(), strlen(buffer.c_str()) + 1, 0);
    if (-1 == len)
    {
        std::cerr << "send groupchat msg error -> " << buffer << std::endl;
    }
};
// "loginout" command handler
void loginout(int clientfd, std::string str)
{
    json js;
    js["msgid"] = LOGINOUT_MSG;
    js["id"] = g_currentUser.getId();
    std::string buffer = js.dump();

    int len = send(clientfd, buffer.c_str(), strlen(buffer.c_str()) + 1, 0);
    if (-1 == len)
    {
        std::cerr << "send loginout msg error -> " << buffer << std::endl;
    }
    isMainMenuRunning = false;
};

// 获取系统时间（聊天信息需要添加时间信息）
std::string getCurrentTime()
{
    auto tt = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    struct tm *ptm = localtime(&tt);
    char date[60] = {0};
    sprintf(date, "%d-%02d-%02d %02d:%02d:%02d",
            (int)ptm->tm_year + 1900, (int)ptm->tm_mon + 1, (int)ptm->tm_mday,
            (int)ptm->tm_hour, (int)ptm->tm_min, (int)ptm->tm_sec);
    return std::string(date);
}
