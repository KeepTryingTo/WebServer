#include "segmentation.h"

Segmentation::Segmentation(std::string imgpath,
                           std::string modelpath,
                           size_t width,
                           size_t height,
                           int N,
                           int close_log)
    : Base(
          imgpath,
          modelpath,
          width,
          height,
          N,
          close_log)
{
    this->m_close_log = close_log;
    this->inference_time = 0;
    this->org_imgH = 0;
    this->org_imgW = 0;

    // 这里给出的调色版颜色是绝对路径
    std::string cls_path = "/home/ubuntu/Documents/KTG/myPro/myProject/myTinyWebServer-v2/root/model_weights/platte.txt";
    if (!this->readFile(cls_path))
    {
        printf("please make sure your file is exist!");
        LOG_ERROR("%s %d %s", __FILE__, __LINE__, "please make sure your file is exist!");
    }
}

bool Segmentation::readFile(std::string file_path)
{
    indexMapName.clear();
    std::ifstream file(file_path); // 替换为你的实际文件名

    if (!file.is_open())
    {
        LOG_ERROR("%s", "open file is failed!");
        return false;
    }

    std::string line;
    int index = 0; // 使用行号作为索引

    while (getline(file, line))
    {
        // 跳过空行
        if (line.empty())
            continue;

        // 去除行首行尾的空格
        size_t start = line.find_first_not_of(" \t");
        size_t end = line.find_last_not_of(" \t");

        if (start != string::npos && end != string::npos)
        {
            line = line.substr(start, end - start + 1);
        }

        // 使用字符串流解析RGB值
        std::stringstream ss(line);
        std::vector<int> rgbValues;
        std::string token;

        // 按逗号分割字符串
        while (getline(ss, token, ','))
        {
            // 去除token前后的空格
            size_t token_start = token.find_first_not_of(" \t");
            size_t token_end = token.find_last_not_of(" \t");

            if (token_start != string::npos && token_end != string::npos)
            {
                token = token.substr(token_start, token_end - token_start + 1);

                try
                {
                    int value = stoi(token);
                    rgbValues.push_back(value);
                }
                catch (const exception &e)
                {
                    cerr << "解析错误: " << token << endl;
                }
            }
        }

        // 确保有3个RGB值
        if (rgbValues.size() == 3)
        {
            indexMapName[index] = rgbValues;
            index++;
        }
        else if (!rgbValues.empty())
        {
            cerr << "第 " << index << " 行数据不完整，跳过" << endl;
        }
    }
    file.close();
    return !indexMapName.empty();
}

Segmentation::Segmentation(const Segmentation &seg)
    : Base(seg), // 调用基类拷贝构造
      inference_time(seg.inference_time),
      indexMapName(seg.indexMapName)
{
    // 可添加其他初始化代码
    this->org_imgH = seg.org_imgH;
    this->org_imgW = seg.org_imgW;
}

Segmentation &Segmentation::operator=(const Segmentation &seg)
{
    if (this == &seg)
        return *this; // 自赋值检查
    inference_time = seg.inference_time;
    indexMapName = seg.indexMapName;
    Image = seg.Image;
    org_imgH = seg.org_imgH;
    org_imgW = seg.org_imgW;

    return *this;
}

void Segmentation::openModel()
{
    // 判断文件是否存在
    if (stat(this->getModelPath().c_str(), &this->m_file_stat) < 0)
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

    if (this->getModelPath().empty())
    {
        LOG_ERROR("%s %d %s", __FILE__, __LINE__, "this model path is empty!");
        return;
    }

    // 读取模型ONNX文件
    this->model = cv::dnn::readNetFromONNX(this->getModelPath());
    if (this->model.empty())
    {
        LOG_ERROR("%s", "object detect and open model is failed!");
        return;
    }
}

cv::Mat Segmentation::createBatch(cv::Mat &img)
{
    // 获取原始图像的维度
    int channels = img.channels();
    int height = img.rows;
    int width = img.cols;

    int batch_n = getBatchN();

    cv::Mat batch;
    if (img.dims != 3)
    {
        // 重新调整mat的维度：2D [HW] => 3D [CHW]
        batch = img.reshape(1, std::vector<int>{channels, height, width});
        // 重新调整mat维度 3D [CHW] => 4D [NCHW]
        batch = img.reshape(1, std::vector<int>{batch_n, channels, height, width});
    }
    else
    {
        // 重新调整mat维度 3D [CHW] => 4D [NCHW]
        batch = img.reshape(1, std::vector<int>{batch_n, channels, height, width});
    }

    return batch;
}
cv::Mat Segmentation::normalizeBlob(cv::Mat &inputBlob, cv::Scalar &mean, cv::Scalar &std)
{
    int height = this->getImageH();
    int width = this->getImageW();
    if (inputBlob.dims != 4 || inputBlob.size[0] != 1 || inputBlob.size[1] != 3 ||
        inputBlob.size[2] != height || inputBlob.size[3] != width)
    {
        LOG_ERROR("%s", "Input blob must be of shape [1, 3, 224, 224]");
    }
    int N = inputBlob.size[0];
    int C = inputBlob.size[1];
    int H = inputBlob.size[2];
    int W = inputBlob.size[3];
    // 对每个通道进行均值和标准差的归一化
    for (int n = 0; n < N; n++)
    {
        for (int c = 0; c < 3; ++c)
        {
            for (int h = 0; h < height; ++h)
            {
                for (int w = 0; w < width; ++w)
                {
                    // 索引位置计算：
                    int index = n * (C * H * W) + c * (H * W) + h * W + w;
                    inputBlob.ptr<float>()[index] = (inputBlob.ptr<float>()[index] - mean[c]) / std[c];
                }
            }
        }
    }
    return inputBlob;
}

