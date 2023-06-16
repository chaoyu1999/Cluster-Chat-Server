#include "chatservice.hpp"
#include "public.hpp"
#include <muduo/base/Logging.h>
#include <string>
#include <vector>
using namespace muduo;
using namespace std;

ChatService::ChatService()
{
    // 对各类消息处理方法的注册
    _msgHandlerMap.insert({REGISTER_MSG, std::bind(&ChatService::registerHandler, this, _1, _2, _3)});    // 注册
    _msgHandlerMap.insert({LOGIN_MSG, std::bind(&ChatService::loginHandler, this, _1, _2, _3)});          // 登录
    _msgHandlerMap.insert({ADD_FRIEND_MSG, std::bind(&ChatService::addFriendHandler, this, _1, _2, _3)}); // 加好友
    _msgHandlerMap.insert({ONE_CHAT_MSG, std::bind(&ChatService::oneChatHandler, this, _1, _2, _3)});     // 一对一聊天
    _msgHandlerMap.insert({LOGINOUT_MSG, std::bind(&ChatService::loginOutHandler, this, _1, _2, _3)});    // 注销登录

    // 群组业务管理相关事件处理回调注册
    _msgHandlerMap.insert({CREATE_GROUP_MSG, std::bind(&ChatService::createGroup, this, _1, _2, _3)}); // 创群组
    _msgHandlerMap.insert({ADD_GROUP_MSG, std::bind(&ChatService::addGroup, this, _1, _2, _3)});       // 加群组
    _msgHandlerMap.insert({GROUP_CHAT_MSG, std::bind(&ChatService::groupChat, this, _1, _2, _3)});     // 群聊

    // Redis是否启动
    if (_redis.connect())
    {
        LOG_DEBUG << "Redis have connected!";
        _redis.init_notify_handler(std::bind(&ChatService::redis_subscribe_message_handler, this, _1, _2)); // redis订阅消息
    }
    else
    {
        LOG_DEBUG << "Redis have not connected!";
    }

    // 服务器初次启动，将所有用户设置为offline，
    //TODO 这个涉及到多服务器时会出bug
    this->reset();
}

/* 注册业务
逻辑描述：
该函数用于处理注册业务。接收到注册请求后，从传入的json对象中获取用户名和密码。
然后创建一个User对象，设置用户名和密码，并调用_userModel的insert方法将用户插入到数据库中。
如果插入成功，表示注册成功，将注册成功的消息以json形式返回给客户端。
如果插入失败，表示注册失败，将注册失败的消息以json形式返回给客户端。

参数：
- conn：TcpConnectionPtr类型，表示TCP连接的指针。
- js：json类型，表示接收到的请求数据。
- time：Timestamp类型，表示时间戳。

返回值：无。

*/
void ChatService::registerHandler(const TcpConnectionPtr &conn, json &js, Timestamp time)
{
    LOG_DEBUG << "Doing regidster service!";

    std::string name = js["name"];         // 获取注册的用户名
    std::string password = js["password"]; // 获取注册的密码

    User user;                            // 创建用户对象
    user.setName(name);                   // 设置用户名
    user.setPassword(password);           // 设置用户密码
    bool state = _userModel.insert(user); // 插入用户，插入时会自动设置用户id
    if (state)
    {
        // 注册成功
        json response;
        response["msgid"] = REGISTER_MSG_ACK;
        response["errno"] = 0;
        response["id"] = user.getId();
        conn->send(response.dump());
    }
    else
    {
        // 注册失败
        json response;
        response["msgid"] = REGISTER_MSG_ACK;
        response["errno"] = 1;
        // 注册已经失败，不需要在json返回id
        conn->send(response.dump());
    }
}

