#include <iostream>
#include <vector>
#include <string>
#include <chrono>
#include <zlib.h>
#include <cstring>
#include <brotli/encode.h>
#include <brotli/decode.h>

// 生成测试数据
std::vector<char> generate_test_data(size_t size)
{
    std::vector<char> data(size);
    // 生成包含重复模式的数据
    for (size_t i = 0; i < size; ++i)
    {
        data[i] = static_cast<char>(i % 256);
        if (i > 0 && i % 100 == 0)
        {
            // 每100字节插入一个重复模式
            data[i] = 'X';
            data[i + 1] = 'Y';
            data[i + 2] = 'Z';
        }
    }
    return data;
}

// Zlib (gzip) 压缩
std::vector<uint8_t> zlib_compress(const std::vector<char> &input)
{
    z_stream zs;
    memset(&zs, 0, sizeof(zs));

    // 压缩流的初始化
    if (deflateInit2(&zs, Z_DEFAULT_COMPRESSION, Z_DEFLATED,
                     MAX_WBITS + 16, 8, Z_DEFAULT_STRATEGY) != Z_OK)
    {
        throw std::runtime_error("deflateInit2 failed");
    }

    zs.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(input.data()));
    zs.avail_in = input.size();

    // 估计压缩后大小以及output保存压缩结果
    std::vector<uint8_t> output(deflateBound(&zs, zs.avail_in));
    zs.next_out = output.data();
    zs.avail_out = output.size();

    // 进行压缩
    int ret = deflate(&zs, Z_FINISH);
    if (ret != Z_STREAM_END)
    {
        deflateEnd(&zs);
        throw std::runtime_error("zlib compression failed");
    }

    output.resize(zs.total_out);
    deflateEnd(&zs);
    return output;
}

// Brotli 压缩
std::vector<uint8_t> brotli_compress(const std::vector<char> &input)
{
    // 估计压缩大小
    size_t max_output_size = BrotliEncoderMaxCompressedSize(input.size());
    std::vector<uint8_t> output(max_output_size);

    // 进行压缩
    size_t encoded_size = output.size();
    BROTLI_BOOL result = BrotliEncoderCompress(
        BROTLI_DEFAULT_QUALITY,
        BROTLI_DEFAULT_WINDOW,
        BROTLI_MODE_GENERIC,
        input.size(),
        reinterpret_cast<const uint8_t *>(input.data()),
        &encoded_size,
        output.data());

    if (result != BROTLI_TRUE)
    {
        throw std::runtime_error("Brotli compression failed");
    }

    output.resize(encoded_size);
    return output;
}

// 测试函数
void test_compression(const std::vector<char> &data)
{
    std::cout << "测试数据大小: " << data.size() << " 字节\n";

    // 测试 zlib (gzip)
    auto zlib_start = std::chrono::high_resolution_clock::now();
    auto zlib_compressed = zlib_compress(data);
    auto zlib_end = std::chrono::high_resolution_clock::now();

    std::cout << "Zlib (gzip):\n";
    std::cout << "  压缩后大小: " << zlib_compressed.size() << " 字节\n";
    std::cout << "  压缩比: "
              << (zlib_compressed.size() * 100.0 / data.size()) << "%\n";
    std::cout << "  耗时: "
              << std::chrono::duration_cast<std::chrono::milliseconds>(zlib_end - zlib_start).count()
              << " ms\n";

    // 测试 Brotli
    auto brotli_start = std::chrono::high_resolution_clock::now();
    auto brotli_compressed = brotli_compress(data);
    auto brotli_end = std::chrono::high_resolution_clock::now();

    std::cout << "Brotli:\n";
    std::cout << "  压缩后大小: " << brotli_compressed.size() << " 字节\n";
    std::cout << "  压缩比: "
              << (brotli_compressed.size() * 100.0 / data.size()) << "%\n";
    std::cout << "  耗时: "
              << std::chrono::duration_cast<std::chrono::milliseconds>(brotli_end - brotli_start).count()
              << " ms\n";

    // 比较结果
    std::cout << "Brotli 比 Zlib 节省: "
              << (zlib_compressed.size() - brotli_compressed.size()) << " 字节 ("
              << ((zlib_compressed.size() - brotli_compressed.size()) * 100.0 / zlib_compressed.size())
              << "%)\n";
    std::cout << "----------------------------------------\n";
}

int main()
{
    // 测试不同大小的数据
    std::cout << "=== 压缩算法比较测试 ===\n";

    // 小数据测试 (1KB)
    std::cout << "\n[1KB 数据测试]\n";
    test_compression(generate_test_data(1024));

    // 中等数据测试 (100KB)
    std::cout << "\n[100KB 数据测试]\n";
    test_compression(generate_test_data(1024 * 100));

    // 大数据测试 (1MB)
    std::cout << "\n[1MB 数据测试]\n";
    test_compression(generate_test_data(1024 * 1024));

    // 超大文本测试 (10MB)
    std::cout << "\n[10MB 数据测试]\n";
    test_compression(generate_test_data(1024 * 1024 * 10));

    return 0;
}
/*
=== 压缩算法比较测试 ===

[1KB 数据测试]
测试数据大小: 1024 字节
Zlib (gzip):
  压缩后大小: 340 字节
  压缩比: 33.2031%
  耗时: 0 ms
Brotli:
  压缩后大小: 252 字节
  压缩比: 24.6094%
  耗时: 2 ms
Brotli 比 Zlib 节省: 88 字节 (25.8824%)
----------------------------------------

[100KB 数据测试]
测试数据大小: 102400 字节
Zlib (gzip):
  压缩后大小: 1295 字节
  压缩比: 1.26465%
  耗时: 0 ms
Brotli:
  压缩后大小: 376 字节
  压缩比: 0.367188%
  耗时: 17 ms
Brotli 比 Zlib 节省: 919 字节 (70.9653%)
----------------------------------------

[1MB 数据测试]
测试数据大小: 1048576 字节
Zlib (gzip):
  压缩后大小: 7256 字节
  压缩比: 0.691986%
  耗时: 4 ms
Brotli:
  压缩后大小: 376 字节
  压缩比: 0.0358582%
  耗时: 29 ms
Brotli 比 Zlib 节省: 6880 字节 (94.8181%)
----------------------------------------

[10MB 数据测试]
测试数据大小: 10485760 字节
Zlib (gzip):
  压缩后大小: 66724 字节
  压缩比: 0.63633%
  耗时: 42 ms
Brotli:
  压缩后大小: 389 字节
  压缩比: 0.00370979%
  耗时: 268 ms
Brotli 比 Zlib 节省: 66335 字节 (99.417%)
----------------------------------------
*/