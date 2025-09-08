
#include <iostream>
#include <memory>
#include <stdexcept>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <string>
#include <string.h>
#include <arpa/inet.h>
#include <chrono>
#include <sstream>
#include <iomanip>

#include "ssl_wrapper.h"
#include "ssl_context.h"

// 获取当前时间字符串
std::string get_current_time()
{
    auto now = std::chrono::system_clock::now();
    auto in_time_t = std::chrono::system_clock::to_time_t(now);

    std::stringstream ss;
    ss << std::put_time(std::localtime(&in_time_t), "%Y-%m-%d %H:%M:%S");
    return ss.str();
}

// 生成漂亮的HTML页面
std::string generate_html_page(const std::string &title, const std::string &content)
{
    return R"(
<!DOCTYPE html>
<html lang="zh-CN">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>)" +
           title + R"(</title>
    <style>
        * {
            margin: 0;
            padding: 0;
            box-sizing: border-box;
        }
        body {
            font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif;
            background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
            min-height: 100vh;
            display: flex;
            justify-content: center;
            align-items: center;
            color: #333;
        }
        .container {
            background: white;
            padding: 2rem;
            border-radius: 15px;
            box-shadow: 0 20px 40px rgba(0,0,0,0.1);
            text-align: center;
            max-width: 600px;
            width: 90%;
        }
        h1 {
            color: #667eea;
            margin-bottom: 1rem;
            font-size: 2.5rem;
        }
        .message {
            font-size: 1.2rem;
            margin: 1.5rem 0;
            line-height: 1.6;
            color: #666;
        }
        .time {
            font-size: 1rem;
            color: #888;
            margin-top: 1rem;
        }
        .status {
            display: inline-block;
            padding: 0.5rem 1rem;
            background: #4CAF50;
            color: white;
            border-radius: 20px;
            font-weight: bold;
            margin: 1rem 0;
        }
        .warning {
            background: #ff9800;
            color: white;
            padding: 1rem;
            border-radius: 8px;
            margin: 1rem 0;
            font-size: 0.9rem;
        }
    </style>
</head>
<body>
    <div class="container">
        <h1>🚀 )" +
           title + R"(</h1>
        <div class="status">HTTPS 服务器运行中</div>
        <div class="message">)" +
           content + R"(</div>
        <div class="warning">
            ⚠️ 注意：这是自签名证书，浏览器可能会显示安全警告。<br>
            请点击"高级" → "继续前往"（不同浏览器提示可能不同）
        </div>
        <div class="time">服务器时间: )" +
           get_current_time() + R"(</div>
    </div>
</body>
</html>
)";
}

int main(int argc, char *argv[])
{
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0)
    {
        throw std::runtime_error("Socket creation failed");
    }

    // 设置socket选项，允许地址重用
    int opt = 1;
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt)))
    {
        perror("setsockopt");
        return -1;
    }

    int port = 443;
    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        perror("Bind failed");
        return -1;
    }

    if (listen(sockfd, 10) < 0)
    {
        perror("Listen failed");
        return -1;
    }

    std::string private_file = "/home/ubuntu/Documents/KTG/myPro/myProject/myTinyWebServer-v2/server.key";
    std::string cert_file = "/home/ubuntu/Documents/KTG/myPro/myProject/myTinyWebServer-v2/server.crt";
    // std::string private_file = "/home/ubuntu/Documents/KTG/myPro/myProject/openssl-client-server/ssl_https/server.key";
    // std::string cert_file = "/home/ubuntu/Documents/KTG/myPro/myProject/openssl-client-server/ssl_https/server.crt";

    OpenSSLContext OpenCtx(cert_file, private_file);
    std::cout << "ssl context is initialized finished!" << std::endl;

    std::cout << "🌐 服务器启动中..." << std::endl;
    std::cout << "📍 本地访问: https://localhost:" << port << std::endl;
    std::cout << "🌍 网络访问: https://<localhost>:" << port << std::endl;
    std::cout << "⏹️  按 Ctrl+C 停止服务器" << std::endl;

    std::string content = generate_html_page("HTTPS 服务器", "成功响应页面");

    while (true)
    {
        sockaddr_in client_addr{};
        socklen_t client_len = sizeof(client_addr);

        int client_sockfd = accept(sockfd, (struct sockaddr *)&client_addr, &client_len);
        if (client_sockfd < 0)
        {
            throw std::runtime_error("Accept failed");
        }

        // 打印客户端IP
        char client_ip[200];
        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip));
        std::cout << "accept fd = " << client_sockfd << "  Client connected: " << client_ip << std::endl;
        try
        {
            SSLWrapper wrapper(client_sockfd, OpenCtx.get());
            if (!wrapper.accept())
            {
                continue;
            }
            std::cout << "ssl handshark is initialized finished!" << std::endl;

            char buf[1024] = {0};
            wrapper.read(buf, sizeof(buf));
            std::cout << "recv: " << buf << std::endl;

            // const char *response = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\n<h1>HTTPS Server</h1>";
            wrapper.write(content.c_str(), content.size());

            wrapper.shutdown();
        }
        catch (const std::exception &e)
        {
            std::cout << e.what() << '\n';
        }
    }

    return 0;
}