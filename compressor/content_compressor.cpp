#include "content_compressor.h"
#include <cstring>
#include <set>

// 支持的压缩格式
const std::set<std::string> ContentCompressor::compressible_types_ = {
    "html", "htm", "css", "js", "json", "xml", "txt", "svg", "png"};

ContentCompressor::ContentCompressor()
    : current_encoding_(NONE)
{
    compressed_data_.reserve(1024 * 16); // 预分配16KB缓冲区
}

ContentCompressor::~ContentCompressor()
{
    reset();
}

bool ContentCompressor::should_compress(const std::string &file_extension,
                                        const std::string &accept_encoding) const
{
    // 已经压缩过的不再压缩
    if (current_encoding_ != NONE)
    {
        return false;
    }

    // 检查客户端是否支持压缩
    if (accept_encoding.empty())
    {
        return false;
    }

    // 只压缩文本类型和常见Web资源
    return compressible_types_.find(file_extension) != compressible_types_.end();
}

bool ContentCompressor::compress(const std::string &content, EncodingType encoding_type)
{
    return compress(content.data(), content.size(), encoding_type);
}

bool ContentCompressor::compress(const char *data, size_t length, EncodingType encoding_type)
{
    reset();
    // 根据客户端能接收的压缩格式选择指定的函数进行压缩
    switch (encoding_type)
    {
    case GZIP:
        if (gzip_compress(data, length))
        {
            current_encoding_ = GZIP;
            return true;
        }
        break;
    case DEFLATE:
        if (deflate_compress(data, length))
        {
            current_encoding_ = DEFLATE;
            return true;
        }
        break;
    case BROTLI:
        if (brotli_compress(data, length))
        {
            current_encoding_ = BROTLI;
            return true;
        }
        break;
    case NONE:
    default:
        break;
    }

    return false;
}

bool ContentCompressor::gzip_compress(const char *data, size_t length)
{
    // 定义一个 z_stream结构体，这是 zlib 库用于管理压缩状态的核心数据结构
    z_stream zs;
    memset(&zs, 0, sizeof(zs));

    // 初始化压缩流，MAX_WBITS + 16​​: 窗口大小位数为 MAX_WBITS(15)，+16 表示使用 gzip 头部和尾部格式
    if (deflateInit2(&zs, Z_DEFAULT_COMPRESSION, Z_DEFLATED,
                     MAX_WBITS + 16, 8, Z_DEFAULT_STRATEGY) != Z_OK)
    {
        return false;
    }

    // 设置输入数据的缓冲区以及数据长度
    zs.next_in = (Bytef *)data;
    zs.avail_in = length;

    int ret;
    char outbuffer[32768]; // 32KB临时缓冲区
    do
    {
        // 指向输出缓冲区的指针和输出缓冲区剩余空间大小
        zs.next_out = (Bytef *)outbuffer;
        zs.avail_out = sizeof(outbuffer);

        // 进行压缩过程
        ret = deflate(&zs, Z_FINISH);
        if (ret == Z_STREAM_ERROR)
        {
            deflateEnd(&zs);
            return false;
        }

        // 处理输出压缩
        size_t have = sizeof(outbuffer) - zs.avail_out;
        compressed_data_.insert(compressed_data_.end(), outbuffer, outbuffer + have);
    } while (zs.avail_out == 0); // 输出缓冲区已满，需要继续压缩

    // 清除压缩流
    deflateEnd(&zs);
    return true;
}

bool ContentCompressor::deflate_compress(const char *data, size_t length)
{
    z_stream zs;
    memset(&zs, 0, sizeof(zs));

    if (deflateInit(&zs, Z_DEFAULT_COMPRESSION) != Z_OK)
    {
        return false;
    }

    zs.next_in = (Bytef *)data;
    zs.avail_in = length;

    int ret;
    char outbuffer[32768]; // 32KB临时缓冲区
    do
    {
        zs.next_out = (Bytef *)outbuffer;
        zs.avail_out = sizeof(outbuffer);

        ret = deflate(&zs, Z_FINISH);
        if (ret == Z_STREAM_ERROR)
        {
            deflateEnd(&zs);
            return false;
        }

        size_t have = sizeof(outbuffer) - zs.avail_out;
        compressed_data_.insert(compressed_data_.end(), outbuffer, outbuffer + have);
    } while (zs.avail_out == 0);

    deflateEnd(&zs);
    return true;
}

bool ContentCompressor::brotli_compress(const char *data, size_t length)
{
    // 估算最大压缩后大小
    size_t max_compressed_size = BrotliEncoderMaxCompressedSize(length);
    compressed_data_.resize(max_compressed_size);

    // 执行压缩
    size_t encoded_size = max_compressed_size;
    BROTLI_BOOL result = BrotliEncoderCompress(
        BROTLI_DEFAULT_QUALITY, // 压缩质量 (0-11)
        BROTLI_DEFAULT_WINDOW,  // 窗口大小 (10-24)
        BROTLI_DEFAULT_MODE,    // 压缩模式
        length,
        reinterpret_cast<const uint8_t *>(data),
        &encoded_size,
        reinterpret_cast<uint8_t *>(compressed_data_.data()));

    if (result != BROTLI_TRUE)
    {
        compressed_data_.clear();
        return false;
    }

    // 调整到实际压缩后大小
    compressed_data_.resize(encoded_size);
    return true;
}

std::string ContentCompressor::content_encoding_header() const
{
    switch (current_encoding_)
    {
    case GZIP:
        return "Content-Encoding: gzip\r\n";
    case DEFLATE:
        return "Content-Encoding: deflate\r\n";
    case BROTLI:
        return "Content-Encoding: br\r\n";
    case NONE:
    default:
        return "";
    }
}

void ContentCompressor::reset()
{
    compressed_data_.clear();
    current_encoding_ = NONE;
}