/* 登录业务
逻辑描述：
该函数用于处理登录业务。接收到登录请求后，从传入的json对象中获取用户id和密码。
然后根据用户id查询数据库中对应的用户信息。如果账号和密码都正确，则进行以下操作：
1. 检查用户状态，如果用户已经在线，则返回登录失败的消息给客户端。
2. 如果用户不在线，记录用户连接信息，并向Redis订阅用户对应的channel。
3. 更新用户状态为在线。
4. 构建回复客户端的消息，包括登录成功的状态、用户id和用户名。
5. 查询该用户是否有离线消息，如果有，则将离线消息加入回复消息，并删除数据库中的离线消息。
6. 查询用户的好友列表，构建好友信息列表加入回复消息。
7. 发送回复消息给客户端。

参数：
- conn：TcpConnectionPtr类型，表示TCP连接的指针。
- js：json类型，表示接收到的请求数据。
- time：Timestamp类型，表示时间戳。

返回值：无。

*/
void ChatService::loginHandler(const TcpConnectionPtr &conn, json &js, Timestamp time)
{
    LOG_DEBUG << "\nDo login service!\n";

    int id = js["id"].get<int>();          // 从数据中获取此id的用户
    std::string password = js["password"]; // 获取密码

    User user = _userModel.query(id); // 根据用户id查询用户信息

    if (user.getId() == id && user.getPassword() == password) // 如果账号和密码都正确
    {
        if (user.getState() == "online") // 该用户已经登录，不能重复登录
        {
            json response;
            response["msgid"] = LOGIN_MSG_ACK;
            response["errno"] = 2;
            response["errmsg"] = "This account is using, cant't login!";
            conn->send(response.dump());
        }
        else // 登录成功，记录用户连接信息
        {
            {
                // 需要考虑线程安全问题 onMessage 会在不同线程中被调用, 然后调用loginHandler，对共享资源_userConnMap进行访问。没锁可能会导致竞态条件、数据竞争、结果不确定性、数据损坏，没有适当的锁保护共享资源 _userConnMap，并发的修改操作可能会导致数据不一致、数据竞争和程序错误。
                lock_guard<mutex> lock(_connMutex);
                _userConnMap.insert({id, conn});
            }
            // id用户登录成功后，向redis订阅channel(id)
            _redis.subscribe(id);

            // 登录成功，更新用户状态信息 state offline => online
            user.setState("online");
            _userModel.updateState(user);

            // 构建回复客户端的消息
            json response;
            response["msgid"] = LOGIN_MSG_ACK;
            response["errno"] = 0;
            response["id"] = user.getId();
            response["name"] = user.getName();

            // 查询该用户是否有离线消息
            std::vector<std::string> vec = _offlineMsgModel.query(id);
            if (!vec.empty())
            {
                response["offlinemsg"] = vec;
                // 读取该用户的离线消息后，将该用户离线消息删除掉
                _offlineMsgModel.remove(id);
            }
            else
            {
                LOG_INFO << "无离线消息";
            }
            // 查询好友在线情况
            std::vector<User> userVec = _friendModel.query(id); // 获取好友列表
            if (!userVec.empty())
            {
                std::vector<std::string> vec; // 好友信息列表
                for (auto &user : userVec)
                {
                    json js;
                    js["id"] = user.getId();
                    js["name"] = user.getName();
                    js["state"] = user.getState();
                    vec.push_back(js.dump());
                }
                response["friends"] = vec; // 好友信息
            }

            conn->send(response.dump());
        }
    }
    else
    { // 用户名或密码错误
        json response;
        response["msgid"] = LOGIN_MSG_ACK;
        response["errno"] = 2;
        response["errmsg"] = "Username or password is error! Please check it!";
        conn->send(response.dump());
    }
}

/* 添加好友业务
逻辑描述：
该函数用于处理添加好友业务。接收到添加好友的请求后，从传入的json对象中获取用户id和要添加的好友id。
然后调用_friendModel的insert方法将好友信息存储到数据库中。

参数：
- conn：TcpConnectionPtr类型，表示TCP连接的指针。
- js：json类型，表示接收到的请求数据。
- time：Timestamp类型，表示时间戳。

返回值：无。

*/
void ChatService::addFriendHandler(const TcpConnectionPtr &conn, json &js, Timestamp time)
{
    int userId = js["id"].get<int>();         // 获取用户id
    int friendId = js["friendid"].get<int>(); // 获取加好友的id
    // 存储好友信息
    _friendModel.insert(userId, friendId); // 存储好友信息
}

