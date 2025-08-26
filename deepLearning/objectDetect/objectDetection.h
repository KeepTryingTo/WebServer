#pragma once

#include <opencv4/opencv2/opencv.hpp>
#include <opencv4/opencv2/highgui/highgui.hpp>
#include <opencv4/opencv2/core/core.hpp>
#include <opencv4/opencv2/dnn.hpp>
#include <opencv4/opencv2/imgproc/imgproc.hpp>
#include <opencv4/opencv2/imgcodecs/imgcodecs.hpp>
#include <opencv4/opencv2/features2d/features2d.hpp>
#include <opencv4/opencv2/video/video.hpp>

#include <iostream>
#include <fstream>
#include <vector>
#include <map>
#include <string>
#include <time.h>
#include <sys/stat.h>
#include <iomanip>

#include "../../log/log.h"
#include "../base.h"

class ObjectDetection : public Base
{
public:
    ObjectDetection() {}
    ObjectDetection(std::string imgpath, std::string modelpath,
                    size_t width, size_t height,
                    float iou_threshold,
                    float conf_threshold,
                    int N, int close_log);

    // 拷贝构造和赋值运算实现
    ObjectDetection(const ObjectDetection &cls);
    ObjectDetection &operator=(const ObjectDetection &cls);

    // 支持移动语义
    ObjectDetection(ObjectDetection &&) = default;
    ObjectDetection &operator=(ObjectDetection &&) = default;

    void timeDetect();
    void predictImage();
    bool readFile(std::string file_path);
    void encodeImage(cv::Mat &image);
    cv::Rect out2org(cv::Rect box, cv::Size crop_size, cv::Size org_size);

    void openModel() override;
    cv::Mat createBatch(cv::Mat &img) override;
    cv::Mat normalizeBlob(cv::Mat &inputBlob, cv::Scalar &mean, cv::Scalar &std) override;

    void setIOUThreshold(float threshold)
    {
        this->iou_threshold = threshold;
    }
    void setConfThreshold(float threshold)
    {
        this->conf_threshold = conf_threshold;
    }
    // 设置检测结果包含的目标数
    void setDetectCount(const size_t &count)
    {
        this->detect_count = count;
    }

    size_t getDetectCount()
    {
        return this->detect_count;
    }

    size_t getImageDataLength()
    {
        return this->image_data_length;
    }
    uchar *getEncodeImage()
    {
        return this->encode_image;
    }

    cv::Mat getImageObj()
    {
        return this->Image;
    }

    pair<size_t, size_t> getOrgImgHW()
    {
        return {this->org_imgH, this->org_imgW};
    }

    // 推理时间
    long long getInferTime()
    {
        return this->inference_time;
    }
    // 返回类别表
    const std::map<int, std::string> &getClassMap() const
    {
        return indexMapName;
    }
    // 根据预测的索引获取类别名称
    std::string getClassName(int index) const
    {
        auto it = indexMapName.find(index);
        if (it != indexMapName.end())
        {
            return it->second;
        }
        return "Unknown";
    }
    // 总的类别数
    size_t getClassCount() const
    {
        return indexMapName.size();
    }
    // 创建一个类似 QString::arg 的格式化函数
    // 修改函数签名，接受字符串参数
    std::string formatString(const std::string &format, const std::string &value)
    {
        std::ostringstream oss;
        size_t pos = format.find("%1");
        if (pos != std::string::npos)
        {
            oss << format.substr(0, pos) << value << format.substr(pos + 2);
        }
        else
        {
            oss << format << value;
        }
        return oss.str();
    }

    ~ObjectDetection() {}

private:
    int m_close_log;
    float iou_threshold;
    float conf_threshold;
    cv::dnn::Net model;
    cv::Mat Image;
    uchar *encode_image;
    size_t org_imgW;
    size_t org_imgH;
    size_t detect_count;
    size_t image_data_length;
    struct stat m_file_stat;
    long long inference_time;
    std::map<int, std::string> indexMapName;
};