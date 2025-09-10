#include "monitor_system.h"
#include <sstream>
#include <iomanip>

// 采用懒汉式单例模式（线程安全）
MonitorSystem &MonitorSystem::instance()
{
    static MonitorSystem instance;
    return instance;
}

MonitorSystem::MonitorSystem() : active_connections_(0), total_connections_(0),
                                 requests_total_(0), request_duration_ms_(0),
                                 read_bytes_total_(0), write_bytes_total_(0),
                                 ssl_handshakes_(0), ssl_errors_(0)
{

    for (auto &method : requests_by_method_)
        method = 0;
    for (auto &status : requests_by_status_)
        status = 0;
}

void MonitorSystem::record_connection_start()
{
    active_connections_++;
    total_connections_++;
}

void MonitorSystem::record_connection_end()
{
    active_connections_--;
}

void MonitorSystem::record_request_start()
{
    requests_total_++;
}

void MonitorSystem::record_request_end(int method, int status, bool ssl_success)
{
    if (method >= 0 && method < 9)
    {
        requests_by_method_[method]++;
    }

    if (status >= 0 && status < 7)
    {
        requests_by_status_[status]++;
    }

    if (ssl_success)
    {
        ssl_handshakes_++;
    }
    else
    {
        ssl_errors_++;
    }
}

void MonitorSystem::record_request_duration(uint64_t duration_ms)
{
    request_duration_ms_ += duration_ms;
}

void MonitorSystem::record_bytes_transferred(size_t read_bytes, size_t written_bytes)
{
    read_bytes_total_ += read_bytes;
    write_bytes_total_ += written_bytes;
}

std::string MonitorSystem::get_metrics_json() const
{
    std::lock_guard<std::mutex> lock(mutex_);

    auto avg_duration = requests_total_ > 0 ? request_duration_ms_ / requests_total_ : 0;

    std::ostringstream json;
    json << std::fixed << std::setprecision(2); // 设置两位小数精度
    json << "{";
    json << "\"connections\":{";
    json << "\"active\":" << active_connections_ << ",";
    json << "\"total\":" << total_connections_;
    json << "},";

    json << "\"requests\":{";
    json << "\"total\":" << requests_total_ << ",";
    json << "\"avg_duration_ms\":" << avg_duration << ",";
    json << "\"by_method\":{";
    json << "\"GET\":" << requests_by_method_[0] << ",";
    json << "\"POST\":" << requests_by_method_[1] << ",";
    json << "\"PUT\":" << requests_by_method_[3];
    json << "},";
    json << "\"by_status\":{";
    json << "\"NO_REQUEST\":" << requests_by_status_[0] << ",";
    json << "\"FILE_REQUEST\":" << requests_by_status_[5];
    json << "}";
    json << "},";

    json << "\"transfer\":{";
    json << "\"read_bytes\":" << read_bytes_total_ << ",";
    json << "\"written_bytes\":" << write_bytes_total_;
    json << "},";

    json << "\"ssl\":{";
    json << "\"handshakes\":" << ssl_handshakes_ << ",";
    json << "\"errors\":" << ssl_errors_;
    json << "}";
    json << "}";

    return json.str();
}

// 默认的monitor system页面，进入这个页面之后就会主动的请求/admin/metrics，然后服务端响应
std::string MonitorSystem::get_admin_html()
{
    // 返回静态HTML内容，同上文admin.html
    // 这里简化为返回一个简单页面
    return R"(
<!DOCTYPE html>
<html>
<head><title>WebServer Admin</title></head>
<body>
    <h1>WebServer Management Console</h1>
    <div id="metrics"></div>
    <script>
        fetch('/admin/metrics')
            .then(r => r.json())
            .then(data => {
                document.getElementById('metrics').innerHTML = 
                    'Active connections: ' + data.connections.active;
            });
    </script>
</body>
</html>
    )";
}