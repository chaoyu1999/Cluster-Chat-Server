#include "chatserver.hpp"
#include "chatservice.hpp"  
#include <muduo/base/Logging.h>
#include <iostream>
#include <signal.h>
using namespace std;

// 捕获SIGINT的处理函数  
void resetHandler(int)  
{   
    LOG_INFO << "capture the SIGINT, will reset state\n";  
    ChatService::instance()->reset();  
    exit(0);
}

int main(int argc, char **argv)  
{
    // 注册SIGINT信号的处理函数 
    signal(SIGINT, resetHandler);  
    
    EventLoop loop;  
     // 获取命令行参数中指定的端口号
    InetAddress addr("127.0.0.1", atoi(argv[1]));  
    
    // 创建ChatServer对象,绑定地址并启动
    ChatServer server(&loop, addr, "ChatServer");   

    server.start();  
    loop.loop();  

    return 0;
}