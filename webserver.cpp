#include "webserver.h"

WebServer::WebServer()
{
    // http_conn类对象
    users = new http_conn[MAX_FD];

    // root文件夹路径
    char server_path[200];
    // 获得当前目录
    getcwd(server_path, 200); // 200表示数组大小
    char root[6] = "/root";
    m_root = (char *)malloc(strlen(server_path) + strlen(root) + 1);
    strcpy(m_root, server_path);
    // 当前目录拼接root目录
    strcat(m_root, root);

    // 定时器
    users_timer = new client_data[MAX_FD];
}

WebServer::~WebServer()
{
    close(m_epollfd);
    close(m_listenfd);
    close(m_pipefd[1]);
    close(m_pipefd[0]);
    delete[] users;
    delete[] users_timer;
    delete m_pool;
}

void WebServer::init(int port, string user, string passWord, string databaseName, int log_write,
                     int opt_linger, int trigmode, int sql_num, int thread_num, int close_log,
                     int actor_model, bool use_ssl, std::string cert_file, std::string private_file)
{
    m_port = port;
    m_user = user;
    m_passWord = passWord;
    m_databaseName = databaseName;
    m_sql_num = sql_num;
    m_thread_num = thread_num;
    m_log_write = log_write;
    m_OPT_LINGER = opt_linger;
    m_TRIGMode = trigmode;
    m_close_log = close_log;
    m_actormodel = actor_model;

    // 确保上传目录存在
    char upload_path[200];
    strcpy(upload_path, m_root);
    strcat(upload_path, "/uploads");
    mkdir(upload_path, 0755);

    use_ssl_ = use_ssl;
    if (use_ssl_)
    {
        printf("using ssl/tls\n");
        try
        {
            opensslContext_ = std::make_shared<OpenSSLContext>(cert_file, private_file);
            printf("Initialize SSL/TLS is successfully!\n");
        }
        catch (const std::exception &e)
        {
            std::cerr << "Failed to initialize SSL: " << e.what() << std::endl;
            exit(EXIT_FAILURE);
        }
    }
}

void WebServer::trig_mode()
{
    // LT + LT
    if (0 == m_TRIGMode)
    {
        m_LISTENTrigmode = 0;
        m_CONNTrigmode = 0;
    }
    // LT + ET
    else if (1 == m_TRIGMode)
    {
        m_LISTENTrigmode = 0;
        m_CONNTrigmode = 1;
    }
    // ET + LT
    else if (2 == m_TRIGMode)
    {
        m_LISTENTrigmode = 1;
        m_CONNTrigmode = 0;
    }
    // ET + ET
    else if (3 == m_TRIGMode)
    {
        m_LISTENTrigmode = 1;
        m_CONNTrigmode = 1;
    }
}

void WebServer::log_write()
{
    if (0 == m_close_log)
    {
        // 初始化日志
        if (1 == m_log_write)
            // 日志名称，是否写入日志，日志文件大小，最大日志行数以及日志队列大小
            Log::get_instance()->init("./ServerLog", m_close_log, 2000, 800000, 800);
        else
            Log::get_instance()->init("./ServerLog", m_close_log, 2000, 800000, 0);
    }
}

void WebServer::sql_pool()
{
    // 初始化数据库连接池
    m_connPool = connection_pool::GetInstance();
    // 数据库用户名，密码，数据库名称，端口，数据库连接数量以及是否写入日志
    m_connPool->init("10.16.110.157", m_user, m_passWord,
                     m_databaseName, 3306, m_sql_num, m_close_log);

    // 初始化数据库读取表（从连接池中取出一个连接，并从数据库中读取对应表的内容，然后使用map<string,string>保存起来）
    //  初始化用户表
    users->initmysql_result(m_connPool);
}

void WebServer::thread_pool()
{
    // 线程池
    m_pool = new threadpool<http_conn>(m_actormodel, m_connPool, m_thread_num);
}

