#pragma once

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

#include "../log/log.h"

class UploadFile
{

public:
    UploadFile(char *root, int close_log) : doc_root(root)
    {
        this->m_close_log = close_log;
    };
    UploadFile(const UploadFile &up_file)
    {
        this->doc_root = up_file.doc_root;
    }
    UploadFile &operator=(const UploadFile &up_file)
    {
        this->doc_root = up_file.doc_root;
    }

    UploadFile(UploadFile &&) = default;
    UploadFile &operator=(UploadFile &&) = default;

    ~UploadFile() {}
    // 定义文件长度，读和写缓冲区大小
    static const int FILENAME_LEN = 200;
    static const int READ_BUFFER_SIZE = 2048 * 128;
    static const int WRITE_BUFFER_SIZE = 1024 * 32;
    static const size_t MAX_UPLOAD_SIZE = 10 * 1024 * 1024; // 10MB
    // 上传文件模块
    bool save_uploaded_file(const char *filename, const char *data, size_t len);
    bool is_valid_path(const char *path);
    bool save_uploaded_chunk(const char *filename, const char *data, size_t len,
                             int chunk_num, int total_chunks);
    void cleanup_chunks();
    bool is_all_digits(const char *str);
    bool merge_uploaded_file(const char *filename, int total_chunks);

private:
    char *doc_root;
    int m_close_log;
};