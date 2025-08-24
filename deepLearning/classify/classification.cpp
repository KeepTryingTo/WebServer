#include "classification.h"

Classification::Classification(std::string imgpath,
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
    this->pred_results = "";
    this->pred_prob = 0;
    std::vector<std::string> nameList = {"daisy", "dandelion", "rose", "sunflower", "tulip"};
    for (int i = 0; i < nameList.size(); i++)
    {
        this->indexMapName[i] = nameList[i];
    }
}

Classification::Classification(const Classification &cls)
    : Base(cls), // 调用基类拷贝构造
      inference_time(cls.inference_time),
      pred_prob(cls.pred_prob),
      pred_results(cls.pred_results),
      indexMapName(cls.indexMapName)
{
    // 可添加其他初始化代码
}

Classification &Classification::operator=(const Classification &cls)
{
    if (this == &cls)
        return *this; // 自赋值检查
    inference_time = cls.inference_time;
    pred_prob = cls.pred_prob;
    pred_results = cls.pred_results;
    indexMapName = cls.indexMapName;

    return *this;
}

cv::Mat Classification::createBatch(cv::Mat &img)
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
cv::Mat Classification::normalizeBlob(cv::Mat &inputBlob, cv::Scalar &mean, cv::Scalar &std)
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

void Classification::predictImage()
{
    // 开始预测时间
    long long s_time = get_current_time_ms();

    cv::Mat Image = this->getImage();

    // 加载权重文件(ONNX文件路径)
    cv::dnn::Net net = cv::dnn::readNetFromONNX(this->getModelPath());
    if (net.empty())
    {
        LOG_ERROR("%s : %s", "提示", "加载文件失败");
        return;
    }

    cv::dnn::ClassificationModel model(net);

    // 设定均值和标准差（与 PyTorch 中相同）
    cv::Scalar mean(0.485, 0.456, 0.406);
    cv::Scalar std_dev(0.229, 0.224, 0.225);

    model.setInputSize(cv::Size(224, 224)); // 缩放到指定大小
    // model.setInputScale(1.0 / 255);
    // model.setInputSwapRB(true);
    // model.setInputCrop(false);

    cv::resize(Image, Image, cv::Size(224, 224));
    Image.convertTo(Image, CV_32F, 1.0 / 255.0);   // 归一化到[0,255]
    cv::cvtColor(Image, Image, cv::COLOR_BGR2RGB); // 交换R和B通道
    // this -> Image = this -> createBatch(Image,1); //手动的升维，加一个维度batch = 1
    // qDebug()<<"Image.shape: "<<Image.size[0]<<","<<Image.size[1]<<","<<Image.size[2]<<","<<Image.size[3];
    // //归一化方式一
    Image = this->normalizeBlob(Image, mean, std_dev); // 手动归一化
    // 归一化方式二
    //  std::cout<<"image channels = "<<Image.channels()<<std::endl;
    //  std::cout<<"image total = "<<Image.total()<<std::endl;
    //  for (int c = 0; c < Image.channels(); ++c) {
    //      float* data = Image.ptr<float>(c);//ptr<T>(c) 函数返回通道 c 的起始地址
    //      for (int i = 0; i < Image.total(); ++i) {
    //          data[i] = (data[i] - mean[c]) / std_dev[c];  // 手动归一化
    //      }
    //  }

    cv::Mat output;
    std::pair<int, float> predictions = model.classify(Image);
    std::cout << "index = " << predictions.first << " conf = " << predictions.second << std::endl;
    this->pred_results = this->indexMapName[predictions.first];
    this->pred_prob = predictions.second;

    // 结束预测时间
    long long e_time = get_current_time_ms();
    this->inference_time = e_time - s_time;
}