/* 一对一聊天业务
逻辑描述：
该函数用于处理一对一聊天业务。接收到一对一聊天消息后，从传入的json对象中获取接收消息的用户id。
然后进行以下操作：
1. 检查接收消息的用户是否在线，如果在线则直接将消息发送给对应的连接。
2. 如果用户不在线，则判断用户在其他主机上的状态，如果状态为在线，则将消息发布到Redis。
3. 如果用户不在线，则将消息存储为离线消息。

参数：
- conn：TcpConnectionPtr类型，表示TCP连接的指针。
- js：json类型，表示接收到的请求数据。
- time：Timestamp类型，表示时间戳。

返回值：无。

*/
void ChatService::oneChatHandler(const TcpConnectionPtr &conn, json &js, Timestamp time)
{
    // 需要接收信息的用户ID
    int toId = js["toid"].get<int>();

    {
        lock_guard<mutex> lock(_connMutex);
        auto it = _userConnMap.find(toId);
        // 确认是在线状态
        if (it != _userConnMap.end())
        {
            // TcpConnection::send() 直接发送消息
            it->second->send(js.dump());
            return;
        }
    }

    // 用户在其他主机的情况，publish消息到redis
    User user = _userModel.query(toId);
    if (user.getState() == "online")
    {
        _redis.publish(toId, js.dump());
        return;
    }

    // toId 不在线则存储离线消息
    _offlineMsgModel.insert(toId, js.dump());
}

/* 注销登录业务
逻辑描述：
该函数用于处理用户注销业务。接收到用户注销请求后，从传入的json对象中获取用户id。
然后进行以下操作：
1. 根据用户id查询数据库中对应的用户信息。
2. 将用户状态设置为"offline"。
3. 调用_userModel的updateState方法更新用户状态为离线。
4. 如果更新失败，则记录错误日志。

参数：
- conn：TcpConnectionPtr类型，表示TCP连接的指针。
- js：json类型，表示接收到的请求数据。
- time：Timestamp类型，表示时间戳。

返回值：无。

*/
void ChatService::loginOutHandler(const TcpConnectionPtr &conn, json &js, Timestamp time)
{
    // 让在线状态为offline
    int id = js["id"].get<int>();     // 从数据中获取此id的用户
    User user = _userModel.query(id); // 根据用户id查询用户信息
    // 登注销成功，更新用户状态信息 state online => offline
    user.setState("offline");
    auto state = _userModel.updateState(user);
    if (!state)
    {
        LOG_ERROR << "\nLoginout error on the id:" << id << "!";
    }
}


/* 创建群组业务
逻辑描述：
该函数用于处理创建群组业务。接收到创建群组的请求后，从传入的json对象中获取用户id、群组名称和群组描述。
然后进行以下操作：
1. 创建Group对象并存储群组信息。
2. 调用_groupModel的createGroup方法将群组信息存储到数据库中。
3. 如果群组创建成功，则调用_groupModel的addGroup方法将创建人信息添加到群组成员中。

参数：
- conn：TcpConnectionPtr类型，表示TCP连接的指针。
- js：json类型，表示接收到的请求数据。
- time：Timestamp类型，表示时间戳。

返回值：无。

*/
void ChatService::createGroup(const TcpConnectionPtr &conn, json &js, Timestamp time)
{
    int userId = js["id"].get<int>();
    std::string name = js["groupname"];
    std::string desc = js["groupesc"];

    // 存储新创建的群组消息
    Group group(-1, name, desc);
    if (_groupModel.createGroup(group))
    {
        // 存储群组创建人信息
        _groupModel.addGroup(userId, group.getId(), "creator");
    }
}

/* 加入群组业务
逻辑描述：
该函数用于处理加入群组业务。接收到加入群组的请求后，从传入的json对象中获取用户id和群组id。
然后进行以下操作：
1. 调用_groupModel的addGroup方法将用户加入群组，角色设置为"normal"。

参数：
- conn：TcpConnectionPtr类型，表示TCP连接的指针。
- js：json类型，表示接收到的请求数据。
- time：Timestamp类型，表示时间戳。

返回值：无。

*/
void ChatService::addGroup(const TcpConnectionPtr &conn, json &js, Timestamp time)
{
    int userId = js["id"].get<int>();
    int groupId = js["groupid"].get<int>();
    _groupModel.addGroup(userId, groupId, "normal");
}

