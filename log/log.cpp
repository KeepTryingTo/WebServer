#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <stdarg.h>
#include "log.h"
#include <pthread.h>
using namespace std;

Log::Log()
{
    m_count = 0;
    m_is_async = false;
}

Log::~Log()
{
    if (m_fp != NULL)
    {
        fclose(m_fp);
    }
}
//异步需要设置阻塞队列的长度，同步不需要设置
bool Log::init(
    const char *file_name, 
    int close_log, 
    int log_buf_size, 
    int split_lines, 
    int max_queue_size)
{
    //如果设置了max_queue_size,则设置为异步
    if (max_queue_size >= 1)
    {
        m_is_async = true;
        m_log_queue = new block_queue<string>(max_queue_size);
        pthread_t tid;
        //flush_log_thread为回调函数,这里表示创建线程异步写日志
        pthread_create(&tid, NULL, flush_log_thread, NULL);
    }
    // 是否关闭日志
    m_close_log = close_log;
    // 日志缓冲区大小
    m_log_buf_size = log_buf_size;
    m_buf = new char[m_log_buf_size];
    memset(m_buf, '\0', m_log_buf_size);

    // 日志最大行数
    m_split_lines = split_lines;

    time_t t = time(NULL);
    struct tm *sys_tm = localtime(&t);// 当前系统时间格式转换
    struct tm my_tm = *sys_tm;

    // 文件名 + 路径斜杠（用于在字符串中查找最后一个出现的指定字符）
    const char *p = strrchr(file_name, '/');
    // 日志文件名
    char log_full_name[256] = {0};

    if (p == NULL)
    {
        snprintf(log_full_name, 255, "%d_%02d_%02d_%s", my_tm.tm_year + 1900, 
            my_tm.tm_mon + 1, my_tm.tm_mday, file_name);
    }else{
        // 最后一个斜杠之后的就是日志文件名了
        strcpy(log_name, p + 1);
        // 复制指定长度字符串到dir_name中
        strncpy(dir_name, file_name, p - file_name + 1);
        // 格式化输出到log_full_name中
        snprintf(log_full_name, 255, "%s%d_%02d_%02d_%s", dir_name, 
            my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday, log_name);
    }

    // 记录当前日志的天数
    m_today = my_tm.tm_mday;
    
    m_fp = fopen(log_full_name, "a");
    if (m_fp == NULL)
    {
        return false;
    }

    return true;
}

void Log::write_log(int level, const char *format, ...)
{
    struct timeval now = {0, 0};
    gettimeofday(&now, NULL);
    time_t t = now.tv_sec;
    // 将时间秒转换为指定的表示格式
    struct tm *sys_tm = localtime(&t);
    struct tm my_tm = *sys_tm;
    char s[16] = {0};
    switch (level)
    {
    case 0:
        strcpy(s, "[debug]:");
        break;
    case 1:
        strcpy(s, "[info]:");
        break;
    case 2:
        strcpy(s, "[warn]:");
        break;
    case 3:
        strcpy(s, "[erro]:");
        break;
    default:
        strcpy(s, "[info]:");
        break;
    }
    //写入一个log，对m_count++, m_split_lines最大行数
    m_mutex.lock();
    m_count++;

    // 如果当前时间不是和日志记录的那天时间相同或者日志行数达到了最大，那么就需要刷新日志到磁盘中了
    if (m_today != my_tm.tm_mday || m_count % m_split_lines == 0) //everyday log
    {
        
        char new_log[256] = {0};
        fflush(m_fp);// 刷新日志到磁盘中
        fclose(m_fp);// 关闭当前日志文件
        char tail[16] = {0};
        
        // 格式化当前时间
        snprintf(tail, 16, "%d_%02d_%02d_", my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday);
       
        // 如果是天数对不上就作为新的日志日期进行重新记录
        if (m_today != my_tm.tm_mday)
        {
            snprintf(new_log, 255, "%s%s%s", dir_name, tail, log_name);
            m_today = my_tm.tm_mday;// 重置天数
            m_count = 0;
        }
        else // 否则按照行数作为文件名来格式化当前日志名称
        {
            snprintf(new_log, 255, "%s%s%s.%lld", dir_name, tail, log_name, m_count / m_split_lines);
        }
        // 打开一个新的文件
        m_fp = fopen(new_log, "a");
    }
 
    m_mutex.unlock();

    // 日志写入的格式
    va_list valst;
    // 初始化一个可变参数列表​​，用于处理 printf 风格的格式化字符串（即带有 %d、%s 等占位符的字符串）
    va_start(valst, format);

    string log_str;
    m_mutex.lock();

    //写入的具体时间内容格式，将时间戳格式化为 YYYY-MM-DD HH:MM:SS.μμμμμμ [s] 的形式
    int n = snprintf(m_buf, 48, "%d-%02d-%02d %02d:%02d:%02d.%06ld %s ",
                     my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday,
                     my_tm.tm_hour, my_tm.tm_min, my_tm.tm_sec, now.tv_usec, s);
    // 将可变参数日志内容（如 "User %s logged in"）追加到时间戳之后
    /*
        参数说明​​：
            m_buf + n：从时间戳的结尾位置开始写入。
            m_log_buf_size - n - 1：剩余缓冲区大小（保留 1 字节给 \n）。
            format：格式化字符串（如 "User %s logged in"）。
            valst：va_list 类型的可变参数列表。
    */
    int m = vsnprintf(m_buf + n, m_log_buf_size - n - 1, format, valst);
    m_buf[n + m] = '\n';
    m_buf[n + m + 1] = '\0';
    log_str = m_buf;

    m_mutex.unlock();
    
    // 如果是异步写入就加入到日志队列中，否则就直接写入文件中
    if (m_is_async && !m_log_queue->full())
    {
        m_log_queue->push(log_str);
    }
    else
    {
        m_mutex.lock();
        fputs(log_str.c_str(), m_fp);
        m_mutex.unlock();
    }

    va_end(valst);
}

void Log::flush(void)
{
    m_mutex.lock();
    //强制刷新写入流缓冲区
    fflush(m_fp);
    m_mutex.unlock();
}
