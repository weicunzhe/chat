#ifndef GROUPMODEL_H
#define GROUPMODEL_H

#include "group.hpp"

// 维护群组信息的操作接口类
class GroupModel
{
public:
    // 创建群组
    bool createGroup(Group &group);
    // 加入群聊
    void addGroup(int userid, int groupid, std::string role);
    // 查询用户所在的群聊
    std::vector<Group> queryGroups(int userid);
    // 根据指定的groupid查询群组用户id列表，除userid自己，主要用户群聊业务给群组其他成员发消息
    std::vector<int> queryGroupUsers(int userid, int groupid);
};

#endif