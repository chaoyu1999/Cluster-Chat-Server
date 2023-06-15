#ifndef CHATSERVICE_H
#define CHATSERVICE_H

#include <muduo/net/TcpConnection.h>
#include <unordered_map>
#include <functional>
#include <mutex>
#include "json.hpp"
#include "usermodel.hpp"
#include "offlinemessagemodel.hpp"
#include "friendmodel.hpp"
#include "group_model.hpp"
#include "redis.hpp"

using json = nlohmann::json;
using namespace muduo;
using namespace muduo::net;

// 回调函数类型
using MsgHandler = std::function<void(const TcpConnectionPtr &, json &, Timestamp)>;

// 聊天服务器业务类
class ChatService
{
private:
    ChatService();                                        // 单例模式，构造函数私有化，构造函数ChatService()在首次调用单例对象时被调用，之后的调用将返回先前创建的单例对象。
    ChatService(const ChatService &) = delete;            // 禁用拷贝构造函数，保证单例模式
    ChatService &operator=(const ChatService &) = delete; // 禁用赋值操作符，保证单例模式

    // 存储消息id和其对应的业务处理方法
    std::unordered_map<int, MsgHandler> _msgHandlerMap;

    // 存储在线用户的通信连接
    std::unordered_map<int, TcpConnectionPtr> _userConnMap;

    // 定义互斥锁
    std::mutex _connMutex;

    // redis操作对象
    Redis _redis;

    // 数据操作类对象
    UserModel _userModel;
    OfflineMsgModel _offlineMsgModel;
    FriendModel _friendModel;
    GroupModel _groupModel;

public:
    // ChatService 单例模式
    // 线程安全，这种线程安全性是在 C++11 及以后的标准下保证的。如果在旧的标准中使用这个实现，线程安全性将无法得到保证。
    static ChatService *instance()
    {
        static ChatService service;
        return &service;
    }
    // 获取对应消息的处理器
    MsgHandler getHandler(int msgId);
    // 登录业务
    void loginHandler(const TcpConnectionPtr &conn, json &js, Timestamp time);
    // 注册业务
    void registerHandler(const TcpConnectionPtr &conn, json &js, Timestamp time);
    // 一对一聊天业务
    void oneChatHandler(const TcpConnectionPtr &conn, json &js, Timestamp time);
    // 添加好友业务
    void addFriendHandler(const TcpConnectionPtr &conn, json &js, Timestamp time);
    // 创建群组业务
    void createGroup(const TcpConnectionPtr &conn, json &js, Timestamp time);
    // 加入群组业务
    void addGroup(const TcpConnectionPtr &conn, json &js, Timestamp time);
    // 群组聊天业务
    void groupChat(const TcpConnectionPtr &conn, json &js, Timestamp time);
    // 注销登录业务
    void loginOutHandler(const TcpConnectionPtr &conn, json &js, Timestamp time);

    // 处理客户端异常退出
    void clientCloseExceptionHandler(const TcpConnectionPtr &conn);
    // 服务端异常终止之后的操作
    void reset();
    // redis订阅消息触发的回调函数
    void redis_subscribe_message_handler(int channel, string message);
};

#endif // CHATSERVICE_H