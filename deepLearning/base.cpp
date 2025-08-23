#include "base.h"

Base::Base(std::string imgpath,
           std::string modelpath,
           size_t width,
           size_t height,
           int N,
           int close_log) : imagePath(imgpath),
                            modelPath(modelpath),
                            img_width(width),
                            img_height(height),
                            batch_n(N)
{
    m_close_log = close_log; // 是否关闭日志
}

Base::Base(const Base &base)
{
    this->Image = base.Image.clone();
    this->imagePath = base.imagePath;
    this->modelPath = base.modelPath;
    this->batch_n = base.batch_n;
    this->img_height = base.img_height;
    this->img_width = base.img_width;
}
Base &Base::operator=(const Base &base)
{
    if (this == &base)
    {
        return *this;
    }
    this->imagePath = base.imagePath;
    this->modelPath = base.modelPath;
    this->batch_n = base.batch_n;
    this->img_height = base.img_height;
    this->img_width = base.img_width;
}

void Base::openImage()
{
    // 判断文件是否存在
    if (stat(this->imagePath.c_str(), &this->m_file_stat) < 0)
    {
        LOG_ERROR("%s %d %s", __FILE__, __LINE__, "this image is not exist!");
        return;
    }
    // 检查文件的读写权限
    if (!this->m_file_stat.st_mode & S_IROTH)
    {
        LOG_ERROR("%s %d %s", __FILE__, __LINE__, "this image do not access(read/write)!");
        return;
    }
    // 禁止访问目录
    if (S_ISDIR(this->m_file_stat.st_mode))
    {
        LOG_ERROR("%s %d %s", __FILE__, __LINE__, "this image directory do not access!");
        return;
    }

    // OpenCV读取图像
    this->Image = cv::imread(this->imagePath);

    // 判断图像文件是否打开成功
    if (this->Image.empty())
    {
        LOG_ERROR("%s %d %s", __FILE__, __LINE__, "this image read is failed!");
        return;
    }

    cv::resize(this->Image, this->Image, cv::Size(this->img_width, this->img_height));
}

void Base::openModel()
{
    // 判断文件是否存在
    if (stat(this->modelPath.c_str(), &this->m_file_stat) < 0)
    {
        LOG_ERROR("%s %d %s", __FILE__, __LINE__, "this model is not exist!");
        return;
    }
    // 检查文件的读写权限
    if (!this->m_file_stat.st_mode & S_IROTH)
    {
        LOG_ERROR("%s %d %s", __FILE__, __LINE__, "this model do not access(read/write)!");
        return;
    }
    // 禁止访问目录
    if (S_ISDIR(this->m_file_stat.st_mode))
    {
        LOG_ERROR("%s %d %s", __FILE__, __LINE__, "this model directory do not access!");
        return;
    }

    if (this->modelPath.empty())
    {
        LOG_ERROR("%s %d %s", __FILE__, __LINE__, "this model path is empty!");
        return;
    }
}
cv::Mat Base::createBatch(cv::Mat &img)
{
    // 获取原始图像的维度
    int channels = this->Image.channels();
    int height = this->Image.rows;
    int width = this->Image.cols;

    cv::Mat batch;
    if (this->Image.dims != 3)
    {
        // 重新调整mat的维度：2D [HW] => 3D [CHW]
        batch = this->Image.reshape(1, std::vector<int>{channels, height, width});
        // 重新调整mat维度 3D [CHW] => 4D [NCHW]
        batch = this->Image.reshape(1, std::vector<int>{this->batch_n, channels, height, width});
    }
    else
    {
        // 重新调整mat维度 3D [CHW] => 4D [NCHW]
        batch = this->Image.reshape(1, std::vector<int>{this->batch_n, channels, height, width});
    }

    return batch;
}
cv::Mat Base::normalizeBlob(cv::Mat &inputBlob, cv::Scalar &mean, cv::Scalar &std)
{
    int H = inputBlob.rows;
    int W = inputBlob.cols;
    // 对每个通道进行均值和标准差的归一化
    for (int i = 0; i < H; ++i)
    {
        for (int j = 0; j < W; ++j)
        {
            cv::Vec3f &pixel = inputBlob.at<cv::Vec3f>(i, j);
            pixel[0] = (pixel[0] - mean[0]) / std[0]; // 归一化 R 通道
            pixel[1] = (pixel[1] - mean[1]) / std[1]; // 归一化 G 通道
            pixel[2] = (pixel[2] - mean[2]) / std[2]; // 归一化 B 通道
        }
    }
    return inputBlob;
}

// 获取当前时间（毫秒）
long long Base::get_current_time_ms()
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts); // 使用单调时钟，不受系统时间调整影响
    return ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}
