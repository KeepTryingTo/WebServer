#include "config.h"

/*
    ./server -s 10 -t 10

*/

int main(int argc, char *argv[])
{
    //需要修改的数据库信息,登录名,密码,库名
    string user = "root";
    string passwd = "123456";
    string databasename = "dbConnectPool"; 

    //命令行解析
    Config config;
    config.parse_arg(argc, argv); 

    WebServer server;

    //初始化
    server.init(config.PORT, user, passwd, databasename, config.LOGWrite, 
                config.OPT_LINGER, config.TRIGMode,  config.sql_num,  config.thread_num, 
                config.close_log, config.actor_model);
    

    //初始化日志（同步还是异步）
    server.log_write();
 
    //建立数据库连接池
    server.sql_pool();

    //创建线程池
    server.thread_pool();

    //初始化listen fd和connect fd触发模式
    server.trig_mode();

    //监听
    server.eventListen();

    //运行
    server.eventLoop();

    return 0;
}