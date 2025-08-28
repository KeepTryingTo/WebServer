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

class Segmentation : public Base
{
public:
    Segmentation() {}
    Segmentation(std::string imgpath, std::string modelpath,
                 size_t width, size_t height,
                 int N, int close_log);

    // 拷贝构造和赋值运算实现
    Segmentation(const Segmentation &seg);
    Segmentation &operator=(const Segmentation &seg);

    // 支持移动语义
    Segmentation(Segmentation &&) = default;
    Segmentation &operator=(Segmentation &&) = default;

    void timeDetect();
    void predictImage();
    bool readFile(std::string file_path);
    void encodeImage(cv::Mat &image);
    cv::Mat cam_mask(cv::Mat &mask, int num_classes);

    void openModel() override;
    cv::Mat createBatch(cv::Mat &img) override;
    cv::Mat normalizeBlob(cv::Mat &inputBlob, cv::Scalar &mean, cv::Scalar &std) override;

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
    const std::map<int, std::vector<int>> &getClassMap() const
    {
        return indexMapName;
    }
    // 根据预测的索引获取类别名称
    std::vector<int> getClassName(int index) const
    {
        auto it = indexMapName.find(index);
        if (it != indexMapName.end())
        {
            return it->second;
        }
        return {};
    }
    // 总的类别数
    size_t getClassCount() const
    {
        return indexMapName.size();
    }
    ~Segmentation() {}

private:
    int m_close_log;
    cv::dnn::Net model;
    cv::Mat Image;
    uchar *encode_image;
    size_t org_imgW;
    size_t org_imgH;
    size_t image_data_length;
    struct stat m_file_stat;
    long long inference_time;
    std::map<int, std::vector<int>> indexMapName;
};