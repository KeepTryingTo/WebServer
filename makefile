CXX ?= g++

DEBUG ?= 1
ifeq ($(DEBUG), 1)
    CXXFLAGS += -g
else
    CXXFLAGS += -O2

endif

# 如果 pkg-config 找不到，可以手动指定路径（根据您的安装位置调整）
OPENCV_INCLUDE := -I/usr/local/include/opencv4
OPENCV_LIBS := -L/usr/local/lib -lopencv_core -lopencv_highgui -lopencv_imgproc -lopencv_imgcodecs -lopencv_dnn -lopencv_ml


SRCS = main.cpp \
       ./timer/lst_timer.cpp \
       ./http/http_conn.cpp \
       ./log/log.cpp \
       ./CGImysql/sql_connection_pool.cpp \
       webserver.cpp \
       config.cpp \
       ./deepLearning/base.cpp \
       ./deepLearning/classify/classification.cpp

LIBS = -lpthread -lmysqlclient $(OPENCV_LIBS)
# 添加 OpenCV 头文件路径
CXXFLAGS += $(OPENCV_INCLUDE)

server: $(SRCS)
	$(CXX) -o server $^ $(CXXFLAGS) $(LIBS)

clean:
	rm  -r server