// main eventloop
void WebServer::eventListen()
{
    // 网络编程基础步骤
    m_listenfd = socket(PF_INET, SOCK_STREAM, 0);
    assert(m_listenfd >= 0);

    // 优雅关闭连接
    if (0 == m_OPT_LINGER)
    {
        struct linger tmp = {0, 1};
        setsockopt(m_listenfd, SOL_SOCKET, SO_LINGER, &tmp, sizeof(tmp));
    }
    else if (1 == m_OPT_LINGER)
    {
        struct linger tmp = {1, 1};
        setsockopt(m_listenfd, SOL_SOCKET, SO_LINGER, &tmp, sizeof(tmp));
    }

    int ret = 0;
    struct sockaddr_in address;
    bzero(&address, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY); // 主机字节序→网络字节序（32位）
    address.sin_port = htons(m_port);            // 主机字节序→网络字节序（16位）

    int flag = 1;
    // 设置listen fd的IP地址和端口可重用（应对timewait状态）
    setsockopt(m_listenfd, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(flag));
    ret = bind(m_listenfd, (struct sockaddr *)&address, sizeof(address));
    assert(ret >= 0);
    ret = listen(m_listenfd, 5);
    assert(ret >= 0);

    // 时钟定时时间间隔设置
    utils.init(TIMESLOT);

    // epoll创建内核事件表
    epoll_event events[MAX_EVENT_NUMBER];
    m_epollfd = epoll_create(5); // 5这个数字在这里没有意义
    assert(m_epollfd != -1);
    // 向epoll上面添加连接读事件并且会设置为非阻塞状态
    utils.addfd(m_epollfd, m_listenfd, false, m_LISTENTrigmode);
    // 注意这里赋值给http中的epoll_fd，也就是后面监听之后的accept对应fd回到http_conn.cpp中去进行初中到到epoll上
    http_conn::m_epollfd = m_epollfd;

    // 创建一对相互连接的Unix域套接字，用于进程间通信
    ret = socketpair(PF_UNIX, SOCK_STREAM, 0, m_pipefd);
    assert(ret != -1);
    // 设置管道写端非阻塞
    utils.setnonblocking(m_pipefd[1]);
    // 将管道读端注册到epoll上，以便于当写入信号到m_pipefd[1]就能感知到
    utils.addfd(m_epollfd, m_pipefd[0], false, 0);

    // 添加忽略信号
    utils.addsig(SIGPIPE, SIG_IGN);
    // 由alarm系统调用产生timer时钟信号，时间到了就处理信号函数；分别为超时和服务停止设置信号处理函数
    utils.addsig(SIGALRM, utils.sig_handler, false);
    // 终止信号，kill进程时触发这个信号（Ctrl + C会触发SIGINT信号）
    utils.addsig(SIGTERM, utils.sig_handler, false);

    // 添加一个时钟（默认为5秒）每 TIMESLOT秒触发一次 SIGALRM信号，作为时间基准
    // 时间到触发这个信号SIGALRM，那么就会执行信号处理函数sig_handler，那么就
    // 会执行信号处理函数中的send函数发送给管道m_pipefd[1]，由于管道的 m_pipefd[0]
    // 是注册在epoll上面的，因此 m_pipefd[0]就可以感知到并执行dealwithsignal函数中recv
    // 接收到这个信号
    alarm(TIMESLOT);

    // 工具类,信号和描述符基础操作
    Utils::u_pipefd = m_pipefd;
    Utils::u_epollfd = m_epollfd;
}

void WebServer::timer(int connfd, struct sockaddr_in client_address)
{
    if (fd_sslwrappers.find(connfd) != fd_sslwrappers.end())
    {
        // 建立HTTP连接
        users[connfd].init(connfd, client_address, m_root, m_CONNTrigmode,
                           m_close_log, m_user, m_passWord, m_databaseName,
                           use_ssl_, opensslContext_, fd_sslwrappers[connfd]);
    }
    else
    {
        // 建立HTTP连接
        users[connfd].init(connfd, client_address, m_root, m_CONNTrigmode,
                           m_close_log, m_user, m_passWord, m_databaseName,
                           use_ssl_, opensslContext_, nullptr);
    }

    // 初始化client_data数据
    // 创建定时器，设置回调函数和超时时间，绑定用户数据，将定时器添加到链表中
    users_timer[connfd].address = client_address;
    users_timer[connfd].sockfd = connfd;
    util_timer *timer = new util_timer;
    timer->user_data = &users_timer[connfd];
    timer->cb_func = cb_func;
    time_t cur = time(NULL);
    timer->expire = cur + 3 * TIMESLOT;
    users_timer[connfd].timer = timer;

    // 将当前定时器添加到链表中保存
    utils.t_min_heap.add_timer(timer);

    printf("%s %d add timer is finished!----------------->\n", __FILE__, __LINE__);
}

