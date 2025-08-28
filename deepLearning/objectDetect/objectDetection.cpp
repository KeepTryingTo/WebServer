#include "objectDetection.h"

ObjectDetection::ObjectDetection(std::string imgpath,
                                 std::string modelpath,
                                 size_t width,
                                 size_t height,
                                 float iou_threshold,
                                 float conf_threshold,
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
    this->iou_threshold = iou_threshold;
    this->conf_threshold = conf_threshold;
    this->org_imgH = 0;
    this->org_imgW = 0;

    std::string cls_path = "/home/ubuntu/Documents/KTG/myPro/myProject/myTinyWebServer-v2/root/model_weights/classes.txt";
    if (!this->readFile(cls_path))
    {
        printf("please make sure your file is exist!");
        LOG_ERROR("%s %d %s", __FILE__, __LINE__, "please make sure your file is exist!");
    }
}
bool ObjectDetection::readFile(std::string file_path)
{
    indexMapName.clear();
    std::ifstream file(file_path);

    if (!file.is_open())
    {
        throw std::runtime_error("无法打开文件: " + file_path);
    }

    std::string line;
    int index = 0;

    // 读取每一行的内容
    while (std::getline(file, line))
    {
        if (!line.empty())
        {
            // 清理行内容
            if (line.back() == '\r')
                line.pop_back();

            // 去掉一行中的开头和结尾空格以及\t符号
            size_t start = line.find_first_not_of(" \t");
            size_t end = line.find_last_not_of(" \t");

            // 如果是一个合法的字符串
            if (start != std::string::npos)
            {
                // 保证合法之后加入映射表中
                std::string clean_line = (end == std::string::npos) ? line.substr(start) : line.substr(start, end - start + 1);
                if (!clean_line.empty())
                {
                    indexMapName[index++] = clean_line;
                }
            }
        }
    }

    file.close();
    return !indexMapName.empty();
}

ObjectDetection::ObjectDetection(const ObjectDetection &obj)
    : Base(obj), // 调用基类拷贝构造
      inference_time(obj.inference_time),
      indexMapName(obj.indexMapName)
{
    // 可添加其他初始化代码
    this->detect_count = obj.detect_count;
    Image = obj.Image;
    this->org_imgH = obj.org_imgH;
    this->org_imgW = obj.org_imgW;
}

ObjectDetection &ObjectDetection::operator=(const ObjectDetection &obj)
{
    if (this == &obj)
        return *this; // 自赋值检查
    inference_time = obj.inference_time;
    indexMapName = obj.indexMapName;
    detect_count = obj.detect_count;
    Image = obj.Image;
    detect_count = obj.detect_count;
    org_imgH = obj.org_imgH;
    org_imgW = obj.org_imgW;

    return *this;
}

void ObjectDetection::openModel()
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

cv::Mat ObjectDetection::createBatch(cv::Mat &img)
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
cv::Mat ObjectDetection::normalizeBlob(cv::Mat &inputBlob, cv::Scalar &mean, cv::Scalar &std)
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

void ObjectDetection::encodeImage(cv::Mat &image)
{
    // 编码为JPEG格式
    std::vector<uchar> buffer;
    cv::imencode(".jpg", image, buffer);
    // 获取数据长度
    this->image_data_length = buffer.size();
    // 获取指向二进制数据的指针
    this->encode_image = buffer.data();
}

void ObjectDetection::predictImage()
{
    // 开始预测时间
    long long s_time = get_current_time_ms();

    int height = this->getImageH();
    int width = this->getImageW();

    // 读取图像文件和模型文件
    this->Image = this->getImage();

    this->org_imgW = this->Image.cols;
    this->org_imgH = this->Image.rows;

    // 对图像进行预处理，让输入的图像符合加载模型要求 => [N,C,H,W]
    cv::Mat blob;
    cv::dnn::blobFromImage(this->Image, blob, 1.0 / 255, cv::Size(height, width), cv::Scalar(), true, false);

    this->model.setInput(blob);
    // 注意这里的输出层名称一定要和转换ONNX时指定的输出层名称相同
    std::vector<cv::Mat> outputBlobs;
    // 注意这里的输出名称要和转换的ONNX模型文件对应
    std::vector<std::string> outBlobNames = {"output0"};

    this->model.forward(outputBlobs, outBlobNames);
    // this -> model.forward(outputBlobs,model.getUnconnectedOutLayersNames());

    std::vector<cv::Rect> boxes;
    std::vector<float> confidences;
    std::vector<int> classIds;

    float *data = (float *)outputBlobs[0].data;
    const int rows = 25200;

    for (int i = 0; i < rows; ++i)
    {
        float confidence = data[4];
        if (confidence >= this->conf_threshold)
        {

            float *classes_scores = data + 5;
            cv::Mat scores(1, this->indexMapName.size(), CV_32FC1, classes_scores);
            cv::Point class_id;
            double max_class_score;
            minMaxLoc(scores, 0, &max_class_score, 0, &class_id);

            confidences.push_back(confidence);
            classIds.push_back(class_id.x);

            float x = data[0];
            float y = data[1];
            float w = data[2];
            float h = data[3];
            float xleft = x - w / 2;
            float yleft = y - h / 2;
            boxes.push_back(cv::Rect(xleft, yleft, w, h));
        }
        data += 85;
    }

    // NMS算法过滤掉重叠的框
    std::vector<int> indices;
    cv::dnn::NMSBoxes(boxes, confidences, this->conf_threshold, this->iou_threshold, indices);
    // 绘制检测信息到图像中
    for (int i : indices)
    {
        // 绘制有效的边界框
        cv::Rect box = boxes[i];
        // 由于这里得到的边界框是相对于模型输入大小的，但是需要实际图像大小对坐标框进行调整
        box = this->out2org(box, cv::Size(width, height), cv::Size(Image.cols, Image.rows));
        // 绘制坐标框
        cv::rectangle(this->Image, box, cv::Scalar(0, 255, 0), 2);
        // 标上置信度以及类别
        //  使用示例
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(2) << (confidences[i] * 100);
        std::string formattedValue = oss.str();
        std::string text = formatString(this->indexMapName[classIds[i]] + " %1%", formattedValue);

        cv::Point org(box.x, box.y - 5);
        cv::putText(this->Image, text, org, cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(0, 255, 0), 1);
    }

    // 更新结果图像
    this->setImage(this->Image);
    this->setDetectCount(indices.size());

    // 结束预测时间
    long long e_time = get_current_time_ms();
    this->inference_time = e_time - s_time;
}

cv::Rect ObjectDetection::out2org(cv::Rect box, cv::Size crop_size, cv::Size org_size)
{
    double xleft = box.x;
    double yleft = box.y;
    double xright = box.x + box.width;
    double yright = box.y + box.height;

    xleft = xleft / crop_size.width * org_size.width;
    xright = xright / crop_size.width * org_size.width;
    yleft = yleft / crop_size.height * org_size.height;
    yright = yright / crop_size.height * org_size.height;

    cv::Rect box_;
    box_.x = xleft;
    box_.y = yleft;
    box_.width = xright - xleft;
    box_.height = yright - yleft;

    return box_;
}

void ObjectDetection::timeDetect()
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

        // 对单张帧进行目标检测
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