void Segmentation::encodeImage(cv::Mat &image)
{
    // 编码为JPEG格式
    std::vector<uchar> buffer;
    cv::imencode(".jpg", image, buffer);
    // 获取数据长度
    this->image_data_length = buffer.size();
    // 获取指向二进制数据的指针
    this->encode_image = buffer.data();
}

void Segmentation::predictImage()
{
    // 开始预测时间
    long long s_time = get_current_time_ms();

    int height = this->getImageH();
    int width = this->getImageW();

    // 读取图像文件和模型文件
    cv::Mat image = this->getImage();
    // printf("%s %d height = %d width = %d\n", __FILE__, __LINE__, image.rows, image.cols);

    // 对图像进行预处理，让输入的图像符合加载模型要求 => [N,C,H,W]
    cv::Mat blob;
    // 设定均值和标准差（与 PyTorch 中相同）
    cv::Scalar mean(0.485, 0.456, 0.406);
    cv::Scalar std_dev(0.229, 0.224, 0.225);
    cv::dnn::blobFromImage(image, blob, 1.0 / 255, cv::Size(height, width), cv::Scalar(), true, false);

    blob = this->normalizeBlob(blob, mean, std_dev);
    this->model.setInput(blob);
    // 注意这里的输出层名称一定要和转换ONNX时指定的输出层名称相同
    std::vector<cv::Mat> outputBlobs;
    // 注意这里的输出名称要和转换的ONNX模型文件对应
    std::vector<std::string> outBlobNames = {"out", "aux"};

    this->model.forward(outputBlobs, outBlobNames);
    // this -> model.forward(outputBlobs,model.getUnconnectedOutLayersNames());

    cv::Mat out = outputBlobs[0];
    cv::Mat aux = outputBlobs[1];
    // 根据对图像每一个像素预测的结果，选择最大概率值的类别索引
    cv::Mat mask = cv::Mat::zeros(cv::Size(width, height), CV_32S);
    int N = outputBlobs[0].size[0];
    int num_classes = outputBlobs[0].size[1];
    int H = outputBlobs[0].size[2];
    int W = outputBlobs[0].size[3];

    for (int i = 0; i < N; i++)
    {
        for (int h = 0; h < H; h++)
        {
            for (int w = 0; w < W; w++)
            {
                float max_prob = 0;
                int classId = 0;
                for (int c = 0; c < num_classes; c++)
                {
                    int index = i * (num_classes * H * W) + c * (H * W) + h * W + w;
                    if (out.ptr<float>()[index] > max_prob)
                    {
                        max_prob = out.ptr<float>()[index];
                        classId = c;
                    }
                }
                mask.at<int>(h, w) = classId;
            }
        }
    }

    cv::Mat seg_img = this->cam_mask(mask, num_classes);
    this->org_imgW = this->getImage().cols;
    this->org_imgH = this->getImage().rows;
    // printf("%s %d ----------------> org height = %d org width = %d\n", __FILE__, __LINE__, seg_img.rows, seg_img.cols);
    cv::resize(seg_img, this->Image, cv::Size(this->org_imgW, this->org_imgH));
    // 结束预测时间
    long long e_time = get_current_time_ms();
    this->inference_time = e_time - s_time;
}

cv::Mat Segmentation::cam_mask(cv::Mat &mask, int num_classes)
{
    // 得到当前模型输入的图像分辨率大小
    int height = this->getImageH();
    int width = this->getImageW();
    // 创建空矩阵用于保存分割之后的图
    cv::Mat seg_img = cv::Mat::zeros(cv::Size(width, height), CV_8UC3);

    for (int i = 0; i < num_classes; i++)
    {
        for (int h = 0; h < height; ++h)
        {
            for (int w = 0; w < width; ++w)
            {
                cv::Vec3b &pixel = seg_img.at<cv::Vec3b>(h, w); // (R,G,B)

                if (mask.at<int>(h, w) == i)
                {
                    pixel[0] = cv::saturate_cast<uchar>(this->indexMapName[i][0]);
                    pixel[1] = cv::saturate_cast<uchar>(this->indexMapName[i][1]);
                    pixel[2] = cv::saturate_cast<uchar>(this->indexMapName[i][2]);
                }
            }
        }
    }
    return seg_img;
}

void Segmentation::timeDetect()
{
    cv::VideoCapture cap;
    cap.open(0); // 0-打开默认的摄像头
    int count_fps = 0;

    // 开始预测时间
    auto start = std::chrono::high_resolution_clock::now();

    // 得到当前模型输入的图像分辨率大小
    int height = this->getImageW();
    int width = this->getImageH();

    // 打开摄像头
    while (cap.isOpened())
    {
        cv::Mat frame;
        // 读取视频帧并判断读取是否成功
        bool ret = cap.read(frame);
        if (!ret)
        {
            LOG_ERROR("%s", "open camera is failed!");
            return;
        }

        this->setImage(frame);
        cv::Mat image = frame;

        // 计算FPS
        auto end = std::chrono::high_resolution_clock::now();
        count_fps += 1;
        float fps = 0;
        int timeDist = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
        if (timeDist > 0)
        {
            fps = count_fps * 1000.0 / timeDist;
        }

        // 对单张帧进行分割
        this->predictImage();

        frame = image;
        cv::imshow("img", frame);
        // 按下ESC键结束检测
        if (cv::waitKey(1) == 27)
        {
            cap.release();
            cv::destroyAllWindows();
            break;
        }
    }
}