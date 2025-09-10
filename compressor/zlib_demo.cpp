#include <iostream>
#include <string>
#include <cstring>
#include <zlib.h>

int main()
{
    // 原始数据
    const char *original_str = "Hello, World! This is a test string to demonstrate zlib compression and decompression. Hello, World! This is a test string to demonstrate zlib compression and decompression.";
    uLong original_size = strlen(original_str) + 1; // +1 是为了包含字符串结束符 '\0'

    // 计算压缩后的预计大小，并分配内存
    uLong compressed_size = compressBound(original_size);
    Bytef *compressed_data = new Bytef[compressed_size];

    // 进行压缩
    int compress_result = compress(compressed_data, &compressed_size,
                                   reinterpret_cast<const Bytef *>(original_str), original_size);

    if (compress_result != Z_OK)
    {
        std::cerr << "Compression failed with error code: " << compress_result << std::endl;
        delete[] compressed_data;
        return 1;
    }

    std::cout << "[Compression Success]" << std::endl;
    std::cout << "Original size: " << original_size << " bytes" << std::endl;
    std::cout << "Compressed size: " << compressed_size << " bytes" << std::endl;
    std::cout << "Compression ratio: " << (100.0 * compressed_size / original_size) << "%" << std::endl;

    // 分配内存用于解压后的数据,知道原始大小，所以直接分配 original_size
    Bytef *decompressed_data = new Bytef[original_size];
    // 这个变量在调用 uncompress 后会被设置为解压后的实际大小
    uLong decompressed_size = original_size;

    // 进行解压
    int decompress_result = uncompress(decompressed_data, &decompressed_size,
                                       compressed_data, compressed_size);

    if (decompress_result != Z_OK)
    {
        std::cerr << "Decompression failed with error code: " << decompress_result << std::endl;
        delete[] compressed_data;
        delete[] decompressed_data;
        return 1;
    }

    // 验证解压后的数据是否与原始数据一致
    if (decompressed_size != original_size ||
        memcmp(original_str, decompressed_data, original_size) != 0)
    {
        std::cerr << "Decompressed data does NOT match the original!" << std::endl;
    }
    else
    {
        std::cout << "[Decompression Success]" << std::endl;
        std::cout << "Decompressed data: " << decompressed_data << std::endl;
    }

    // 清理内存
    delete[] compressed_data;
    delete[] decompressed_data;

    return 0;
}