// 若有数据传输，则将定时器往后延迟3个单位
// 并对新的定时器在链表上的位置进行调整
void WebServer::adjust_timer(util_timer *timer)
{
    time_t cur = time(NULL);
    if (timer && timer->cb_func && timer->user_data->timer)
    {
        utils.t_min_heap.adjust_timer(timer, cur + 3 * TIMESLOT);
    }

    LOG_INFO("%s", "adjust timer once");
}

void WebServer::deal_timer(util_timer *timer, int sockfd)
{
    // 从epoll上删除对应的fd
    timer->cb_func(&users_timer[sockfd]);
    if (fd_sslwrappers.find(sockfd) != fd_sslwrappers.end())
    {
        fd_sslwrappers.erase(sockfd);
    }
    if (timer)
    {
        // 并将已经执行之后的定时器从链表中删除
        utils.t_min_heap.del_timer(timer);
    }

    LOG_INFO("close fd %d", users_timer[sockfd].sockfd);
}

bool WebServer::dealclientdata()
{
    struct sockaddr_in client_address;
    socklen_t client_addrlength = sizeof(client_address);
    // 默认水平触发模式
    if (0 == m_LISTENTrigmode)
    {
        // 和客户端建立连接
        int connfd = accept(m_listenfd, (struct sockaddr *)&client_address, &client_addrlength);
        if (connfd < 0)
        {
            LOG_ERROR("%s:errno is:%d", "accept error", errno);
            return false;
        }
        if (http_conn::m_user_count >= MAX_FD)
        {
            utils.show_error(connfd, "Internal server busy");
            LOG_ERROR("%s", "Internal server busy");
            return false;
        }

        if (use_ssl_)
        {
            std::shared_ptr<SSLWrapper> ssl_wrapper = std::make_unique<SSLWrapper>(connfd, opensslContext_->get());
            // 设置SSL模式为非阻塞
            SSL_set_mode(ssl_wrapper->getSSL(), SSL_MODE_ACCEPT_MOVING_WRITE_BUFFER);
            // 如果ssl没有建立成功就直接退出
            if (ssl_wrapper->accept() <= 0)
            {
                LOG_ERROR("%s %d SSL/TLS connect failed!", __FILE__, __LINE__);
                printf("%s %d SSL/TLS connect failed!\n", __FILE__, __LINE__);
                return false;
            }
            fd_sslwrappers[connfd] = ssl_wrapper;
        }
        timer(connfd, client_address);
    }
    else
    {
        // 如果是边缘触发模式，就要保证这次的把所有的连接读取完
        while (1)
        {
            int connfd = accept(m_listenfd, (struct sockaddr *)&client_address, &client_addrlength);
            if (connfd < 0)
            {
                LOG_ERROR("%s:errno is:%d", "accept error", errno);
                break;
            }
            // 当HTTP连接的数量大于指定数量就直接返回
            if (http_conn::m_user_count >= MAX_FD)
            {
                utils.show_error(connfd, "Internal server busy");
                LOG_ERROR("%s", "Internal server busy");
                break;
            }
            if (use_ssl_)
            {
                std::shared_ptr<SSLWrapper> ssl_wrapper = std::make_unique<SSLWrapper>(connfd, opensslContext_->get());
                // 设置SSL模式为非阻塞
                SSL_set_mode(ssl_wrapper->getSSL(), SSL_MODE_ACCEPT_MOVING_WRITE_BUFFER);
                // 如果ssl没有建立成功就直接退出
                if (ssl_wrapper->accept() <= 0)
                {
                    LOG_ERROR("%s %d SSL/TLS connect failed!", __FILE__, __LINE__);
                    printf("%s %d SSL/TLS connect failed!\n", __FILE__, __LINE__);
                    return false;
                }
                fd_sslwrappers[connfd] = ssl_wrapper;
            }
            timer(connfd, client_address);
        }
        return false;
    }
    return true;
}

bool WebServer::dealwithsignal(bool &timeout, bool &stop_server)
{
    int ret = 0;
    int sig;
    char signals[1024];
    // 接收信号处理函数发送的信号
    ret = recv(m_pipefd[0], signals, sizeof(signals), 0);
    if (ret == -1)
    {
        return false;
    }
    else if (ret == 0)
    {
        return false;
    }
    else
    {
        // 遍历当前所有的信号情况

        for (int i = 0; i < ret; ++i)
        {
            switch (signals[i])
            {
            case SIGALRM:
            {
                timeout = true;
                break;
            }
            case SIGTERM:
            {
                stop_server = true;
                break;
            }
            }
        }
    }
    return true;
}

