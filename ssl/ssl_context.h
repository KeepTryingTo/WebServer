#pragma once

#include <iostream>
#include <algorithm>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <string>
#include <sys/stat.h>

using namespace std;

class OpenSSLContext
{
public:
    OpenSSLContext(const std::string &cert_file, const std::string &private_key)
    {
        SSL_library_init();
        OpenSSL_add_all_algorithms();
        SSL_load_error_strings();

        // 使用TLS方法创建上下文
        // 创建SSL上下文
        ctx = SSL_CTX_new(TLS_server_method());
        if (!ctx)
        {
            throw std::runtime_error("Failed to create SSL context");
        }

        // 配置安全协议和加密套件
        /*
        禁用不安全SSL/TLS协议版本的关键配置
            SSL_OP_NO_SSLv2→ 禁用SSL 2.0（1995年，已彻底不安全）
            SSL_OP_NO_SSLv3→ 禁用SSL 3.0（1996年，存在POODLE攻击漏洞）
            SSL_OP_NO_TLSv1→ 禁用TLS 1.0（1999年，已不满足现代安全要求）
            SSL_OP_NO_TLSv1_1→ 禁用TLS 1.1（2006年，缺乏现代加密特性）
        */
        // SSL_CTX_set_options(ssl_ctx_, SSL_OP_NO_SSLv2 | SSL_OP_NO_SSLv3 |
        //                                   SSL_OP_NO_TLSv1 | SSL_OP_NO_TLSv1_1);
        // 对证书进行权限验证
        if (!validate_key_permissions(private_key))
        {
            throw std::runtime_error("private key not permission!");
        }

        /*
        "HIGH:!aNULL:!MD5:!RC4"是一个加密套件选择字符串，由多个规则组成：
            HIGH：表示允许所有"高"强度加密套件（通常指密钥长度≥128位的现代加密算法）
            !aNULL：排除所有不提供认证的匿名DH套件（防止中间人攻击）
            !MD5：排除使用MD5哈希算法的套件（MD5已被证明不安全）
            !RC4：排除RC4流加密算法（RC4存在严重漏洞）
        */
        SSL_CTX_set_cipher_list(ctx, "HIGH:!aNULL:!MD5:!RC4");
        // 设置更宽松的加密套件
        // SSL_CTX_set_cipher_list(ctx, "ALL:!ADH:!LOW:!EXP:!MD5:@STRENGTH");

        // 设置验证模式和深度
        SSL_CTX_set_verify(ctx, SSL_VERIFY_NONE, nullptr);

        // 在OpenSSLContext构造函数中
        SSL_CTX_set_min_proto_version(ctx, TLS1_2_VERSION); // 最低TLS 1.2
        SSL_CTX_set_max_proto_version(ctx, TLS1_3_VERSION); // 最高TLS 1.3

        // 加载证书和私钥
        if (SSL_CTX_use_certificate_file(ctx, cert_file.c_str(), SSL_FILETYPE_PEM) <= 0)
        {
            ERR_print_errors_fp(stderr);
            throw std::runtime_error("Certificate load error");
        }

        if (SSL_CTX_use_PrivateKey_file(ctx, private_key.c_str(), SSL_FILETYPE_PEM) <= 0)
        {
            ERR_print_errors_fp(stderr);
            throw std::runtime_error("Private key load error");
        }

        if (SSL_CTX_load_verify_locations(ctx, cert_file.c_str(), nullptr) != 1)
        {
            ERR_print_errors_fp(stderr);
            throw std::runtime_error("certificate is validate failed!");
        }

        // 验证私钥
        if (!SSL_CTX_check_private_key(ctx))
        {
            throw std::runtime_error("Private key does not match certificate");
        }
    }

    bool validate_key_permissions(const std::string &key_path)
    {
        struct stat st;
        if (stat(key_path.c_str(), &st) != 0)
        {
            throw std::runtime_error("Cannot access key file");
            return false;
        }

        if ((st.st_mode & 0777) != 0600)
        { // 必须为600
            throw std::runtime_error(
                "Key file must have 600 permissions (current: " +
                std::to_string(st.st_mode & 0777) + ")");
            return false;
        }
        return true;
    }
    SSL_CTX *get() { return ctx; }

    OpenSSLContext(const OpenSSLContext &openCTX)
    {
        this->ctx = openCTX.ctx;
    }
    OpenSSLContext &operator=(const OpenSSLContext &openCTX)
    {
        if (this == &openCTX)
        {
            return *this;
        }
        this->ctx = openCTX.ctx;
        return *this;
    }

    ~OpenSSLContext()
    {
        if (ctx)
            SSL_CTX_free(ctx);
        EVP_cleanup();
    }

private:
    SSL_CTX *ctx;
};