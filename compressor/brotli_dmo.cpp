#include <brotli/decode.h>
#include <brotli/encode.h>
#include <brotli/port.h>
#include <vector>
#include <iostream>
#include <string>
#include <cstring>
#include <iomanip>

// Brotli 解码函数
bool brotli_decompress(const uint8_t *compressed_data, size_t compressed_size,
                       std::vector<uint8_t> &decompressed_data)
{
    // 初始输出缓冲区大小（可以设置为原始数据预估大小或默认值）
    size_t output_size = compressed_size * 4; // 初始猜测
    decompressed_data.resize(output_size);

    BrotliDecoderResult result;
    size_t available_in = compressed_size;
    const uint8_t *next_in = compressed_data;
    size_t available_out = output_size;
    uint8_t *next_out = decompressed_data.data();

    result = BrotliDecoderDecompressStream(
        BrotliDecoderCreateInstance(nullptr, nullptr, nullptr),
        &available_in, &next_in,
        &available_out, &next_out,
        nullptr);

    if (result == BROTLI_DECODER_RESULT_ERROR)
    {
        return false;
    }

    // 调整到实际解压后大小
    size_t total_out = output_size - available_out;
    decompressed_data.resize(total_out);

    return true;
}

// 测试压缩和解压的完整流程
void test_roundtrip(const std::string &input)
{
    // 压缩阶段
    size_t input_size = input.size();
    size_t max_output_size = BrotliEncoderMaxCompressedSize(input_size);
    std::vector<uint8_t> compressed(max_output_size);
    size_t encoded_size = max_output_size;

    BrotliEncoderCompress(
        BROTLI_DEFAULT_QUALITY,
        BROTLI_DEFAULT_WINDOW,
        BROTLI_MODE_TEXT,
        input_size,
        reinterpret_cast<const uint8_t *>(input.data()),
        &encoded_size,
        compressed.data());

    compressed.resize(encoded_size);

    // 解压阶段
    std::vector<uint8_t> decompressed;
    if (!brotli_decompress(compressed.data(), compressed.size(), decompressed))
    {
        std::cerr << "Decompression failed!\n";
        return;
    }

    // 验证结果
    bool success = (decompressed.size() == input_size) &&
                   (memcmp(decompressed.data(), input.data(), input_size) == 0);

    std::cout << "Roundtrip test: " << (success ? "SUCCESS" : "FAILED") << "\n";
    std::cout << "Original: " << input_size << " bytes\n";
    std::cout << "Compressed: " << encoded_size << " bytes\n";
    std::cout << "Decompressed: " << decompressed.size() << " bytes\n";
    std::cout << "Ratio: " << (encoded_size * 100.0 / input_size) << "%\n";
    std::cout << "------------------------\n";

    // 打印解码后的数据
    std::cout << "Decoded data: \"";
    // 安全地打印数据，处理可能的非打印字符
    for (size_t i = 0; i < decompressed.size(); ++i)
    {
        uint8_t c = decompressed[i];
        if (c >= 32 && c <= 126)
        { // 可打印ASCII字符
            std::cout << c;
        }
        else
        {
            // 以16进制形式显示非打印字符
            std::cout << "\\x" << std::hex << std::setw(2) << std::setfill('0')
                      << static_cast<int>(c) << std::dec;
        }
    }
    std::cout << "\"\n";
    std::cout << "------------------------\n";
}

int main()
{
    // 测试不同大小的数据
    test_roundtrip("A");
    test_roundtrip("Hello");
    test_roundtrip("Hello, World!");
    test_roundtrip("Hello, Brotli compression test!");
    test_roundtrip(std::string(100, 'A'));
    test_roundtrip(std::string(1000, 'A'));

    // 测试包含重复模式的数据
    test_roundtrip("ABABABABABABABABABABABABABABABAB");
    test_roundtrip("This is a test. This is a test. This is a test.");

    // 测试随机数据
    test_roundtrip(std::string("Random\0Data\0With\0Nulls", 20));

    return 0;
}
