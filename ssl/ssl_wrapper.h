#ifndef SSL_WRAPPER_H
#define SSL_WRAPPER_H

#include <openssl/ssl.h>
#include <openssl/err.h>
#include <string>
#include <sys/stat.h>
#include <iostream>
#include <sys/errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <sys/fcntl.h>
#include <cstring>
#include <sys/uio.h>

class SSLWrapper
{
public:
    SSLWrapper(int sockfd, SSL_CTX *ctx);
    ~SSLWrapper();

    // 在SSL握手前检查连接是否仍然活跃
    bool is_connection_alive(int sockfd);

    // SSL操作封装
    bool accept();
    int read(void *buf, size_t len);
    void flushSslOut();
    int write(const struct iovec *iov, int iovcnt);
    void shutdown();

    void setCtx(SSL_CTX *ctx)
    {
        // 初始化SSL库，加载私钥和证书
        if (!ctx)
        {
            throw std::runtime_error("SSL library not initialized");
        }
        this->ctx_ = ctx;
    }
    void setSSL(SSL *ssl)
    {
        this->ssl_ = ssl;
    }
    void setSockfd(int sockfd)
    {
        this->sockfd_ = sockfd;
    }
    SSL *getSSL()
    {
        return this->ssl_;
    }
    SSL_CTX *getSSLContext()
    {
        return this->ctx_;
    }

    // 错误处理
    std::string get_last_error() const;
    void print_detailed_ssl_errors(SSL *ssl, int ret);

    // 禁用拷贝和赋值
    SSLWrapper(const SSLWrapper &) = delete;
    SSLWrapper &operator=(const SSLWrapper &) = delete;

private:
    SSL_CTX *ctx_;
    SSL *ssl_;
    int sockfd_;
    BIO *writeBio_; // 用于存储待发送的加密数据

    void throw_ssl_error(const std::string &msg) const;
};

#endif // SSL_WRAPPER_H