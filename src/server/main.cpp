#include "chatserver.hpp"
#include "chatservice.hpp"
#include <muduo/base/Logging.h>
#include <iostream>
#include <signal.h>
using namespace std;

// 捕获SIGINT的处理函数
void resetHandler(int)
{
    LOG_INFO << "\n capture the SIGINT, will reset state! \n";
    ChatService::instance()->reset(); // 将所有online状态的用户，设置成offline
    exit(0);
}

int main(int argc, char **argv)
{
    Logger::setLogLevel(Logger::LogLevel::DEBUG); // 设置日志等级
    // 注册SIGINT信号的处理函数当在终端按下Ctrl + C时，会生成SIGINT信号，用于通知正在运行的程序中断执行，并提供一种优雅退出的方式。程序可以通过注册SIGINT信号的处理函数来捕获该信号，并在收到信号时执行特定的操作，比如清理资源、保存状态等。在提供的代码中，SIGINT信号的处理函数被用于重置聊天服务器的状态。
    signal(SIGINT, resetHandler);
    // EventLoop是一个事件循环类，常见于网络编程中。它提供了一个事件循环机制，用于监听和处理各种事件，例如网络连接、数据到达、定时器事件等。
    EventLoop loop;
    // 获取命令行参数中指定的端口号
    InetAddress addr("127.0.0.1", atoi(argv[1]));
    // 创建ChatServer对象,绑定地址并启动
    ChatServer server(&loop, addr, "ChatServer");
    server.start();
    loop.loop();
    return 0;
}