cmake_minimum_required(VERSION 3.10)
project(WebServer)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(DEBUG "Build with debug information" ON)
if(DEBUG)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g")
else()
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O2")
endif()

if(NOT DEBUG)
    add_compile_options(-O2)  # 显式添加优化选项
endif()

# MySQL配置
find_path(MYSQL_INCLUDE_DIR mysql.h
    PATHS
    /usr/include/mysql
    /usr/local/include/mysql
    /opt/mysql/include
)

find_library(MYSQL_LIBRARY
    NAMES mysqlclient libmysqlclient
    PATHS
    /usr/lib
    /usr/lib/x86_64-linux-gnu
    /usr/local/lib
    /opt/mysql/lib
)


find_package(OpenCV REQUIRED)  # 自动查找 OpenCV
include_directories(${OpenCV_INCLUDE_DIRS})

if(NOT MYSQL_INCLUDE_DIR OR NOT MYSQL_LIBRARY)
    message(FATAL_ERROR "MySQL development libraries not found. Install with: sudo apt-get install libmysqlclient-dev")
endif()



include_directories(
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/timer
    ${CMAKE_SOURCE_DIR}/http
    ${CMAKE_SOURCE_DIR}/log
    ${CMAKE_SOURCE_DIR}/CGImysql
    ${CMAKE_SOURCE_DIR}/threadpool
    ${CMAKE_SOURCE_DIR}/lock
    ${CMAKE_SOURCE_DIR}/deepLearning
    ${CMAKE_SOURCE_DIR}/deepLearning/classify
    ${MYSQL_INCLUDE_DIR}
)

set(SOURCES
    main.cpp
    timer/lst_timer.cpp
    http/http_conn.cpp
    log/log.cpp
    CGImysql/sql_connection_pool.cpp
    webserver.cpp
    config.cpp
    deepLearning/base.cpp
    deepLearning/classify/classification.cpp
)

add_executable(server ${SOURCES})

target_link_libraries(server
    PRIVATE
    pthread
    ${MYSQL_LIBRARY}
    ${OpenCV_LIBS}
)