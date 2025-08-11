#ifndef WEBSERVER_H
#define WEBSERVER_H

#include <sys/socket.h> 
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h> 
#include <cassert>
#include <sys/epoll.h>

#include "./threadpool/threadpool.h"
#include "./http/http_conn.h" 

const int MAX_FD = 65536;           //最大文件描述符
const int MAX_EVENT_NUMBER = 10000; //最大事件数
const int TIMESLOT = 5;             //最小超时单位

class WebServer
{
    public:
        WebServer();
        ~WebServer();

        void init(int port , string user, string passWord, string databaseName,
                int log_write , int opt_linger, int trigmode, int sql_num,
                int thread_num, int close_log, int actor_model);
        
        // 创建线程池
        void thread_pool();
        void sql_pool();// 数据库连接池
        void log_write();// 写日志
        void trig_mode();// 触发模式
        void eventListen(); // 连接事件
        void eventLoop(); // 事件循环
        // 定时器
        void timer(int connfd, struct sockaddr_in client_address);
        // 调整定时器
        void adjust_timer(util_timer *timer);
        void deal_timer(util_timer *timer, int sockfd);
        bool dealclientdata();
        bool dealwithsignal(bool& timeout, bool& stop_server);
        void dealwithread(int sockfd);
        void dealwithwrite(int sockfd);

    public:
        //基础
        int m_port;
        char *m_root;
        int m_log_write;
        int m_close_log;
        int m_actormodel;// 默认为proactor模式

        int m_pipefd[2];
        int m_epollfd; // IO多路复用fd
        http_conn *users;

        //数据库相关
        connection_pool *m_connPool;
        string m_user;         //登陆数据库用户名
        string m_passWord;     //登陆数据库密码
        string m_databaseName; //使用数据库名
        int m_sql_num;// 数据库连接数量

        //线程池相关
        threadpool<http_conn> *m_pool; 
        int m_thread_num;// 线程池中线程数量

        //epoll_event相关
        epoll_event events[MAX_EVENT_NUMBER];

        int m_listenfd;
        int m_OPT_LINGER;// 是否优雅关闭连接
        int m_TRIGMode; // 组合触发模式
        int m_LISTENTrigmode; // 监听fd触发模式
        int m_CONNTrigmode; // 连接fd触发模式

        //定时器相关
        client_data *users_timer;
        Utils utils;
};
#endif
