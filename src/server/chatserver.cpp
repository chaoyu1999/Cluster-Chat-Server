#include "chatserver.hpp"
#include "json.hpp"
#include "chatservice.hpp"
#include <iostream>
#include <functional>
#include <string>
using namespace std;
using namespace placeholders;
using json = nlohmann::json;

// 初始化聊天服务器对象
ChatServer::ChatServer(EventLoop *loop,
                       const InetAddress &listenAddr,
                       const string &nameArg)
    : _server(loop, listenAddr, nameArg), _loop(loop)
{
    // 注册连接事件的回调函数
    _server.setConnectionCallback(std::bind(&ChatServer::onConnection, this, _1));

    // 注册消息事件的回调函数
    _server.setMessageCallback(std::bind(&ChatServer::onMessage, this, _1, _2, _3));

    // 设置subLoop线程数量
    _server.setThreadNum(4);
}

// 启动服务
void ChatServer::start()
{
    _server.start();
}

/**
 * 连接事件相关信息的回调函数
 *
 * 当有连接事件发生时，即客户端与服务器建立或断开连接时，该回调函数会被调用。
 * 在连接断开时，会处理客户端异常退出事件，并进行半关闭操作。
 *
 * @param conn 表示连接对象的智能指针
 */
void ChatServer::onConnection(const TcpConnectionPtr &conn)
{
    // 客户端断开连接
    if (!conn->connected())
    {
        // 处理客户端异常退出事件
        ChatService::instance()->clientCloseExceptionHandler(conn);
        // 半关闭
        conn->shutdown();
    }
}

/* 上报读写事件相关信息的回调函数
*  这段代码实现了将客户端发送过来的消息进行解析，并根据消息类型调用相应的业务处理器来处理该消息。
*  通过这种方式，实现了网络模块与业务模块的解耦，使得代码结构更加灵活和可扩展。
*/
void ChatServer::onMessage(const TcpConnectionPtr &conn,
                           Buffer *buffer,
                           Timestamp time)
{
    // 将json数据转换为string
    string buf = buffer->retrieveAllAsString();
    // 数据的反序列化
    json js = json::parse(buf);

    // 完全解耦网络模块和业务模块，不要在网络模块中调用业务模块的方法
    // 通过 js["msg_id"] 来获取不同的业务处理器（事先绑定的回调方法）
    // js["msgid"].get<int>() 将js["msgid"]对应的值强制转换成int
    auto msgHandler = ChatService::instance()->getHandler(js["msgid"].get<int>());
    // 回调消息绑定好的事件处理器，来执行相应的业务处理
    msgHandler(conn, js, time);
}