#pragma

#include "session_info.h"
#include <stdio.h>
#include <map>
#include <array>
#include <vector>
#include <condition_variable>
#include <mutex>
#include <iostream>

class SessionManager
{
private:
    std::map<std::string, enhanced_session_info> sessions_;
    std::mutex mutx_;
    std::map<std::string, std::string> user_to_session_; // 用户到session的映射

    // 私有默认构造函数
    SessionManager() = default;

public:
    static SessionManager &instance()
    {
        static SessionManager instance;
        return instance;
    }

    // 实现的是单例模式，因此禁止掉拷贝构造和赋值运算操作
    SessionManager(const SessionManager &session) = delete;
    SessionManager &operator=(const SessionManager &session) = delete;

    // 移动构造函数（正确实现）
    SessionManager(SessionManager &&other) noexcept
        : sessions_(std::move(other.sessions_)),
          user_to_session_(std::move(other.user_to_session_))
    {
        // 锁不需要移动，新对象会创建自己的锁
    }

    // 移动赋值运算符
    SessionManager &operator=(SessionManager &&other) noexcept
    {
        if (this != &other)
        {
            sessions_ = std::move(other.sessions_);
            user_to_session_ = std::move(other.user_to_session_);
            // 锁不需要移动
        }
        return *this;
    }

    // 创建新session
    std::string create_session(const std::string &username,
                               const std::string &client_ip,
                               int client_port,
                               const std::string &user_agent = "")
    {
        std::string session_id = generate_secure_session_id();
        {
            unique_lock<std::mutex> lock(mutx_);
            // 检查用户是否已有活跃session
            auto user_it = user_to_session_.find(username);
            if (user_it != user_to_session_.end())
            {
                // 使旧session失效
                sessions_.erase(user_it->second);
                user_to_session_.erase(user_it);
            }

            // 创建新session
            enhanced_session_info session(session_id, username, client_ip,
                                          client_port, user_agent);
            // 根据用户浏览器信息 + 客户端ip + 客户端端口生成指纹信息
            session.connection_fingerprint = session.generate_fingerprint();

            sessions_[session_id] = session;
            user_to_session_[username] = session_id;
        }
        printf("Created session for user: %s, IP: %s, SessionID: %s",
               username.c_str(), client_ip.c_str(), session_id.c_str());

        return session_id;
    }

    // 验证session
    bool validate_session(const std::string &session_id,
                          const std::string &client_ip = "",
                          const std::string &user_agent = "")
    {

        unique_lock<std::mutex> lock(mutx_);

        auto it = sessions_.find(session_id);
        if (it == sessions_.end())
        {
            printf("Session not found: %s", session_id.c_str());
            return false;
        }

        enhanced_session_info &session = it->second;

        // 检查session是否过期;过期就从记录中删除
        if (session.is_expired())
        {
            sessions_.erase(it);
            user_to_session_.erase(session.username);
            printf("Session expired: %s", session_id.c_str());
            return false;
        }

        // 可选：验证连接指纹（增强安全性）
        if (!client_ip.empty() && !user_agent.empty())
        {
            std::string current_fingerprint = client_ip + ":" + user_agent;
            std::string session_fingerprint = session.client_ip + ":" + session.user_agent;

            // 如果指纹不匹配，可能是session被盗用
            if (current_fingerprint != session_fingerprint)
            {
                printf("Session fingerprint mismatch for: %s", session_id.c_str());
                // 可以根据安全策略决定是否使session失效
                sessions_.erase(it);
                user_to_session_.erase(session.username);
            }
        }

        // 更新访问时间
        session.update_access_time();

        return true;
    }

    // 获取session信息
    enhanced_session_info get_session(const std::string &session_id)
    {
        unique_lock<std::mutex> lock(mutx_);
        auto it = sessions_.find(session_id);
        if (it != sessions_.end())
        {
            enhanced_session_info session = it->second;
            return session;
        }

        return enhanced_session_info(); // 返回无效session
    }

    // 销毁session
    void destroy_session(const std::string &session_id)
    {
        unique_lock<std::mutex> lock(mutx_);

        auto it = sessions_.find(session_id);
        if (it != sessions_.end())
        {
            std::string username = it->second.username;
            sessions_.erase(it);
            user_to_session_.erase(username);

            printf("Destroyed session: %s for user: %s",
                   session_id.c_str(), username.c_str());
        }
    }

    // 清理过期session
    void cleanup_expired_sessions()
    {
        unique_lock<std::mutex> lock(mutx_);

        auto it = sessions_.begin();
        while (it != sessions_.end())
        {
            if (it->second.is_expired())
            {
                std::string username = it->second.username;
                user_to_session_.erase(username);
                it = sessions_.erase(it);
                printf("Cleaned expired session for user: %s", username.c_str());
            }
            else
            {
                ++it;
            }
        }
    }

    // 获取用户活跃session数量
    size_t get_active_session_count()
    {
        unique_lock<std::mutex> lock(mutx_);
        size_t count = sessions_.size();

        return count;
    }

private:
    /**
     * @brief 生成安全的随机Session ID
     *
     * 使用密码学安全的随机数生成器创建16字节随机数，
     * 并将其转换为32字符的十六进制字符串格式
     *
     * @return std::string 32字符的十六进制Session ID（如"a1b2c3d4e5f6..."）
     *
     * @note 实现细节：
     * 1. 使用std::random_device获取硬件熵源（如果可用）
     * 2. 生成16字节（128位）随机数，满足密码学强度要求
     * 3. 转换为HEX格式避免二进制数据在传输中出现问题
     * 4. 输出示例：将字节{0xAB,0xCD...}转换为字符串"abcd..."
     */
    // 生成安全的session ID（使用您现有的实现）
    std::string generate_secure_session_id()
    {
        std::array<unsigned char, 16> bytes{};
        std::random_device rd;
        for (auto &b : bytes)
        {
            b = static_cast<unsigned char>(rd() & 0xFF);
        }

        static const char *hex_chars = "0123456789abcdef";
        std::string session_id;
        session_id.resize(32);
        for (size_t i = 0; i < bytes.size(); i++)
        {
            session_id[i * 2] = hex_chars[bytes[i] >> 4];
            session_id[i * 2 + 1] = hex_chars[bytes[i] & 0x0F];
        }
        return session_id;
    }
};