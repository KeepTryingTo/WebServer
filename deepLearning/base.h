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

#include "../log/log.h"

class Base
{
public:
    Base() {}
    Base(std::string imgpath, std::string modelpath, size_t width, size_t height, int N, int close_log);

    // 拷贝构造和赋值运算符重载
    Base(const Base &base);
    Base &operator=(const Base &base);

    virtual ~Base() {}

    void setImagePath(const std::string &imagepath)
    {
        this->imagePath = imagepath;
    }
    void setMdoelPath(const std::string &modelPath)
    {
        this->modelPath = modelPath;
    }
    void setImgWH(size_t img_width, size_t img_height)
    {
        this->img_height = img_height;
        this->img_width = img_width;
    }
    void setImage(cv::Mat &image)
    {
        this->Image = image;
    }

    std::string getImagePath() const
    {
        return this->imagePath;
    }
    std::string getModelPath() const
    {
        return this->modelPath;
    }
    size_t getImageW() const
    {
        return this->img_width;
    }
    size_t getImageH() const
    {
        return this->img_height;
    }

    cv::Mat &getImage()
    {
        return this->Image;
    }
    const cv::Mat &getImage() const
    {
        return this->Image;
    } // 重载 const 版本

    int getBatchN() const
    {
        return this->batch_n;
    }

    long long get_current_time_ms();

    virtual void openImage();
    virtual void openModel();
    virtual cv::Mat createBatch(cv::Mat &img);
    virtual cv::Mat normalizeBlob(cv::Mat &inputBlob, cv::Scalar &mean, cv::Scalar &std);

private:
    cv::Mat Image;
    int m_close_log;
    int batch_n; // batch 维度
    size_t img_width;
    size_t img_height;
    struct stat m_file_stat;
    std::string modelPath;
    std::string imagePath;
};