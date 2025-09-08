#include "ssl_wrapper.h"
#include <stdexcept>

// 在SSL握手前检查连接是否仍然活跃
bool SSLWrapper::is_connection_alive(int sockfd)
{
    struct timeval timeout = {1, 0}; // 1秒超时
    fd_set read_fds;
    // 清空文件描述符
    FD_ZERO(&read_fds);
    // 将一个特定的文件描述符 sockfd 加入到集合 read_fds 中
    FD_SET(sockfd, &read_fds);

    // 检查是否有数据可读或错误；
    // 调用 select 函数来同步监视多个文件描述符的状态变化。这里它被用来监视一个 socket 是否可读，并带有超时限制。
    int result = select(sockfd + 1, &read_fds, nullptr, nullptr, &timeout);
    if (result < 0)
        return false; // 错误
    if (result == 0)
        return true; // 超时，连接仍存在

    // 有数据可读，检查是否是FIN包
    char buf[1];
    if (recv(sockfd, buf, 1, MSG_PEEK | MSG_DONTWAIT) == 0)
    {
        return false; // 收到FIN
    }
    return true;
}

SSLWrapper::SSLWrapper(int sockfd, SSL_CTX *ctx) : sockfd_(sockfd), ctx_(ctx), ssl_(nullptr)
{

    // 检查连接是否仍然有效
    if (!is_connection_alive(sockfd))
    {
        throw std::runtime_error("Connection already closed by client");
    }
    // 初始化SSL库，加载私钥和证书
    if (!ctx_)
    {
        throw std::runtime_error("SSL library not initialized");
    }
    ssl_ = SSL_new(ctx_);
    if (!ssl_)
    {
        throw_ssl_error("Failed to create SSL object");
    }
    SSL_set_fd(ssl_, sockfd_);
}

SSLWrapper::~SSLWrapper()
{
    if (ssl_)
    {
        SSL_free(ssl_);
    }
}

void SSLWrapper::print_detailed_ssl_errors(SSL *ssl, int ret)
{
    int err = SSL_get_error(ssl, ret);

    if (err == SSL_ERROR_ZERO_RETURN)
    {
        std::cerr << "SSL Error Code: " << err << " SSL has been shutdown" << std::endl;
    }
    else if (err == SSL_ERROR_SYSCALL)
    {
        // 打印系统错误码
        std::cerr << "系统错误码: " << errno << std::endl;
        std::cerr << "系统错误描述: " << strerror(errno) << std::endl;
    }
    else
    {
        std::cerr << "SSL Error Code: " << err << " Connection has been aborted" << std::endl;
    }

    // OpenSSL内部错误
    unsigned long error;
    char err_buf[256];
    while ((error = ERR_get_error()) != 0)
    {
        ERR_error_string_n(error, err_buf, sizeof(err_buf));
        std::cerr << "OpenSSL Error: " << err_buf << std::endl;
    }
}

