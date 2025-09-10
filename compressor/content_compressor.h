#ifndef CONTENT_COMPRESSOR_H
#define CONTENT_COMPRESSOR_H

#include <string>
#include <vector>
#include <zlib.h>
#include <set>
#include <brotli/encode.h>
#include <brotli/decode.h>

class ContentCompressor
{
public:
    enum EncodingType
    {
        GZIP,
        DEFLATE,
        BROTLI, // 新增 Brotli 类型
        NONE
    };

    ContentCompressor();
    ~ContentCompressor();

    // 通过检查文件的后缀名来判断是否应该压缩
    bool should_compress(const std::string &file_extension,
                         const std::string &accept_encoding) const;

    // 执行压缩
    bool compress(const std::string &content, EncodingType encoding_type);
    bool compress(const char *data, size_t length, EncodingType encoding_type);

    // 获取压缩后的数据
    const std::vector<char> &compressed_data() const { return compressed_data_; }

    // 获取内容编码头
    std::string content_encoding_header() const;

    // 获取压缩后的数据大小
    size_t compressed_size() const { return compressed_data_.size(); }

    // 重置压缩器状态
    void reset();

private:
    std::vector<char> compressed_data_;
    EncodingType current_encoding_;

    // 内部压缩实现
    bool gzip_compress(const char *data, size_t length);
    bool deflate_compress(const char *data, size_t length);
    // brotli压缩
    bool brotli_compress(const char *data, size_t length);

    // 支持的压缩类型
    static const std::set<std::string> compressible_types_;
};

#endif // CONTENT_COMPRESSOR_H