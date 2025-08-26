#include "upload_file.h"

// 新增函数实现
bool UploadFile::is_valid_path(const char *path)
{
    return strstr(path, "../") == nullptr &&
           strstr(path, "..\\") == nullptr &&
           strstr(path, "%2e%2e") == nullptr;
}

bool UploadFile::save_uploaded_file(const char *filename, const char *data, size_t len)
{
    char dir_path[FILENAME_LEN];
    strcpy(dir_path, doc_root);
    strcat(dir_path, "/uploads");

    // 创建上传目录（如果不存在）
    struct stat st;
    if (stat(dir_path, &st))
    {
        mkdir(dir_path, 0755);
    }

    char full_path[FILENAME_LEN];
    snprintf(full_path, sizeof(full_path), "%s/%s", dir_path, filename);

    printf("upload file: %s\n", full_path);

    int fd = open(full_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0)
        return false;

    ssize_t written = ::write(fd, data, len);
    close(fd);
    return written == static_cast<ssize_t>(len);
}

/**
 * 保存上传的文件分块
 * @param filename 原始文件名
 * @param data 分块数据
 * @param len 分块长度
 * @param chunk_num 分块序号
 * @param total_chunks 总分块数
 * @return 是否成功
 */
bool UploadFile::save_uploaded_chunk(const char *filename, const char *data, size_t len,
                                     int chunk_num, int total_chunks)
{
    // 创建临时目录存放分块
    char chunk_dir[FILENAME_LEN];
    // printf("doc root: %s\n", doc_root);
    snprintf(chunk_dir, sizeof(chunk_dir), "%s/uploads_chunks", doc_root);

    // 确保临时目录存在
    struct stat st;
    if (stat(chunk_dir, &st))
    {
        if (mkdir(chunk_dir, 0755))
        {
            LOG_ERROR("Cannot create chunk directory: %s", chunk_dir);
            return false;
        }
    }

    // 生成分块临时文件名
    char chunk_path[FILENAME_LEN];
    snprintf(chunk_path, sizeof(chunk_path), "%s/%s.part%d", chunk_dir, filename, chunk_num);

    // 写入分块文件
    FILE *fp = fopen(chunk_path, "wb");
    if (!fp)
    {
        LOG_ERROR("Cannot open chunk file %s: %s", chunk_path, strerror(errno));
        return false;
    }

    size_t written = fwrite(data, 1, len, fp);
    fclose(fp);

    if (written != len)
    {
        LOG_ERROR("Incomplete write to chunk file %s: %zu/%zu", chunk_path, written, len);
        return false;
    }

    LOG_INFO("Saved chunk %d/%d of %s (%zu bytes)", chunk_num + 1, total_chunks, filename, len);
    return true;
}

/**
 * 合并所有分块为完整文件
 * @param filename 原始文件名
 * @param total_chunks 总分块数
 * @return 是否成功
 */
bool UploadFile::merge_uploaded_file(const char *filename, int total_chunks)
{
    // 准备最终文件路径
    char final_path[FILENAME_LEN];
    snprintf(final_path, sizeof(final_path), "%s/uploads/%s", doc_root, filename);

    // 创建上传目录（如果不存在）
    char upload_dir[FILENAME_LEN];
    snprintf(upload_dir, sizeof(upload_dir), "%s/uploads", doc_root);

    struct stat st;
    if (stat(upload_dir, &st))
    {
        if (mkdir(upload_dir, 0755))
        {
            LOG_ERROR("Cannot create upload directory: %s", upload_dir);
            return false;
        }
    }

    // 打开最终文件
    FILE *final_fp = fopen(final_path, "wb");
    if (!final_fp)
    {
        LOG_ERROR("Cannot open final file %s: %s", final_path, strerror(errno));
        return false;
    }

    // 分块临时目录
    char chunk_dir[FILENAME_LEN];
    snprintf(chunk_dir, sizeof(chunk_dir), "%s/uploads_chunks", doc_root);

    // 合并所有分块
    bool success = true;
    char chunk_path[FILENAME_LEN];
    char buffer[65536]; // 64KB缓冲区

    // 读取所有的分块文件然后进行合并
    for (int i = 0; i < total_chunks; i++)
    {
        // 当前索引文件
        snprintf(chunk_path, sizeof(chunk_path), "%s/%s.part%d", chunk_dir, filename, i);

        FILE *chunk_fp = fopen(chunk_path, "rb");
        if (!chunk_fp)
        {
            LOG_ERROR("Cannot open chunk file %s: %s", chunk_path, strerror(errno));
            success = false;
            break;
        }

        // 读取并写入分块内容
        size_t bytes_read;
        while ((bytes_read = fread(buffer, 1, sizeof(buffer), chunk_fp)))
        {
            size_t bytes_written = fwrite(buffer, 1, bytes_read, final_fp);
            if (bytes_written != bytes_read)
            {
                LOG_ERROR("Write failed for chunk %d", i);
                success = false;
                break;
            }
        }

        fclose(chunk_fp);

        // 删除已合并的分块
        if (unlink(chunk_path))
        {
            LOG_WARN("Cannot delete chunk file %s: %s", chunk_path, strerror(errno));
        }

        if (!success)
            break;
    }

    fclose(final_fp);

    // 如果合并失败，删除不完整的最终文件
    if (!success)
    {
        unlink(final_path);
        return false;
    }

    LOG_INFO("Successfully merged %d chunks into %s", total_chunks, final_path);
    return true;
}

/**
 * 清理指定文件的所有分块
 * @param filename 原始文件名
 */
void UploadFile::cleanup_chunks()
{
    char chunk_dir[FILENAME_LEN];
    snprintf(chunk_dir, sizeof(chunk_dir), "%s/uploads_chunks", doc_root);

    DIR *dir = opendir(chunk_dir);
    if (!dir)
    {
        LOG_WARN("Cannot open chunk directory: %s", chunk_dir);
        return;
    }

    struct dirent *entry;
    // 遍历当前目录下的文件
    while ((entry = readdir(dir)) != NULL)
    {
        const char *name = entry->d_name;
        // printf("entry -> d_name = %s\n", name);
        size_t len = strlen(name);

        // 检查文件名格式：<原始文件名>.part<数字>
        // 例如：filename.png.part0
        const char *part_ptr = strstr(name, ".part");
        if (part_ptr && part_ptr > name)
        { // 确保.part前面有内容
            // 验证.part后是否为纯数字
            const char *num_ptr = part_ptr + 5; // 跳过".part"
            if (is_all_digits(num_ptr))
            {
                char chunk_path[FILENAME_LEN];
                snprintf(chunk_path, sizeof(chunk_path), "%s/%s", chunk_dir, name);

                if (unlink(chunk_path) != 0)
                {
                    LOG_WARN("Failed to delete chunk file: %s (%s)",
                             chunk_path, strerror(errno));
                }
                else
                {
                    LOG_INFO("Deleted chunk file: %s", chunk_path);
                }
            }
        }
    }

    closedir(dir);
}

// 辅助函数：检查字符串是否全为数字
bool UploadFile::is_all_digits(const char *str)
{
    if (!str || *str == '\0')
        return false;

    while (*str)
    {
        if (!isdigit(*str))
            return false;
        str++;
    }
    return true;
}