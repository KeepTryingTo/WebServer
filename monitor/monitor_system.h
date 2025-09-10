#ifndef MONITOR_SYSTEM_H
#define MONITOR_SYSTEM_H

#include <atomic>
#include <string>
#include <chrono>
#include <map>
#include <mutex>

class MonitorSystem
{
public:
    // 采用懒汉式单例模式（线程安全）
    static MonitorSystem &instance();

    // 禁用拷贝和赋值
    MonitorSystem(const MonitorSystem &) = delete;
    MonitorSystem &operator=(const MonitorSystem &) = delete;

    // 指标记录接口
    void record_connection_start();
    void record_connection_end();
    void record_request_start();
    void record_request_end(int method, int status, bool ssl_success);
    void record_bytes_transferred(size_t read_bytes, size_t written_bytes);
    void record_request_duration(uint64_t duration_ms);

    // 管理接口
    std::string get_metrics_json() const;
    std::string get_metrics_prometheus() const;

    // 管理控制台HTML
    static std::string get_admin_html();

private:
    MonitorSystem(); // 私有构造函数

    // 连接指标
    std::atomic<uint64_t> active_connections_;
    std::atomic<uint64_t> total_connections_;

    // 请求指标
    std::atomic<uint64_t> requests_total_;
    std::atomic<uint64_t> request_duration_ms_;

    // 方法指标 (GET=0, POST=1, etc.)
    std::atomic<uint64_t> requests_by_method_[9];

    // 状态码指标 (NO_REQUEST=0, FILE_REQUEST=1, etc.)
    std::atomic<uint64_t> requests_by_status_[7];

    // 传输指标
    std::atomic<uint64_t> read_bytes_total_;
    std::atomic<uint64_t> write_bytes_total_;

    // SSL指标
    std::atomic<uint64_t> ssl_handshakes_;
    std::atomic<uint64_t> ssl_errors_;

    // 线程安全
    mutable std::mutex mutex_;
};

#endif // MONITOR_SYSTEM_H