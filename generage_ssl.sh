#!/bin/bash
# 创建私钥
openssl genrsa -out server.key 2048

# 创建证书签名请求(CSR)
openssl req -new -key server.key -out server.csr -subj "/C=CN/ST=Beijing/L=Beijing/O=MyCompany/OU=IT/CN=localhost"

# 自签名证书（有效期10年）
openssl x509 -req -days 3650 -in server.csr -signkey server.key -out server.crt

# 2. 设置证书权限
chmod 600 server.key
chmod 644 server.crt

# 验证并清理
openssl x509 -in server.crt -text -noout | grep -A 3 -i "subject alternative name"
rm -f ssl.conf
