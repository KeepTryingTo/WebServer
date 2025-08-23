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
#include <vector>
#include <map>
#include <string>
#include <time.h>
#include <sys/stat.h>

#include "../../log/log.h"
#include "../base.h"

class Classification : public Base
{
public:
    Classification() {}
    Classification(std::string imgpath, std::string modelpath, size_t width, size_t height, int N, int close_log);

    // 拷贝构造和赋值运算实现
    Classification(const Classification &cls);
    Classification &operator=(const Classification &cls);

    // 支持移动语义
    Classification(Classification &&) = default;
    Classification &operator=(Classification &&) = default;

    long long getInferTime()
    {
        return this->inference_time;
    }
    std::string getPredResult()
    {
        return this->pred_results;
    }
    double getPredProb()
    {
        return this->pred_prob;
    }

    void predictImage();
    cv::Mat createBatch(cv::Mat &img) override;
    cv::Mat normalizeBlob(cv::Mat &inputBlob, cv::Scalar &mean, cv::Scalar &std) override;

    ~Classification() {}

private:
    int m_close_log;
    long long inference_time;
    std::string pred_results;
    double pred_prob; // 这里将预测的概率使用转换为字符串表示
    std::map<int, std::string> indexMapName;
};