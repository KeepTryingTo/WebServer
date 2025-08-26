#ifndef HTTPCONNECTION_H
#define HTTPCONNECTION_H
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
#include <map>
#include <dirent.h>    // 用于目录操作
#include <sys/types.h> // 用于 DIR 等类型定义
#include <time.h>
#include <random>
#include <array>
#include <set>
#include <iostream>

#include "../lock/locker.h"
#include "../CGImysql/sql_connection_pool.h"
#include "../timer/lst_timer.h"
#include "../log/log.h"
#include "../deepLearning/classify/classification.h"
#include "str2float.h"
#include "../deepLearning/objectDetect/objectDetection.h"
#include "upload_file.h"

struct session_info
{
    std::string username;
    time_t create_time;
    time_t last_access;
    bool is_valid;

    session_info() : create_time(0), last_access(0), is_valid(false) {}
    session_info(const std::string &user) : username(user), create_time(time(NULL)),
                                            last_access(time(NULL)), is_valid(true) {}
};

class http_conn
{
public:
    // 定义文件长度，读和写缓冲区大小
    static const int FILENAME_LEN = 200;
    static const int READ_BUFFER_SIZE = 2048 * 128;
    static const int WRITE_BUFFER_SIZE = 1024 * 32;
    static const size_t MAX_UPLOAD_SIZE = 10 * 1024 * 1024; // 10MB
    // HTTP各种请求
    enum METHOD
    {
        GET = 0,
        POST,
        HEAD,
        PUT,
        DELETE,
        TRACE,
        OPTIONS,
        CONNECT,
        PATH
    };
    // 主状态机的状态
    enum CHECK_STATE
    {
        CHECK_STATE_REQUESTLINE = 0, // 请求行状态
        CHECK_STATE_HEADER,          // 请求头状态
        CHECK_STATE_CONTENT          // 请求内容的状态
    };
    enum HTTP_CODE
    {
        NO_REQUEST,  // 请求不完整，需要继续读取请求报文数据
        GET_REQUEST, // 获得了完整的HTTP请求
        BAD_REQUEST, // HTTP请求报文有语法错误
        NO_RESOURCE,
        FORBIDDEN_REQUEST, // 请求资源禁止访问，没有读取权限
        FILE_REQUEST,      // 请求资源可以正常访问
        INTERNAL_ERROR,    // 服务器内部错误，该结果在主状态机逻辑switch的default下，一般不会触发
        CLOSED_CONNECTION
    };
    // 从状态机的状态
    enum LINE_STATUS
    {
        LINE_OK = 0,
        LINE_BAD,
        LINE_OPEN
    };

public:
    http_conn() {}
    ~http_conn() {}

public:
    void init(int sockfd, const sockaddr_in &addr, char *, int, int,
              string user, string passwd, string sqlname);
    void close_conn(bool real_close = true); // 默认为关闭状态
    void process();
    bool read_once();
    bool write();
    sockaddr_in *get_address()
    {
        return &m_address;
    }
    void initmysql_result(connection_pool *connPool);
    int timer_flag;
    int improv;

private:
    void init();
    // 从m_read_buf读取，并处理请求报文
    HTTP_CODE process_read();
    // 向m_write_buf写入响应报文数据
    bool process_write(HTTP_CODE ret);
    // 主状态机解析报文中的请求行数据
    HTTP_CODE parse_request_line(char *text);
    // 主状态机解析报文中的请求头数据
    HTTP_CODE parse_headers(char *text);
    // 主状态机解析报文中的请求内容
    HTTP_CODE parse_content(char *text);
    // 生成响应报文
    HTTP_CODE do_request();
    char *get_line() { return m_read_buf + m_start_line; };
    LINE_STATUS parse_line();
    void unmap();
    bool add_response(const char *format, ...);
    bool add_content(const char *content);
    bool add_status_line(int status, const char *title);
    bool add_headers(int content_length);
    bool add_content_type();
    bool add_content_length(int content_length);
    bool add_linger();
    bool add_blank_line();
    bool add_content_disposition(const char *filename);

public:
    static int m_epollfd;
    static int m_user_count;
    MYSQL *mysql;
    int m_state; // 读为0, 写为1

private:
    int m_sockfd;
    sockaddr_in m_address;

    // 存储读取的请求报文数据
    char m_read_buf[READ_BUFFER_SIZE];
    // 缓冲区中m_read_buf中数据的最后一个字节的下一个位置
    int m_read_idx;
    // m_read_buf读取的位置m_checked_idx
    int m_checked_idx;
    // m_read_buf中已经解析的字符个数
    int m_start_line;

    // 存储发出的响应报文数据
    char m_write_buf[WRITE_BUFFER_SIZE];
    // 指示buffer中的长度
    int m_write_idx;

    // 主状态机的状态
    CHECK_STATE m_check_state;
    // 请求方法
    METHOD m_method;

    char m_real_file[FILENAME_LEN];
    char *m_url;
    char *m_version;
    char *m_host;
    long m_content_length;
    bool m_linger;
    char *m_file_address;
    struct stat m_file_stat;
    struct iovec m_iv[2];
    int m_iv_count;
    int cgi;        // 是否启用的POST
    char *m_string; // 存储请求头数据
    int bytes_to_send;
    int bytes_have_send;
    char *doc_root;

    map<std::string, std::string> m_users;
    int m_TRIGMode;
    int m_close_log;

    char sql_user[100];
    char sql_passwd[100];
    char sql_name[100];

    std::string m_header_value;
    char *m_method_override;
    int chunk_header;
    int total_header;
    char *m_upload_filename;
    long int m_file_size;
    std::string m_download;

    // session  + cookie
    static map<std::string, session_info> sessions;
    static locker session_lock;
    std::string m_session_id;
    bool m_is_logged_in;
    char m_session_id_buf[65];
    bool m_has_session;
    bool m_need_set_cookie;
    set<std::string> sessions_st;

    // 关于session id生成，验证以及管理模块
    std::string generate_session_id();
    bool create_session(const std::string &username);
    bool validate_session(const std::string &session_id);
    void destroy_session(const std::string &session_id);
    bool add_session_cookie();
    static void cleanup_expired_sessions();

    // 图像分类模块
    Classification g_cls;
    char *model_name;
    std::map<std::string, std::string> form_fields;
    bool process_image_classification(const char *image_path);
    bool is_response_result; // 如果图像分类完成，就设置为true，表示可以将结果响应给浏览器了

    // 目标检测系统
    float iou_threshold;
    float conf_threshold;
    std::string imageHW; // 对于目标检测模型输入图像的大小要求
    ObjectDetection g_obj;
    bool is_objectDetect;
    // 保存结果图像
    char save_path[FILENAME_LEN];
    bool process_image_objectDetection(const char *image_path);

    // 浮点数字符串转换为数字浮点数
    Str2Float str2f;
};

#endif
