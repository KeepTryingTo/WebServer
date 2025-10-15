

#pragma

#include <iostream>
#include <string>
#include <cstring>
#include <ostream>

// 增强的session_info结构体
struct enhanced_session_info
{
    std::string session_id;
    std::string username;
    std::string client_ip; // 客户端IP地址
    int client_port;       // 客户端端口
    time_t create_time;    // 创建时间
    time_t last_access;    // 最后访问时间
    time_t expire_time;    // 过期时间
    /*
        User-Agent是浏览器发送到服务器的特殊字符串，用于标识客户端的硬件和软件配置，包括浏览器类型、版本、操作系统等信息。
    */
    std::string user_agent;             // 用户浏览器信息
    bool is_valid;                      // 是否有效
    std::string connection_fingerprint; // 连接指纹

    // 构造函数
    enhanced_session_info() : client_port(0), create_time(0),
                              last_access(0), expire_time(0), is_valid(false) {}

    enhanced_session_info(const std::string &id, const std::string &user,
                          const std::string &ip, int port, const std::string &agent = "")
        : session_id(id), username(user), client_ip(ip), client_port(port),
          user_agent(agent), is_valid(true)
    {
        time_t now = time(NULL);
        create_time = now;
        last_access = now;
        expire_time = now + 1800; // 30分钟过期
    }

    // 拷贝构造函数
    enhanced_session_info(const enhanced_session_info &se_info)
        : session_id(se_info.session_id),
          username(se_info.username),
          client_ip(se_info.client_ip),
          client_port(se_info.client_port),
          create_time(se_info.create_time),
          last_access(se_info.last_access),
          expire_time(se_info.expire_time),
          user_agent(se_info.user_agent),
          is_valid(se_info.is_valid),
          connection_fingerprint(se_info.connection_fingerprint)
    {
    }

    // 拷贝赋值运算符
    enhanced_session_info &operator=(const enhanced_session_info &se_info)
    {
        if (this != &se_info)
        {
            session_id = se_info.session_id;
            username = se_info.username;
            client_ip = se_info.client_ip;
            client_port = se_info.client_port;
            create_time = se_info.create_time;
            last_access = se_info.last_access;
            expire_time = se_info.expire_time;
            user_agent = se_info.user_agent;
            is_valid = se_info.is_valid;
            connection_fingerprint = se_info.connection_fingerprint;
        }
        return *this;
    }

    // 移动构造函数
    enhanced_session_info(enhanced_session_info &&se_info) noexcept
        : session_id(std::move(se_info.session_id)),
          username(std::move(se_info.username)),
          client_ip(std::move(se_info.client_ip)),
          client_port(se_info.client_port),
          create_time(se_info.create_time),
          last_access(se_info.last_access),
          expire_time(se_info.expire_time),
          user_agent(std::move(se_info.user_agent)),
          is_valid(se_info.is_valid),
          connection_fingerprint(std::move(se_info.connection_fingerprint))
    {
        // 将源对象重置为无效状态
        se_info.client_port = 0;
        se_info.create_time = 0;
        se_info.last_access = 0;
        se_info.expire_time = 0;
        se_info.is_valid = false;
    }

    // 移动赋值运算符
    enhanced_session_info &operator=(enhanced_session_info &&se_info) noexcept
    {
        if (this != &se_info)
        {
            session_id = std::move(se_info.session_id);
            username = std::move(se_info.username);
            client_ip = std::move(se_info.client_ip);
            client_port = se_info.client_port;
            create_time = se_info.create_time;
            last_access = se_info.last_access;
            expire_time = se_info.expire_time;
            user_agent = std::move(se_info.user_agent);
            is_valid = se_info.is_valid;
            connection_fingerprint = std::move(se_info.connection_fingerprint);

            // 将源对象重置为无效状态
            se_info.client_port = 0;
            se_info.create_time = 0;
            se_info.last_access = 0;
            se_info.expire_time = 0;
            se_info.is_valid = false;
        }
        return *this;
    }

    // 更新访问时间
    void update_access_time()
    {
        last_access = time(NULL);
        expire_time = last_access + 1800; // 每次访问重置过期时间
    }

    // 生成连接指纹（用于验证session合法性）
    std::string generate_fingerprint() const
    {
        std::stringstream ss;
        ss << client_ip << ":" << client_port << ":" << user_agent;
        return ss.str();
    }

    // 验证session是否仍然有效
    bool is_expired() const
    {
        return time(NULL) > expire_time;
    }
};