#ifndef HTTP_CONN_MONITOR_ADAPTER_H
#define HTTP_CONN_MONITOR_ADAPTER_H

#include "monitor_system.h"

class HttpConnMonitorAdapter
{
public:
    explicit HttpConnMonitorAdapter(MonitorSystem &monitor) : monitor_(monitor) {}

    void on_connection_start()
    {
        monitor_.record_connection_start();
    }

    void on_connection_end()
    {
        monitor_.record_connection_end();
    }

    void on_request_start(int method)
    {
        current_method_ = method;
        start_time_ = std::chrono::steady_clock::now();
        monitor_.record_request_start();
    }

    void on_request_end(int status, bool ssl_success)
    {
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start_time_);

        monitor_.record_request_end(current_method_, status, ssl_success);
        monitor_.record_request_duration(duration.count()); // 新增此行
        monitor_.record_bytes_transferred(read_bytes_, written_bytes_);

        // Reset for next request
        read_bytes_ = 0;
        written_bytes_ = 0;
    }

    void on_data_read(size_t bytes)
    {
        read_bytes_ += bytes;
    }

    void on_data_written(size_t bytes)
    {
        written_bytes_ += bytes;
    }

private:
    MonitorSystem &monitor_;
    std::chrono::steady_clock::time_point start_time_;
    int current_method_ = 0;
    size_t read_bytes_ = 0;
    size_t written_bytes_ = 0;
};

#endif // HTTP_CONN_MONITOR_ADAPTER_H