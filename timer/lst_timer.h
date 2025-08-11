#ifndef LST_TIMER
#define LST_TIMER

#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include <sys/stat.h>
#include <string.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <stdarg.h>
#include <errno.h>
#include <sys/wait.h>
#include <sys/uio.h>

#include <time.h>
#include "../log/log.h"
#include "../lock/locker.h"

#include <queue>
#include <vector>
#include <functional> 
#include <unordered_map>
#include <algorithm>
#include <mutex>

/*
非活跃：是指客户端（这里是浏览器）与服务器端建立连接后，长时间不交换数据，
    一直占用服务器端的文件描述符，导致连接资源的浪费。
定时事件：是指固定一段时间之后触发某段代码，由该段代码处理一个事件，如从内
    核事件表删除事件，并关闭文件描述符，释放连接资源。
定时器：是指利用结构体或其他形式，将多种定时事件进行封装起来。具体的，这里
    只涉及一种定时事件，即定期检测非活跃连接，这里将该定时事件与连接资源封装为一个结构体定时器。
定时器容器：是指使用某种容器类数据结构，将上述多个定时器组合起来，便于对定
    时事件统一管理。具体的，项目中使用升序链表将所有定时器串联组织起来。（现在是使用自定义的最小堆来记录定时器了）

定时器超时时间 = 浏览器和服务器连接时刻 + 固定时间(TIMESLOT)，可以看出，
定时器使用绝对时间作为超时值，这里alarm设置为5秒，连接超时为15秒。
*/

class util_timer;

struct client_data {
    sockaddr_in address;
    int sockfd;
    util_timer * timer;
};


// 保持原有结构体定义不变
class util_timer {
public:
    time_t expire;
    void (*cb_func)(client_data*);
    client_data* user_data;
    
    bool operator>(const util_timer& other) const {
        return expire > other.expire; // 小顶堆
    }
};

class timer_min_heap {
private:
    std::mutex mtx_;
    std::vector<util_timer*> heap_;
    std::unordered_map<util_timer*, size_t> timer_to_index_; // 指针到索引的映射

    // 上浮调整
    void sift_up(size_t index) {
        while (index > 0) {
            size_t parent = (index - 1) / 2;
            // 如果当前节点的时间大于父节点，直接退出，不需要考虑继续调整了
            if (*heap_[index] > *heap_[parent]) break;
            // 否则交换父节点和子节点
            std::swap(heap_[index], heap_[parent]);
            // 对应的索引位置也需要发生改变
            timer_to_index_[heap_[index]] = index;
            timer_to_index_[heap_[parent]] = parent;
            // 继续向上调整
            index = parent;
        }
    }

    // 下沉调整
    void sift_down(size_t index) {
        size_t size = heap_.size();
        while (true) {
            // 获得左右孩子的索引位置
            size_t left = 2 * index + 1;
            size_t right = 2 * index + 2;
            size_t smallest = index;
            
            // 当前节点和左右孩子节点的时间大小做对比
            if (left < size && *heap_[left] > *heap_[smallest]) 
                smallest = left;
            if (right < size && *heap_[right] > *heap_[smallest]) 
                smallest = right;
            // 说明左右孩子时间都大于当前父节点时间
            if (smallest == index) break;

            // 否则交换父子节点位置
            std::swap(heap_[index], heap_[smallest]);
            timer_to_index_[heap_[index]] = index;
            timer_to_index_[heap_[smallest]] = smallest;
            // 继续向下调整
            index = smallest;
        }
    }

public:
    // 添加定时器
    void add_timer(util_timer* timer) {
        if (!timer) return;
        heap_.push_back(timer);
        timer_to_index_[timer] = heap_.size() - 1;
        // 添加定时器之后，需要对最小堆进行调整
        sift_up(heap_.size() - 1);
    }

    // 删除定时器
    void del_timer(util_timer* timer) {
        if (!timer || !timer_to_index_.count(timer)) return;
        // 获得当前定时器对应的索引
        size_t index = timer_to_index_[timer];
        // 取出数组的最后一个定时器（也就是最小堆对应的最后一个叶子节点）
        util_timer* last = heap_.back();

        // 用末尾元素替换待删除元素
        heap_[index] = last;
        timer_to_index_[last] = index;

        // 移除末尾元素
        timer_to_index_.erase(timer);
        heap_.pop_back();

        // 交换节点之后，需要调整堆
        if (index < heap_.size()) {
            sift_up(index);
            sift_down(index);
        }
    }

    // 调整定时器时间
    void adjust_timer(util_timer* timer, time_t new_expire) {
        if (!timer || !timer_to_index_.count(timer)) return;

        size_t index = timer_to_index_[timer];
        time_t old_expire = timer->expire;
        timer->expire = new_expire;

        // 根据新时间决定上浮或下沉
        if (new_expire < old_expire) {
            sift_up(index);  // 时间提前，可能需要上浮
        } else {
            sift_down(index); // 时间延后，可能需要下沉
        }
    }

    // 获取堆顶定时器
    util_timer* top() const {
        return heap_.empty() ? nullptr : heap_.front();
    }

    // 核心功能：检查并触发到期定时器 (线程安全)
    /*
        统一事件源，是指将信号事件与其他事件一样被处理。
            具体的，信号处理函数使用管道将信号传递给主循环，信号处理函数往管道的写端写入信号值，
            主循环则从管道的读端读出信号值，使用I/O复用系统调用来监听管道读端的可读事件，这样
            信号事件与其他文件描述符都可以通过epoll来监测，从而实现统一处理。
    */
    void tick() {
        std::lock_guard<std::mutex> lock(mtx_);
        time_t now = time(nullptr);
        
        while (!heap_.empty() && heap_.front()->expire <= now) {
            util_timer* timer = heap_.front();
            
            // 执行回调（注意：回调可能在锁外执行）
            if (timer->cb_func && timer->user_data) {
                mtx_.unlock(); // 解锁以避免死锁（回调可能操作定时器）
                timer->cb_func(timer->user_data);
                mtx_.lock();
            }
            
            // 从堆中移除
            pop();
        }
    }

    // 弹出堆顶定时器
    void pop() {
        if (heap_.empty()) return;
        // 弹出堆顶的元素并删除
        util_timer* timer = heap_.front();
        timer_to_index_.erase(timer);

        // 堆顶元素和最后叶子节点交换之后进行堆的调整，从而达到删除
        if (heap_.size() > 1) { 
            heap_[0] = heap_.back();
            timer_to_index_[heap_[0]] = 0;
            heap_.pop_back();
            // 向下进行调整
            sift_down(0);
        } else {
            // 只有叶子节点直接弹出
            heap_.pop_back();
        }
    }

    bool empty() const {
        return heap_.empty();
    }
};


class Utils
{
    public:
        Utils() {}
        ~Utils() {}

        void init(int timeslot);

        //对文件描述符设置非阻塞
        int setnonblocking(int fd);

        //将内核事件表注册读事件，ET模式，选择开启EPOLLONESHOT
        void addfd(int epollfd, int fd, bool one_shot, int TRIGMode);

        //信号处理函数 
        static void sig_handler(int sig);

        //设置信号函数
        void addsig(int sig, void(handler)(int), bool restart = true);

        //定时处理任务，重新定时以不断触发SIGALRM信号
        void timer_handler();

        void show_error(int connfd, const char *info);

    public: 
        static int *u_pipefd;
        // 定时器双向链表 
        timer_min_heap t_min_heap;
        // epoll的fd
        static int u_epollfd;
        int m_TIMESLOT;
};

void cb_func(client_data *user_data);

#endif