bool SSLWrapper::accept()
{
    // 再次检查连接状态
    if (!is_connection_alive(sockfd_))
    {
        return false;
    }
    // 预检查套接字状态
    int socket_error = 0;
    socklen_t error_len = sizeof(socket_error);
    if (getsockopt(sockfd_, SOL_SOCKET, SO_ERROR, &socket_error, &error_len) < 0)
    {
        std::cerr << __FILE__ << " " << __LINE__ << " " << "getsockopt error: " << strerror(errno) << std::endl;
        return false;
    }

    if (socket_error != 0)
    {
        std::cerr << __FILE__ << " " << __LINE__ << " " << "Socket error before SSL_accept: "
                  << strerror(socket_error) << std::endl;
        return false;
    }

    try
    {
        std::cout << __FILE__ << " " << __LINE__ << " " << "SSL state: " << SSL_state_string_long(ssl_) << std::endl;
        int count = 3;
        while (count--)
        {
            int ret = SSL_accept(ssl_);
            if (ret > 0)
            {
                std::cout << "握手成功 - 最终状态: " << SSL_state_string_long(ssl_) << "\n"
                          << "协商协议: " << SSL_get_version(ssl_) << "\n"
                          << "使用加密套件: " << SSL_get_cipher(ssl_) << "\n"
                          << "证书验证结果: " << SSL_get_verify_result(ssl_) << std::endl;

                return true;
            }
            if (ret <= 0)
            {
                int ssl_err = SSL_get_error(ssl_, ret);
                if (ssl_err == SSL_ERROR_WANT_READ || ssl_err == SSL_ERROR_WANT_WRITE)
                {
                    continue; // 非致命错误，继续重试
                }

                // 详细错误处理
                switch (ssl_err)
                {
                case SSL_ERROR_WANT_READ:
                    std::cerr << __FILE__ << " " << __LINE__ << " " << "SSL需要更多读取数据" << std::endl;
                    break;
                case SSL_ERROR_WANT_WRITE:
                    std::cerr << __FILE__ << " " << __LINE__ << " " << "SSL需要更多写入数据" << std::endl;
                    break;
                default:
                    print_detailed_ssl_errors(ssl_, ret);
                    break;
                    // return false;
                }
            }
        }
    }
    catch (const std::exception &e)
    {
        std::cerr << __FILE__ << " " << __LINE__ << " " << "SSL接受异常: " << e.what() << std::endl;
        return false;
    }
    return false;
}

int SSLWrapper::read(void *buf, size_t len)
{
    int bytes = SSL_read(ssl_, buf, len);
    if (bytes <= 0)
    {
        int err = SSL_get_error(ssl_, bytes);
        if (err == SSL_ERROR_ZERO_RETURN || err == SSL_ERROR_SYSCALL)
        {
            return 0; // 连接关闭
        }
        throw_ssl_error("SSL read error");
    }
    printf("%s %d  recv data is successfully %s\n", __FILE__, __LINE__, buf);
    return bytes;
}

// 将SSL加密后的数据从writeBio刷新到socket
void SSLWrapper::flushSslOut()
{
    char buf[4096];
    int pending;

    // 检查BIO中是否有待发送的加密数据
    while ((pending = BIO_pending(writeBio_)) > 0)
    {
        int bytes = BIO_read(writeBio_, buf, std::min(pending, static_cast<int>(sizeof(buf))));
        if (bytes > 0)
        {
            // 实际发送到网络（需处理EAGAIN等错误）
            int sent = ::send(sockfd_, buf, bytes, 0);
            if (sent <= 0)
            {
                if (errno == EAGAIN || errno == EWOULDBLOCK)
                {
                    // 将未发送的数据重新放回BIO（重要！）
                    BIO_write(writeBio_, buf + sent, bytes - sent);
                    break;
                }
                throw std::runtime_error("Socket send failed");
            }
        }
    }
}

int SSLWrapper::write(const struct iovec *iov, int iovcnt)
{
    ssize_t totalWritten = 0;

    for (int i = 0; i < iovcnt; ++i)
    {
        const auto &vec = iov[i];
        if (vec.iov_len == 0)
            continue;

        // 将当前vec[i]数据写入缓冲区
        int written = 0;
        do
        {
            written = SSL_write(ssl_,
                                static_cast<char *>(vec.iov_base) + written,
                                vec.iov_len - written);

            if (written <= 0)
            {
                int err = SSL_get_error(ssl_, written);
                if (err == SSL_ERROR_WANT_WRITE)
                {
                    // 确保所有加密数据已发送
                    flushSslOut();
                    continue;
                }
                throw_ssl_error("SSL write error");
            }

            totalWritten += written;
        } while (written < static_cast<int>(vec.iov_len));
    }

    // 确保所有加密数据已发送到网络
    flushSslOut();
    return totalWritten;
}

void SSLWrapper::shutdown()
{
    SSL_shutdown(ssl_);
}

std::string SSLWrapper::get_last_error() const
{
    char buf[256];
    ERR_error_string_n(ERR_get_error(), buf, sizeof(buf));
    return std::string(buf);
}

void SSLWrapper::throw_ssl_error(const std::string &msg) const
{
    throw std::runtime_error(msg + ": " + get_last_error());
}