/* 群组聊天业务
逻辑描述：
该函数用于处理群组聊天业务。接收到群组聊天消息后，从传入的json对象中获取用户id和群组id。
然后进行以下操作：
1. 查询群组中的用户列表。
2. 遍历用户列表，对每个用户进行以下操作：
   a. 检查用户是否在线，如果在线则直接将消息发送给对应的连接。
   b. 如果用户不在线，则判断用户在其他主机上的状态，如果状态为在线，则将消息发布到Redis。
   c. 如果用户不在线，则将消息存储为离线消息。

参数：
- conn：TcpConnectionPtr类型，表示TCP连接的指针。
- js：json类型，表示接收到的请求数据。
- time：Timestamp类型，表示时间戳。

返回值：无。

*/
void ChatService::groupChat(const TcpConnectionPtr &conn, json &js, Timestamp time)
{
    int userId = js["id"].get<int>();
    int groupId = js["groupid"].get<int>();
    std::vector<int> userIdVec = _groupModel.queryGroupUsers(userId, groupId);

    lock_guard<mutex> lock(_connMutex);
    for (int id : userIdVec)
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
                // 向群组成员publish信息
                _redis.publish(id, js.dump());
            }
            else
            {
                // 转储离线消息
                _offlineMsgModel.insert(id, js.dump());
            }
        }
    }
}

/* 处理Redis订阅消息的回调函数
逻辑描述：
该函数是处理Redis订阅消息的回调函数。当接收到订阅的消息时，根据消息中的通道(channel)进行以下操作：
1. 检查通道对应的用户是否在线，如果在线则直接将消息发送给对应的连接。
2. 如果用户不在线，则将消息存储为离线消息。

参数：
- channel：int类型，表示订阅消息的通道。
- message：string类型，表示接收到的订阅消息。

返回值：无。

*/
void ChatService::redis_subscribe_message_handler(int channel, string message)
{
    // 用户在线
    lock_guard<mutex> lock(_connMutex);
    auto it = _userConnMap.find(channel);
    if (it != _userConnMap.end())
    {
        it->second->send(message);
        return;
    }

    // 转储离线
    _offlineMsgModel.insert(channel, message);
}

/* 获取对应的消息处理器回调函数
逻辑描述：
该函数用于根据消息ID获取对应的消息处理器(MsgHandler)。首先在_msgHandlerMap中查找是否存在与消息ID对应的处理器，如果存在则返回该处理器；如果不存在则返回一个默认的处理器，该处理器会记录错误日志。

参数：
- msgId：int类型，表示消息ID。

返回值：
- MsgHandler类型，表示消息处理器。

*/
MsgHandler ChatService::getHandler(int msgId)
{
    // 找不到对应处理器的情况
    auto it = _msgHandlerMap.find(msgId);
    if (it == _msgHandlerMap.end())
    {
        // 返回一个默认的处理器(lambda匿名函数，仅仅用作提示)
        return [=](const TcpConnectionPtr &conn, json &js, Timestamp)
        {
            LOG_ERROR << "msgId: " << msgId << " can not find handler!";
        };
    }

    return _msgHandlerMap[msgId];
}


/* 服务器异常，业务重置方法
逻辑描述：
该函数用于重置所有在线用户的状态，将其状态设置为离线（offline）。

参数：无。

返回值：无。

*/
void ChatService::reset()
{
    // 将所有online状态的用户，设置成offline
    _userModel.resetState();
}

/* 客户端异常退出
逻辑描述：
该函数用于处理客户端关闭连接的异常情况。在客户端连接关闭时，需要执行以下操作：
1. 根据连接对象找到对应的用户，并从用户连接映射表中删除该用户的连接信息。
2. 取消用户在Redis上的订阅。
3. 更新用户的状态信息为离线（offline）。

参数：
- conn：TcpConnectionPtr类型，表示关闭的连接对象。

返回值：无。

*/
void ChatService::clientCloseExceptionHandler(const TcpConnectionPtr &conn)
{
    User user;
    // 互斥锁保护
    {
        lock_guard<mutex> lock(_connMutex);
        for (auto it = _userConnMap.begin(); it != _userConnMap.end(); ++it)
        {
            if (it->second == conn)
            {
                // 从map表删除用户的链接信息
                user.setId(it->first);
                _userConnMap.erase(it);
                break;
            }
        }
    }

    // 用户注销
    _redis.unsubscribe(user.getId());

    // 更新用户的状态信息
    if (user.getId() != -1)
    {
        user.setState("offline");
        _userModel.updateState(user);
    }
}








