#include "lst_timer.h"
#include "../http/http_conn.h"


void Utils::init(int timeslot)
{
    m_TIMESLOT = timeslot;
}

//对文件描述符设置非阻塞
int Utils::setnonblocking(int fd)
{
    // 将当前fd设置为非阻塞模式
    int old_option = fcntl(fd, F_GETFL);
    int new_option = old_option | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_option);
    return old_option;
}

//将内核事件表注册读事件，ET模式，选择开启EPOLLONESHOT
void Utils::addfd(int epollfd, int fd, bool one_shot, int TRIGMode)
{
    epoll_event event;
    event.data.fd = fd;
    /*
        EPOLLRDHUP：表示对端关闭连接（或关闭写端，即触发了 shutdown(SHUT_WR)

    */
    if (1 == TRIGMode) 
        event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
    else
        event.events = EPOLLIN | EPOLLRDHUP;

    if (one_shot)
        event.events |= EPOLLONESHOT;
    // 将其事件添加到epoll上
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    setnonblocking(fd);
}

//信号处理函数
void Utils::sig_handler(int sig)
{
    //为保证函数的可重入性，保留原来的errno
    //可重入性表示中断后再次进入该函数，环境变量与之前相同，不会丢失数据
    int save_errno = errno;
    int msg = sig;
    // 通过管道通知主循环
    /*
        当套接字发送缓冲区变满时，send通常会阻塞，除非套接字设置为非阻塞模式，当缓冲区变满时，
        返回EAGAIN或者EWOULDBLOCK错误，此时可以调用select函数来监视何时可以发送数据。
    */
    send(u_pipefd[1], (char *)&msg, 1, 0); // 向管道中写入信号sig
    //将原来的errno赋值为当前的errno
    errno = save_errno;
}

//设置信号函数
void Utils::addsig(int sig, void(handler)(int), bool restart)
{
    struct sigaction sa;// 描述当信号到达时要采取的动作
    memset(&sa, '\0', sizeof(sa));
    // 信号处理函数
    sa.sa_handler = handler;
    // 信号标识
    if (restart) 
        sa.sa_flags |= SA_RESTART; // 使被信号打断的系统调用自动重新发起

    // sa_mask用来指定在信号处理函数执行期间需要被屏蔽的信号
    sigfillset(&sa.sa_mask);// 用来将参数set信号集初始化，然后把所有的信号加入到此信号集里。

    /*
        sig表示操作的信号。
        sa表示对信号设置新的处理方式。
        oldact表示信号原来的处理方式。
    */
    assert(sigaction(sig, &sa, NULL) != -1);
}

//定时处理任务，重新定时以不断触发SIGALRM信号
void Utils::timer_handler()
{
    // 将那些已经超时的连接关闭 https://mp.weixin.qq.com/s/mmXLqh_NywhBXJvI45hchA
    t_min_heap.tick();
    /*
       设置信号传送闹钟，即用来设置信号SIGALRM在经过参数seconds秒数后发送给目前的进程。
       如果未设置信号SIGALRM的处理函数，那么alarm()默认处理终止进程.
    */ 
    // 利用alarm函数周期性地触发SIGALRM信号，信号处理函数利用管道通知主循环，主循环接收到该信
    // 号后对升序链表上所有定时器进行处理，若该段时间内没有交换数据，则将该连接关闭，释放所占用的资源。
    alarm(m_TIMESLOT);// 启动定时器
}

void Utils::show_error(int connfd, const char *info)
{
    // 发送error信息
    send(connfd, info, strlen(info), 0);
    close(connfd);
}

int *Utils::u_pipefd = 0;
int Utils::u_epollfd = 0;

class Utils;
void cb_func(client_data *user_data)
{
    epoll_ctl(Utils::u_epollfd, EPOLL_CTL_DEL, user_data->sockfd, 0);
    assert(user_data);
    close(user_data->sockfd);
    http_conn::m_user_count--;
}