void WebServer::dealwithread(int sockfd)
{
    // 获得一个定时器任务
    util_timer *timer = users_timer[sockfd].timer;

    // reactor
    if (1 == m_actormodel)
    {
        // 执行读任务，在定时器未到期之前，重置定时器的时间
        if (timer)
        {
            adjust_timer(timer);
        }

        // 若监测到读事件，将该事件放入请求队列
        m_pool->append(users + sockfd, 0);

        while (true)
        {
            // 判断当前是否进行了提升
            if (1 == users[sockfd].improv)
            {
                // 表示执行失败，将对应的fd 从epoll上删除以及关闭对应连接
                if (1 == users[sockfd].timer_flag)
                {
                    deal_timer(timer, sockfd);
                    users[sockfd].timer_flag = 0;
                }
                users[sockfd].improv = 0;
                break;
            }
        }
    }
    else
    {
        // proactor
        if (users[sockfd].read_once()) // 一次性读取成功
        {
            LOG_INFO("deal with the client(%s)", inet_ntoa(users[sockfd].get_address()->sin_addr));

            // 若监测到读事件，将该事件放入请求队列，注意users是一个指针类型，users + sockfd表示指向了sockfd的位置http_conn
            m_pool->append_p(users + sockfd);

            // 重置定时器时间
            if (timer)
            {
                adjust_timer(timer);
            }
        }
        else
        {
            // 读取失败就删除对应的定时器
            deal_timer(timer, sockfd);
        }
    }
}

void WebServer::dealwithwrite(int sockfd)
{
    util_timer *timer = users_timer[sockfd].timer;
    // reactor
    if (1 == m_actormodel)
    {
        // 执行写事件，重置定时器时间
        if (timer)
        {
            adjust_timer(timer);
        }

        m_pool->append(users + sockfd, 1);

        while (true)
        {
            if (1 == users[sockfd].improv)
            {
                // 执行写失败就删除对应的定时器
                if (1 == users[sockfd].timer_flag)
                {
                    deal_timer(timer, sockfd);
                    users[sockfd].timer_flag = 0;
                }
                users[sockfd].improv = 0;
                break;
            }
        }
    }
    else
    {
        // proactor
        if (users[sockfd].write())
        {
            LOG_INFO("send data to the client(%s)", inet_ntoa(users[sockfd].get_address()->sin_addr));

            // 重置定时器时间
            if (timer)
            {
                adjust_timer(timer);
            }
        }
        else
        {
            deal_timer(timer, sockfd);
        }
    }
}
// sub_eventloop
void WebServer::eventLoop()
{
    bool timeout = false;
    bool stop_server = false;

    while (!stop_server)
    {
        // 从epoll上获得触发的事件
        int number = epoll_wait(m_epollfd, events, MAX_EVENT_NUMBER, -1);
        if (number < 0 && errno != EINTR)
        {
            LOG_ERROR("%s", "epoll failure");
            break;
        }

        for (int i = 0; i < number; i++)
        {
            int sockfd = events[i].data.fd;

            // 处理新到的客户连接
            if (sockfd == m_listenfd)
            {
                bool flag = dealclientdata();
                if (false == flag)
                    continue;
            }
            else if (events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) // 对端关闭连接
            {
                // 服务器端关闭连接，移除对应的定时器
                util_timer *timer = users_timer[sockfd].timer;
                deal_timer(timer, sockfd);
            }
            // 处理信号，并且发生的是读事件
            else if ((sockfd == m_pipefd[0]) && (events[i].events & EPOLLIN))
            {
                // 接受进程发送的信号，判断是超时信号还是服务停止信号
                bool flag = dealwithsignal(timeout, stop_server);
                if (false == flag)
                    LOG_ERROR("%s", "dealclientdata failure");
            }
            // 处理客户连接上接收到的数据
            else if (events[i].events & EPOLLIN)
            {
                dealwithread(sockfd);
            }
            // 处理写事件
            else if (events[i].events & EPOLLOUT)
            {
                dealwithwrite(sockfd);
            }
        }
        if (timeout)
        {
            // 如果是超时信号，就把那些定时器过时的都从最小堆中删除
            utils.timer_handler();

            LOG_INFO("%s", "timer tick");

            timeout = false;
        }
    }
}
