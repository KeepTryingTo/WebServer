#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <list>
#include <cstdio>
#include <exception>
#include <pthread.h>
#include "../lock/locker.h"
#include "../CGImysql/sql_connection_pool.h"

template <typename T> 
class threadpool
{
public:
    /*thread_number是线程池中线程的数量，max_requests是请求队列中最多允许的、等待处理的请求的数量*/
    threadpool(
        int actor_model, 
        connection_pool *connPool, 
        int thread_number = 8, 
        int max_request = 10000
    );
    ~threadpool();
    bool append(T *request, int state);
    bool append_p(T *request);

private:
    /*工作线程运行的函数，它不断从工作队列中取出任务并执行之*/
    static void *worker(void *arg);
    void run();

private:
    int m_thread_number;        //线程池中的线程数
    int m_max_requests;         //请求队列中允许的最大请求数
    pthread_t *m_threads;       //描述线程池的数组，其大小为m_thread_number
    std::list<T *> m_workqueue; //请求队列
    locker m_queuelocker;       //保护请求队列的互斥锁
    sem m_queuestat;            //是否有任务需要处理（信号默认初始化资源为0）
    connection_pool *m_connPool;  //数据库
    int m_actor_model;          //模型切换
};
template <typename T>
threadpool<T>::threadpool( int actor_model, connection_pool *connPool, 
    int thread_number, int max_requests) : m_actor_model(actor_model), 
    m_thread_number(thread_number), m_max_requests(max_requests),
    m_threads(NULL),m_connPool(connPool)
{
    if (thread_number <= 0 || max_requests <= 0)
        throw std::exception();
    // 保存线程ID的数组（C++中可以使用vector数组保存）
    m_threads = new pthread_t[m_thread_number];
    if (!m_threads)
        throw std::exception();
    for (int i = 0; i < thread_number; ++i)
    {
        if (pthread_create(m_threads + i, NULL, worker, this) != 0){
            delete[] m_threads;
            throw std::exception();
        }
        if (pthread_detach(m_threads[i])){
            delete[] m_threads;
            throw std::exception();
        }
    }
}
template <typename T>
threadpool<T>::~threadpool()
{
    delete[] m_threads;
}
template <typename T>
bool threadpool<T>::append(T *request, int state)
{
    m_queuelocker.lock();
    // 如果当前工作队列大于指定的最大请求数，直接返回
    if (m_workqueue.size() >= m_max_requests)
    {
        m_queuelocker.unlock();
        return false;
    }
    // 标志当前请求状态并加入请求队列中(0-表示读，1-表示写)
    request->m_state = state;
    m_workqueue.push_back(request);

    m_queuelocker.unlock();
    m_queuestat.post();// 需要处理的任务数量减一
    return true;
}
template <typename T>
bool threadpool<T>::append_p(T *request)
{
    // 保证线程安全
    m_queuelocker.lock();
    //根据硬件，预先设置请求队列的最大值
    if (m_workqueue.size() >= m_max_requests)
    {
        m_queuelocker.unlock();
        return false;
    }
    m_workqueue.push_back(request);
    m_queuelocker.unlock();
    //信号量提醒有任务要处理
    m_queuestat.post();
    return true;
}
template <typename T>
void *threadpool<T>::worker(void *arg) // 静态成员函数，通过传参（this）来访问私有成员变量
{
    threadpool *pool = (threadpool *)arg;
    pool->run();
    return pool;
}
template <typename T>
void threadpool<T>::run()
{
    while (true)
    {
        // 等待需要处理的任务，如果没有就阻塞，否则取出任务执行（使用信号量来记录需要处理的任务，大于0表示有多个任务需要处理）
        m_queuestat.wait();
        m_queuelocker.lock();
        if (m_workqueue.empty())
        {
            m_queuelocker.unlock();
            continue;
        }
        // 取出一个任务
        T *request = m_workqueue.front();
        m_workqueue.pop_front();
        m_queuelocker.unlock();
        if (!request)
            continue;
        // 如果当前是reactor模式
        if (1 == m_actor_model)
        {
            // 判断当前请求状态（读还是写状态）
            if (0 == request->m_state)
            {
                // 一次性读取所有数据（读取请求消息）
                if (request->read_once())
                {
                    request->improv = 1;
                    // 从数据库连接池中取出一个连接
                    connectionRAII mysqlcon(&request->mysql, m_connPool);
                    // 请求读还是请求写，解析完所有消息之后就是发送响应
                    request->process();//process(模板类中的方法,这里是http类)进行处理
                }else{
                    // 读取失败
                    request->improv = 1;
                    request->timer_flag = 1;// 读取失败后面就需要删除定时器（并将在epoll上监听的fd也删除和关闭连接）
                }
            }else{
                // 写数据是否成功（当然缓冲区写满之后，返回也是true）
                if (request->write()) 
                {
                    request->improv = 1;
                }else{
                    // 写入失败
                    request->improv = 1;
                    // 写入失败后面就需要删除定时器（并将在epoll上监听的fd也删除和关闭连接），不是一种优雅的关闭连接
                    request->timer_flag = 1;
                }
            }
        }
        else
        {
            // 如果是proactor模式的话，在webserver那里就已经一次性读取出来或者写入了，因此这里直接取出一个连接，进行后面的处理即可
            // 不需要再进行读和写操作了 
            connectionRAII mysqlcon(&request->mysql, m_connPool);
            request->process();
        }
    }
}
#endif
