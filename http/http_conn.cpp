#include "http_conn.h"

#include <mysql/mysql.h>
#include <fstream>

// 定义http响应的一些状态信息
const char *ok_200_title = "OK";
const char *error_400_title = "Bad Request";
const char *error_400_form = "Your request has bad syntax or is inherently impossible to staisfy.\n";

const char *error_403_title = "Forbidden";
const char *error_403_form = "You do not have permission to get file form this server.\n";

const char *error_404_title = "Not Found";
const char *error_404_form = "The requested file was not found on this server.\n";

const char *error_500_title = "Internal Error";
const char *error_500_form = "There was an unusual problem serving the request file.\n";

locker m_lock;
map<std::string, std::string> users;

// 静态成员初始化
map<std::string, session_info> http_conn::sessions;
locker http_conn::session_lock;

void http_conn::initmysql_result(connection_pool *connPool)
{
    // 先从连接池中取一个连接，用于后面对用户表的初始化
    MYSQL *mysql = NULL;
    connectionRAII mysqlcon(&mysql, connPool);

    // 在user表中检索username，passwd数据，浏览器端输入
    if (mysql_query(mysql, "SELECT username,passwd FROM user"))
    {
        LOG_ERROR("SELECT error:%s\n", mysql_error(mysql));
    }

    // 从表中检索完整的结果集
    MYSQL_RES *result = mysql_store_result(mysql);

    // 返回结果集中的列数（字段的个数）
    int num_fields = mysql_num_fields(result);

    // 返回所有字段结构的数组（）
    MYSQL_FIELD *fields = mysql_fetch_fields(result);

    // 从结果集中获取下一行，将对应的用户名和密码，存入map中
    while (MYSQL_ROW row = mysql_fetch_row(result))
    {
        std::string temp1(row[0]);
        std::string temp2(row[1]);
        // 保存用户名和密码
        users[temp1] = temp2;
    }
}

// 对文件描述符设置非阻塞
int setnonblocking(int fd)
{
    // 设置当前socket fd非阻塞模式
    int old_option = fcntl(fd, F_GETFL);
    int new_option = old_option | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_option);
    return old_option;
}

// 将内核事件表注册读事件，ET模式，选择开启EPOLLONESHOT
void addfd(int epollfd, int fd, bool one_shot, int TRIGMode)
{
    epoll_event event;
    event.data.fd = fd;

    if (1 == TRIGMode)
        event.events = EPOLLIN | EPOLLET | EPOLLRDHUP; // 添加边缘触发模式
    else
        event.events = EPOLLIN | EPOLLRDHUP;

    /*
        件描述符上的事件只会被触发一次
        事件被触发后，该文件描述符会被自动禁用
        需要重新使用 epoll_ctl() 的 EPOLL_CTL_MOD 操作重新激活该文件描述符
    */
    if (one_shot)
        event.events |= EPOLLONESHOT;
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    setnonblocking(fd);
}

// 从内核时间表删除描述符
void removefd(int epollfd, int fd)
{
    epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, 0);
    close(fd);
}

// 将事件重置为EPOLLONESHOT
void modfd(int epollfd, int fd, int ev, int TRIGMode)
{
    epoll_event event;
    event.data.fd = fd;
    /*
        EPOLLRDHUP：表示对端关闭连接（或关闭写端，即触发了 shutdown(SHUT_WR)
        EPOLLONESHOT：避免多个线程同时处理同一个套接字：防止一个连接上的数据被多
                      个工作线程同时处理导致的竞争条件
    */
    if (1 == TRIGMode)
        event.events = ev | EPOLLET | EPOLLONESHOT | EPOLLRDHUP;
    else
        event.events = ev | EPOLLONESHOT | EPOLLRDHUP;

    epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &event);
}

int http_conn::m_user_count = 0;
int http_conn::m_epollfd = -1;

// 关闭连接，关闭一个连接，客户总量减一
void http_conn::close_conn(bool real_close)
{
    // 关闭连接以及从epoll上移除事件
    if (real_close && (m_sockfd != -1))
    {
        printf("close %d\n", m_sockfd);
        // 将对应的fd从epoll上面移除
        removefd(m_epollfd, m_sockfd);
        m_sockfd = -1;
        m_user_count--;
    }
}

// 初始化连接,外部调用初始化套接字地址
void http_conn::init(int sockfd, const sockaddr_in &addr, char *root, int TRIGMode,
                     int close_log, std::string user, std::string passwd, std::string sqlname)
{
    m_sockfd = sockfd;
    m_address = addr;

    // 将已经建立的连接注册到epoll中
    addfd(m_epollfd, sockfd, true, m_TRIGMode);
    m_user_count++;

    // 当浏览器出现连接重置时，可能是网站根目录出错或http响应格式出错或者访问的文件中内容完全为空
    doc_root = root;
    m_TRIGMode = TRIGMode;   // 水平还是边缘触发模式
    m_close_log = close_log; // 是否关闭日志
    model_name = 0;

    strcpy(sql_user, user.c_str());
    strcpy(sql_passwd, passwd.c_str());
    strcpy(sql_name, sqlname.c_str());

    // 对所有成员变量进行初始化
    init();
}

// 初始化新接受的连接
// check_state默认为分析请求行状态
void http_conn::init()
{
    mysql = NULL;
    bytes_to_send = 0;
    bytes_have_send = 0;
    m_check_state = CHECK_STATE_REQUESTLINE; // 默认初始化是对行进行解析
    m_linger = false;
    m_method = GET;
    m_url = 0;
    m_version = 0;
    m_content_length = 0;
    m_host = 0;
    m_start_line = 0;
    m_checked_idx = 0;
    m_read_idx = 0;
    m_write_idx = 0;
    cgi = 0; // 是否启用POST
    m_state = 0;
    timer_flag = 0;
    improv = 0;
    m_header_value = "";
    m_upload_filename = NULL;
    m_session_id = "";
    m_is_logged_in = false;
    m_has_session = false;
    m_session_id_buf[0] = '\0';
    m_need_set_cookie = false;
    model_name = NULL;
    m_download = "";
    iou_threshold = 0;
    conf_threshold = 0;
    is_response_result = false;
    is_objectDetect = false;

    memset(m_read_buf, '\0', READ_BUFFER_SIZE);
    memset(m_write_buf, '\0', WRITE_BUFFER_SIZE);
    memset(m_real_file, '\0', FILENAME_LEN);
}

/**
 * @brief 生成安全的随机Session ID
 *
 * 使用密码学安全的随机数生成器创建16字节随机数，
 * 并将其转换为32字符的十六进制字符串格式
 *
 * @return std::string 32字符的十六进制Session ID（如"a1b2c3d4e5f6..."）
 *
 * @note 实现细节：
 * 1. 使用std::random_device获取硬件熵源（如果可用）
 * 2. 生成16字节（128位）随机数，满足密码学强度要求
 * 3. 转换为HEX格式避免二进制数据在传输中出现问题
 * 4. 输出示例：将字节{0xAB,0xCD...}转换为字符串"abcd..."
 */
std::string http_conn::generate_session_id()
{
    // 16字节随机数缓冲区（128位）
    std::array<unsigned char, 16> bytes{};
    // 使用硬件熵源初始化随机数生成器
    std::random_device rd;
    // 填充随机字节（保留低8位）
    for (auto &b : bytes)
    {
        b = static_cast<unsigned char>(rd() & 0xFF);
    }
    // HEX转换表（小写字母）
    static const char *hex_chars = "0123456789abcdef";
    // 预分配32字符缓冲区
    std::string session_id;
    session_id.resize(32);
    // 将每个字节转为两个HEX字符
    for (size_t i = 0; i < bytes.size(); i++)
    {
        session_id[i * 2] = hex_chars[bytes[i] >> 4];       // 高4位
        session_id[i * 2 + 1] = hex_chars[bytes[i] & 0x0F]; // 低4位
    }
    return session_id;
}

bool http_conn::create_session(const std::string &username)
{

    session_lock.lock();
    try
    {

        if (m_has_session && strlen(m_session_id_buf) > 0)
        {
            std::string session_id(m_session_id_buf);
            printf("strlen(session id) = %d\n", session_id.size());
            printf("%s %d session id = %s\n", __FILE__, __LINE__, session_id.c_str());
            sessions[session_id] = session_info(username);
            sessions_st.insert(session_id);
            m_session_id = session_id; // 其实在解析头部信息的时候就赋值过了，这里只是重复的赋值一遍
        }
        else
        {
            std::string session_id = generate_session_id();
            printf("%s %d session id = %s\n", __FILE__, __LINE__, session_id.c_str());
            sessions[session_id] = session_info(username);
            m_session_id = session_id;
            sessions_st.insert(session_id);
            m_has_session = true;
            m_need_set_cookie = true;
        }
        session_lock.unlock();
        return true;
    }
    catch (...)
    {
        // std::cout<<"username = "<<username<<std::endl;
        printf("%s %d throw ERROR and username = %s\n", __FILE__, __LINE__, username.c_str());
        session_lock.unlock();
        return false;
    }
    return false;
}

bool http_conn::validate_session(const std::string &session_id)
{
    session_lock.lock();
    auto it = sessions.find(session_id);
    if (it != sessions.end() && it->second.is_valid)
    {
        // 检查session id是否过期（是否超过规定的30分钟）
        time_t now = time(NULL);
        if (now - (it->second).last_access < 1800)
        {
            it->second.last_access = now;
            session_lock.unlock();
            return true;
        }
        else
        {
            // 否则删除过期的session id
            sessions.erase(it);
        }
    }
    session_lock.unlock();
    return false;
}

void http_conn::destroy_session(const std::string &session_id)
{
    session_lock.lock();
    sessions.erase(session_id);
    session_lock.unlock();

    m_session_id = "";
    m_is_logged_in = false;
}

// 定期清理过期的session id
void http_conn::cleanup_expired_sessions()
{
    session_lock.lock();
    time_t now = time(NULL);
    auto it = sessions.begin();
    while (it != sessions.end())
    {
        if (now - (it->second.last_access) > 1800)
        {
            it = sessions.erase(it);
        }
        else
        {
            it++;
        }
    }
    session_lock.unlock();
}

// 从状态机，用于分析出一行内容
// 返回值为行的读取状态，有LINE_OK,LINE_BAD,LINE_OPEN

// m_read_idx指向缓冲区m_read_buf的数据末尾的下一个字节
// m_checked_idx指向从状态机当前正在分析的字节
/*
    在HTTP报文中，每一行的数据由\r\n作为结束字符，空行则是仅仅是字符\r\n。
    因此，可以通过查找\r\n将报文拆解成单独的行进行解析
*/
http_conn::LINE_STATUS http_conn::parse_line()
{
    char temp;
    // 在调用read_once的时候就已经讲数据一次读取出来了，m_read_idx表示当前读取数据大小
    for (; m_checked_idx < m_read_idx; ++m_checked_idx)
    {
        // temp为将要分析的字节
        temp = m_read_buf[m_checked_idx];

        // 如果当前是\r字符，则有可能会读取到完整行
        if (temp == '\r')
        {

            // 下一个字符达到了buffer结尾，则接收不完整，需要继续接收
            if ((m_checked_idx + 1) == m_read_idx)
                return LINE_OPEN;
            // 下一个字符是\n，将\r\n改为\0\0,表示读取了完整的一行
            else if (m_read_buf[m_checked_idx + 1] == '\n')
            {
                m_read_buf[m_checked_idx++] = '\0';
                m_read_buf[m_checked_idx++] = '\0';
                return LINE_OK;
            }
            // 如果都不符合，则返回语法错误
            return LINE_BAD;
        }

        // 如果当前字符是\n，也有可能读取到完整行
        // 一般是上次读取到\r就到buffer末尾了，没有接收完整，再次接收时会出现这种情况
        else if (temp == '\n')
        {
            // 前一个字符是\r，则接收完整
            if (m_checked_idx > 1 && m_read_buf[m_checked_idx - 1] == '\r')
            {
                m_read_buf[m_checked_idx - 1] = '\0';
                m_read_buf[m_checked_idx++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }
    }

    // 并没有找到\r\n，需要继续接收
    return LINE_OPEN;
}

// 循环读取客户数据，直到无数据可读或对方关闭连接
// 非阻塞ET工作模式下，需要一次性将数据读完
bool http_conn::read_once()
{
    // 判断有无数据可读
    if (m_read_idx >= READ_BUFFER_SIZE)
    {
        return false;
    }
    int bytes_read = 0;

    // LT读取数据
    if (0 == m_TRIGMode)
    {
        bytes_read = recv(m_sockfd, m_read_buf + m_read_idx,
                          READ_BUFFER_SIZE - m_read_idx, 0);
        m_read_idx += bytes_read;

        if (bytes_read <= 0)
        {
            return false;
        }

        return true;
    }
    // ET读数据
    else
    {
        // ET模式只通知一次，因此要一次性将缓冲区中所有数据都读取出来
        while (true)
        {
            bytes_read = recv(m_sockfd, m_read_buf + m_read_idx,
                              READ_BUFFER_SIZE - m_read_idx, 0);
            if (bytes_read == -1)
            {
                if (errno == EAGAIN || errno == EWOULDBLOCK)
                    break;
                return false;
            }
            else if (bytes_read == 0)
            {
                return false;
            }
            m_read_idx += bytes_read;
        }
        return true;
    }
}

// 解析http请求行，获得请求方法，目标url及http版本号
http_conn::HTTP_CODE http_conn::parse_request_line(char *text)
{
    // 用于查找字符串中任意指定字符的首次出现位置
    m_url = strpbrk(text, " \t");
    if (!m_url)
    {
        return BAD_REQUEST;
    }
    *m_url++ = '\0';
    char *method = text;
    // 是一个用于不区分大小写的字符串比较函数
    if (strcasecmp(method, "GET") == 0)
        m_method = GET;
    else if (strcasecmp(method, "POST") == 0)
    {
        m_method = POST;
        cgi = 1; // 启用POST请求
    }
    else if (strcasecmp(method, "PUT") == 0)
    { // 添加 PUT 方法支持
        m_method = PUT;
        cgi = 1;
    }
    else
        return BAD_REQUEST;
    // 跳过空白字符
    m_url += strspn(m_url, " \t");
    // 查找版本前的空格
    m_version = strpbrk(m_url, " \t");
    if (!m_version)
        return BAD_REQUEST;
    *m_version++ = '\0'; // 划分出URL
    // 跳过空白字符
    m_version += strspn(m_version, " \t");
    if (strcasecmp(m_version, "HTTP/1.1") != 0) // 只支持HTTP/1.1
        return BAD_REQUEST;
    // 处理http://前缀
    if (strncasecmp(m_url, "http://", 7) == 0)
    {
        m_url += 7;
        // 查找第一个/
        m_url = strchr(m_url, '/');
    }

    if (strncasecmp(m_url, "https://", 8) == 0)
    {
        m_url += 8;
        m_url = strchr(m_url, '/');
    }

    if (!m_url || m_url[0] != '/')
        return BAD_REQUEST;
    // 当url为/时，显示判断界面(当解析请求行的时候，默认的显示的HTML为judge.html)
    if (strlen(m_url) == 1)
        strcat(m_url, "judge.html");
    // 当前解析了行，表示下一步需要解析头部，因此最后返回的不完整的表示code
    m_check_state = CHECK_STATE_HEADER;
    return NO_REQUEST;
}

// 解析http请求的一个头部信息
http_conn::HTTP_CODE http_conn::parse_headers(char *text)
{
    // 在报文中，请求头和空行的处理使用的同一个函数，这里通过判断当前的text首位是不是\0字符，
    // 若是，则表示当前处理的是空行，若不是，则表示当前处理的是请求头。
    if (text[0] == '\0')
    {
        if (m_content_length != 0)
        {
            // 表示当前已经解析完头部信息，下一步对内容进行解析
            m_check_state = CHECK_STATE_CONTENT;
            return NO_REQUEST;
        }
        return GET_REQUEST;
    }
    else if (strncasecmp(text, "Connection:", 11) == 0)
    {
        text += 11;
        text += strspn(text, " \t"); // 跳过空白符
        // 是否采用HTTP长连接
        if (strcasecmp(text, "keep-alive") == 0)
        {
            m_linger = true; // 保持长连接（客户端请求）
        }
    }
    else if (strncasecmp(text, "Content-length:", 15) == 0)
    {
        text += 15;
        text += strspn(text, " \t");
        // 获得文本长度
        m_content_length = atol(text);
    }
    else if (strncasecmp(text, "Host:", 5) == 0)
    {
        text += 5;
        text += strspn(text, " \t");
        // 主机名，比如www.wrox.com
        m_host = text;
    }
    else if (strncasecmp(text, "X-HTTP-Method-Override:", 22) == 0)
    {
        text += 22;
        // 跳过": ""
        text += 2;
        text += strspn(text, " \t");
        // 存储方法覆盖值
        m_method_override = text;
        LOG_INFO("Got method override: %s", text);
    } // 添加分块上传相关头部解析
    else if (strncasecmp(text, "X-Chunk-Number:", 15) == 0)
    {
        text += 15;
        text += strspn(text, " \t");
        chunk_header = atoi(text);
        LOG_INFO("Got chunk number: %d", chunk_header);
    }
    else if (strncasecmp(text, "X-Total-Chunks:", 15) == 0)
    {
        text += 15;
        text += strspn(text, " \t");
        total_header = atoi(text);
        LOG_INFO("Got total chunks: %d", total_header);
    }
    else if (strncasecmp(text, "X-File-Name:", 12) == 0)
    {
        text += 12;
        text += strspn(text, " \t");
        m_upload_filename = text;
        LOG_INFO("Got upload filename: %s", m_upload_filename);
    }
    else if (strncasecmp(text, "X-File-Size:", 12) == 0)
    {
        text += 12;
        text += strspn(text, " \t");
        m_file_size = atol(text);
        LOG_INFO("Got file size: %ld", m_file_size);
    }
    else if (strncasecmp(text, "X-Model-Type:", 13) == 0)
    {
        text += 13;
        text += strspn(text, " \t");
        model_name = text;
        LOG_INFO("Got model name: %ld", model_name);
    }
    else if (strncasecmp(text, "X-IOU-Threshold:", 16) == 0)
    {
        text += 16;
        text += strspn(text, " \t");
        iou_threshold = str2f.robust_stof(text);
        LOG_INFO("Got iou threshold: %lf", iou_threshold);
    }
    else if (strncasecmp(text, "X-Confidence-Threshold:", 23) == 0)
    {
        text += 23;
        text += strspn(text, " \t");
        conf_threshold = str2f.robust_stof(text);
        LOG_INFO("Got confidence threshold: %lf", conf_threshold);
    }
    else if (strncasecmp(text, "X-Image-Size:", 13) == 0)
    {
        text += 13;
        text += strspn(text, " \t");
        // 格式为：640（不是640x640这样格式）
        imageHW = std::string(text);
        LOG_INFO("Got image width and height: %s", imageHW.c_str());
    }
    else if (strncasecmp(text, "Cookie:", 7) == 0)
    {
        // text 形如 "Cookie: a=1; session_id=abcd...; b=2"
        const char *p = text + 7;
        p += strspn(p, " \t");
        const char *sid = strcasestr(p, "session_id=");
        if (sid)
        {
            sid += 11; // 跳过 "session_id="
            size_t n = 0;
            while (sid[n] && sid[n] != ';' && sid[n] != ' ' && n < 64)
                n++;
            // 只接受 32 位十六进制
            if (n == 32)
            {
                bool ok = true;
                // 确保session id是合法的
                for (size_t i = 0; i < 32; ++i)
                {
                    char c = sid[i];
                    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')))
                    {
                        ok = false;
                        break;
                    }
                }
                if (ok)
                {
                    memcpy(m_session_id_buf, sid, 32);
                    m_session_id_buf[32] = '\0';
                    m_has_session = true;
                }
            }
        }
        if (strlen(m_session_id_buf) > 0)
        {
            m_has_session = true;
            m_session_id = std::string(m_session_id_buf);
            sessions_st.insert(m_session_id);
        }
    }
    else
    {
        LOG_INFO("oop!unknow header: %s", text);
    }
    // 当前解析了头部信息，下一步需要对内容进行解析，因此这里返回的是不完整的code表示
    return NO_REQUEST;
}

// 判断http请求是否被完整读入
http_conn::HTTP_CODE http_conn::parse_content(char *text)
{
    if (m_read_idx >= (m_content_length + m_checked_idx))
    {
        text[m_content_length] = '\0';
        // POST请求中最后为输入的用户名和密码
        m_string = text;
        return GET_REQUEST;
    }
    return NO_REQUEST;
}

/*
判断条件
    主状态机转移到CHECK_STATE_CONTENT，该条件涉及解析消息体
    从状态机转移到LINE_OK，该条件涉及解析请求行和请求头部
    两者为或关系，当条件为真则继续循环，否则退出

循环体
    从状态机读取数据
    调用get_line函数，通过m_start_line将从状态机读取数据间接赋给text
    主状态机解析text
*/
http_conn::HTTP_CODE http_conn::process_read()
{
    LINE_STATUS line_status = LINE_OK;
    // 默认是解析当前内容还不完整
    HTTP_CODE ret = NO_REQUEST;
    char *text = 0;
    // m_check_state初始值为CHECK_STATE_REQUESTLINE
    // 需要首先是请求内容，请求行没有问题，读取完整的一行返回LINE_OK，读取的不是完整行返回LINE_OPEN，否则返回LINE_BAD
    while ((m_check_state == CHECK_STATE_CONTENT && line_status == LINE_OK) ||
           ((line_status = parse_line()) == LINE_OK))
    {
        // 获得读缓冲区的位置
        text = get_line();
        // 更新读取的位置
        m_start_line = m_checked_idx;
        // 输出读取的内容
        LOG_INFO("%s", text);
        switch (m_check_state) // 默认初始化解析“行”
        {
        case CHECK_STATE_REQUESTLINE:
        {
            // 对读取的内容进行“行”解析
            ret = parse_request_line(text);
            if (ret == BAD_REQUEST)
                return BAD_REQUEST;
            break;
        }
        case CHECK_STATE_HEADER:
        {
            // 解析头部信息
            ret = parse_headers(text);
            if (ret == BAD_REQUEST)
                return BAD_REQUEST;
            else if (ret == GET_REQUEST)
            {
                // 解析完请求之后就是对浏览器（客户端）的响应
                return do_request();
            }
            break;
        }
        case CHECK_STATE_CONTENT:
        {
            // 解析内容
            ret = parse_content(text);
            if (ret == GET_REQUEST)
            {
                // 解析完请求之后就是对浏览器（客户端）的响应
                return do_request();
            }

            line_status = LINE_OPEN;
            break;
        }
        default:
            return INTERNAL_ERROR; // 服务器内部错误，该结果在主状态机逻辑switch的default下，一般不会触发
        }
    }
    // 请求不完整，需要继续读取请求报文数据
    return NO_REQUEST;
}

bool http_conn::process_image_classification(const char *image_path)
{
    char model_file[160];
    snprintf(model_file, sizeof(model_file), "%s/%s/%s.onnx", doc_root, "model_weights", model_name);
    printf("model path = %s\n", model_file);
    printf("classify image path = %s\n", image_path);

    LOG_INFO("model path = %s\n", model_file);
    LOG_INFO("classify image path = %s\n", image_path);
    // 实例化对象
    Classification cls(std::string(image_path), std::string(model_file), 224, 224, 1, 0);
    try
    {
        // 设置相关属性
        cls.setImagePath(std::string(image_path));
        cls.setMdoelPath(std::string(model_file));
        cls.setImgWH(224, 224);

        // 打开图像
        cls.openImage();
        cls.openModel();
        // 执行推理
        cls.predictImage();

        g_cls = cls;

        printf("pred = %s conf = %lf\n", cls.getPredResult().c_str(), cls.getPredProb());

        LOG_INFO("Image classified: %s (model: %s)", image_path, model_name);
    }
    catch (const std::exception &e)
    {
        std::cerr << e.what() << '\n';
        LOG_INFO("Image classified: %s (model: %s)", image_path, model_name);
    }

    return true;
}

bool http_conn::process_image_objectDetection(const char *image_path)
{
    char model_file[160];
    snprintf(model_file, sizeof(model_file), "%s/%s/%s.onnx", doc_root, "model_weights", model_name);
    printf("model path = %s\n", model_file);
    printf("objectDetect image path = %s\n", image_path);
    printf("IOU Threshold = %lf\n", iou_threshold);
    printf("Conf Threshold = %lf\n", conf_threshold);

    LOG_INFO("model path = %s\n", model_file);
    LOG_INFO("objectDetect image path = %s\n", image_path);
    LOG_INFO("IOU Threshold = %lf\n", iou_threshold);
    LOG_INFO("Conf Threshold = %lf\n", conf_threshold);

    // 解析前端选择的图像大小
    size_t imgH = atol(imageHW.c_str());
    size_t imgW = atol(imageHW.c_str());
    printf("object image h = %ld\n", imgH);
    printf("object image w = %ld\n", imgW);
    LOG_INFO("object image h = %ld\n", imgH);
    LOG_INFO("object image w = %ld\n", imgW);
    // 实例化对象
    ObjectDetection obj(std::string(image_path), std::string(model_file),
                        imgH, imgW, iou_threshold,
                        conf_threshold, 1, 0);
    try
    {
        // 设置相关属性
        obj.setImagePath(std::string(image_path));
        obj.setMdoelPath(std::string(model_file));
        obj.setImgWH(imgH, imgW);

        obj.openImage();
        obj.openModel();

        // 执行推理
        obj.predictImage();

        // obj.encodeImage(obj.getImage());

        g_obj = obj;

        LOG_INFO("Image detect: %s (model: %s)", image_path, model_name);
    }
    catch (const std::exception &e)
    {
        std::cerr << e.what() << '\n';
        LOG_INFO("Image detect: %s (model: %s)", image_path, model_name);
    }

    return true;
}

bool http_conn::process_image_segmentation(const char *image_path)
{
    char model_file[160];
    snprintf(model_file, sizeof(model_file), "%s/%s/%s.onnx", doc_root, "model_weights", model_name);
    printf("model path = %s\n", model_file);
    printf("segmentation image path = %s\n", image_path);

    LOG_INFO("model path = %s\n", model_file);
    LOG_INFO("segmentation image path = %s\n", image_path);

    // 解析前端选择的图像大小
    size_t imgH = atol(imageHW.c_str());
    size_t imgW = atol(imageHW.c_str());
    printf("segmentation image h = %ld\n", imgH);
    printf("segmentation image w = %ld\n", imgW);
    LOG_INFO("segmentation image h = %ld\n", imgH);
    LOG_INFO("segmentation image w = %ld\n", imgW);
    // 实例化对象
    Segmentation seg(std::string(image_path), std::string(model_file),
                     imgH, imgW, 1, 0);
    try
    {
        // 设置相关属性
        seg.setImagePath(std::string(image_path));
        seg.setMdoelPath(std::string(model_file));
        seg.setImgWH(imgH, imgW);

        seg.openImage();
        seg.openModel();

        printf("open model  is success!\n");
        // 执行推理
        seg.predictImage();

        g_seg = seg;

        LOG_INFO("Image detect: %s (model: %s)", image_path, model_name);
    }
    catch (const std::exception &e)
    {
        std::cerr << e.what() << '\n';
        LOG_INFO("Image detect: %s (model: %s)", image_path, model_name);
    }

    return true;
}

http_conn::HTTP_CODE http_conn::do_request()
{
    // 上传文件模块
    UploadFile up_file(this->doc_root, this->m_close_log);

    // 服务端路径
    strcpy(m_real_file, doc_root);
    int len = strlen(doc_root);
    // printf("m_url:%s\n", m_url);
    //  用于查找字符在字符串中的最后一次出现位置，因为这个是反着查找‘/’位置的(也就是我们要请求的内容)
    const char *p = strrchr(m_url, '/');

    /*
    HTML表单的限制：
        HTML标准表单(<form>)只正式支持GET和POST方法
        即使您写method="put"，浏览器会自动转换为GET请求
        这是HTML规范的历史遗留限制
    */

    METHOD actual_method = m_method;
    if (m_method_override != nullptr)
    {
        // 使用临时字符串比较，避免直接操作可能无效的指针
        std::string override_val(m_method_override);
        if (strcasecmp(override_val.c_str(), "PUT") == 0)
        {
            actual_method = PUT;
        }
    }

    // 在do_request()开始处添加
    printf("------------------ Request Debug -----------------\n");
    printf("Method: %d\n", m_method); // 或转换为字符串显示
    printf("actual method: %d\n", actual_method);
    printf("method override: %s\n", m_method_override);
    printf("URL: %s\n", m_url);
    printf("Content-Length: %d\n", m_content_length);
    printf("has session: %d\n", m_has_session);
    printf("--------------------------------------------------\n");

    // 如果是携带了cookie的请求，首先进行校验
    if (m_has_session && sessions.find(m_session_id) != sessions.end())
    {
        std::string session_id(m_session_id_buf);
        printf("%s %d session id = %s\n", __FILE__, __LINE__, m_session_id.c_str());
        if (validate_session(session_id))
        {
            m_is_logged_in = true;
        }
        else
        {
            // 验证失败时的处理
            m_is_logged_in = false;
            // 1. 清除无效的session
            destroy_session(session_id);
            // 2. 清除客户端的cookie (设置过期时间为过去)
            add_response("Set-Cookie: session_id=; Path=/; Expires=Thu, 01-Jan-1970 00:00:00 GMT\r\n");
            // 3. 可以重定向到登录页面并显示提示信息
            strcpy(m_url, "/logError.html");
            // 4. 记录日志
            LOG_INFO("Invalid session attempt: %s", session_id.c_str());
        }
    }

    // 在do_request()的最前面添加上传文件：
    if (actual_method == PUT && (*(p + 1) == '8' || (*(p + 1) == '1' && *(p + 2) == '0') || (*(p + 1) == '1' && *(p + 2) == '1') || (*(p + 1) == '1' && *(p + 2) == '2')))
    {
        char *filename = NULL;
        if (*(p + 1) == '8')
        {
            filename = m_url + 2;
            printf("general upload file\n");
        }
        else if (*(p + 1) == '1' && *(p + 2) == '0')
        {
            filename = m_url + 3;
            printf("classify upload file\n");
        }
        else if (*(p + 1) == '1' && *(p + 2) == '1')
        {
            filename = m_url + 3;
            printf("object detect file\n");
        }
        else if ((*(p + 1) == '1' && *(p + 2) == '2'))
        {
            filename = m_url + 3;
            printf("segmentation file\n");
        }

        // 获取分块信息头：块的大小以及总的块数量
        int chunk_num = chunk_header, total_chunks = total_header;

        // 保存分块或完整文件
        bool save_result = false;
        bool is_merge_file = false;
        if (total_chunks > 1)
        {
            // 分块上传文件
            save_result = up_file.save_uploaded_chunk(filename, m_string, m_content_length,
                                                      chunk_num, total_chunks);

            // 如果是最后一个分块，合并文件
            if (save_result && chunk_num == total_chunks - 1)
            {
                save_result = up_file.merge_uploaded_file(filename, total_chunks);
                is_merge_file = true;
            }
        }
        else
        {
            // 单块直接保存
            save_result = up_file.save_uploaded_file(filename, m_string, m_content_length);
        }

        // 如果是图像分类,并且等图像完整的上传完整之后，那么还需要进行分类检测
        if (this->model_name != NULL && save_result)
        {
            printf("model name = %s is merge file = %d\n", this->model_name, is_merge_file);
            // 图像分类
            if ((is_merge_file || total_chunks <= 1) && (*(p + 1) == '1' && *(p + 2) == '0'))
            {
                char image_file[300];
                snprintf(image_file, sizeof(image_file), "%s/%s/%s", doc_root, "uploads", filename);
                process_image_classification(image_file);
                is_response_result = true;
                is_merge_file = false;
            }
            // 目标检测
            else if ((is_merge_file || total_chunks <= 1) && (*(p + 1) == '1' && *(p + 2) == '1'))
            {
                char image_file[300];
                snprintf(image_file, sizeof(image_file), "%s/%s/%s", doc_root, "uploads", filename);
                process_image_objectDetection(image_file);

                try
                {

                    snprintf(save_path, sizeof(save_path), "%s/%s/%s", doc_root, "outputs", filename);

                    // 获得结果图像（坐标框绘制之后的结果）
                    cv::Mat Image = g_obj.getImageObj();
                    // 获得原始图像大小
                    pair<size_t, size_t> org_img_hw = g_obj.getOrgImgHW();
                    // 将图像从(640, 640)还原回原始图像大小
                    cv::resize(Image, Image, cv::Size(org_img_hw.second, org_img_hw.first));

                    printf("image wh = %ld, %ld\n", org_img_hw.first, org_img_hw.second);
                    cv::imwrite(save_path, Image);
                }
                catch (const std::exception &e)
                {
                    std::cerr << e.what() << '\n';
                }

                is_objectDetect = true;
                is_merge_file = false;
            }
            else if ((is_merge_file || total_chunks <= 1) && (*(p + 1) == '1' && *(p + 2) == '2'))
            { // 语义分割
                char image_file[300];
                snprintf(image_file, sizeof(image_file), "%s/%s/%s", doc_root, "uploads", filename);
                process_image_segmentation(image_file);

                try
                {

                    snprintf(save_path, sizeof(save_path), "%s/%s/%s", doc_root, "outputs", filename);

                    // 获得结果图像（坐标框绘制之后的结果）
                    cv::Mat Image = g_seg.getImageObj();
                    // 获得原始图像大小
                    pair<size_t, size_t> org_img_hw = g_seg.getOrgImgHW();
                    // 将图像从(640, 640)还原回原始图像大小
                    cv::resize(Image, Image, cv::Size(org_img_hw.second, org_img_hw.first));

                    printf("image wh = %ld, %ld\n", org_img_hw.first, org_img_hw.second);
                    cv::imwrite(save_path, Image);
                }
                catch (const std::exception &e)
                {
                    std::cerr << e.what() << '\n';
                }

                is_segmentation = true;
                is_merge_file = false;
            }
        }
        // 清理分块上传文件保存的哪些文件信息
        if (is_merge_file || total_chunks <= 1)
        {
            up_file.cleanup_chunks();
        }
    }
    else if (cgi == 1 && (*(p + 1) == '2' || *(p + 1) == '3'))
    {
        // 处理cgi（是否启用POST）
        // 根据标志判断是登录检测还是注册检测
        char flag = m_url[1];

        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/");
        strcat(m_url_real, m_url + 2);
        // 复制指定长度的字符串
        strncpy(m_real_file + len, m_url_real, FILENAME_LEN - len - 1);
        free(m_url_real);

        // 将用户名和密码提取出来
        // user=123&passwd=123
        char name[100], password[100];
        int i;
        for (i = 5; m_string[i] != '&'; ++i)
            name[i - 5] = m_string[i];
        name[i - 5] = '\0';

        int j = 0;
        for (i = i + 10; m_string[i] != '\0'; ++i, ++j)
            password[j] = m_string[i];
        password[j] = '\0';

        if (*(p + 1) == '3')
        {
            // 如果是注册，先检测数据库中是否有重名的
            // 没有重名的，进行增加数据
            char *sql_insert = (char *)malloc(sizeof(char) * 200);
            strcpy(sql_insert, "INSERT INTO user(username, passwd) VALUES(");
            strcat(sql_insert, "'");
            strcat(sql_insert, name);
            strcat(sql_insert, "', '");
            strcat(sql_insert, password);
            strcat(sql_insert, "')");

            if (users.find(name) == users.end())
            {
                m_lock.lock();
                int res = mysql_query(mysql, sql_insert);
                users.insert(pair<std::string, std::string>(name, password));
                m_lock.unlock();

                // 判断当前是否请求成功，请求成功就进入登录界面
                if (!res)
                    strcpy(m_url, "/log.html");
                else
                    strcpy(m_url, "/registerError.html");
            }
            else
                strcpy(m_url, "/registerError.html");
        }
        // 如果是登录，直接判断
        // 若浏览器端输入的用户名和密码在表中可以查找到，返回1，否则返回0
        else if (*(p + 1) == '2')
        {
            // if (users.find(name) != users.end() && users[name] == password)
            //     strcpy(m_url, "/welcome.html");
            // else
            //     strcpy(m_url, "/logError.html");

            if (users.find(name) != users.end() && users[name] == password)
            {
                // 登录成功，创建session id
                if (create_session(name))
                {
                    printf("%s %d session id = %s\n", __FILE__, __LINE__, m_session_id.c_str());
                    strcpy(m_url, "/welcome.html");
                }
                else
                {
                    strcpy(m_url, "/logError.html");
                }
            }
            else
            {
                strcpy(m_url, "/logError.html");
            }
        }
    }
    if (*(p + 1) == '0')
    {
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        // 进入注册页面进程注册
        strcpy(m_url_real, "/register.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

        free(m_url_real);
    }
    else if (*(p + 1) == '1')
    {
        if (*(p + 2) == '0')
        {
            // printf("%s len = %d\n", filename, strlen(filename));
            char *m_url_real = (char *)malloc(sizeof(char) * 200);
            strcpy(m_url_real, "/classification.html");
            strncpy(m_real_file + len, m_url_real, FILENAME_LEN - len - 1);
            free(m_url_real);
        }
        else if (*(p + 2) == '1')
        {
            char *m_url_real = (char *)malloc(sizeof(char) * 200);
            strcpy(m_url_real, "/objectDetection.html");
            strncpy(m_real_file + len, m_url_real, FILENAME_LEN - len - 1);
            free(m_url_real);
        }
        else if (*(p + 2) == '2')
        {
            char *m_url_real = (char *)malloc(sizeof(char) * 200);
            strcpy(m_url_real, "/segmentation.html");
            strncpy(m_real_file + len, m_url_real, FILENAME_LEN - len - 1);
            free(m_url_real);
        }
        else
        {
            char *m_url_real = (char *)malloc(sizeof(char) * 200);
            strcpy(m_url_real, "/log.html");
            strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

            free(m_url_real);
        }
    }
    else if (*(p + 1) == '5' && m_is_logged_in)
    {
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/picture.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

        free(m_url_real);
    }
    else if (*(p + 1) == '6' && m_is_logged_in)
    {
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/video.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

        free(m_url_real);
    }
    else if (*(p + 1) == '7' && m_is_logged_in)
    {
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/fans.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

        free(m_url_real);
    }
    else if (*(p + 1) == '8')
    {
        // 文件上传路由 (PUT /8filename)
        // printf("Debug: Serving upload page\n");  // 调试点1
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/upload.html");
        strncpy(m_real_file + len, m_url_real, FILENAME_LEN - len - 1);
        // printf("Real file path: %s\n", m_real_file);  // 调试点2
        printf("upload = %s\n", "route/8");
        free(m_url_real);
    }
    else if (*(p + 1) == '9')
    {
        // 跳过 "/9"
        const char *filename = m_url + 2;

        // 1. 处理下载页面请求
        if (strstr(filename, "download.html") != nullptr)
        {
            char *m_url_real = (char *)malloc(sizeof(char) * 200);
            strcpy(m_url_real, "/download.html");
            strncpy(m_real_file + len, m_url_real, FILENAME_LEN - len - 1);
            free(m_url_real);
        }
        else
        {
            // 2. 处理实际文件下载（前端点击选择下载文件请求，然后走这条路由）
            char filepath[FILENAME_LEN];
            snprintf(filepath, sizeof(filepath), "%s/uploads/%s", doc_root, filename);

            if (stat(filepath, &m_file_stat) < 0)
            {
                if (errno == ENOENT)
                {
                    LOG_ERROR("File not found: %s", filepath);
                }
                else
                {
                    LOG_ERROR("Cannot access file %s: %s", filepath, strerror(errno));
                }
                return NO_RESOURCE;
            }

            // 检查是否是目录
            if (S_ISDIR(m_file_stat.st_mode))
            {
                LOG_ERROR("Path is a directory: %s", filepath);
                return BAD_REQUEST;
            }

            strcpy(m_real_file, filepath);
            // 添加下载头[用于控制客户端（如浏览器）如何处理服务器返回的内容，特别是在文件下载场景]
            // add_content_disposition(filename);
            m_upload_filename = const_cast<char *>(filename);
            // add_content_type();
            printf("download file path: %s\n", filepath);
        }
    }
    else if (*(p + 1) == 'a')
    {
        // 处理文件列表请求
        // 文件列表路由 (GET /10)
        // 使用a(ASCII 97)表示列表
        // /home/ubuntu/Documents/KTG/myPro/myProject/myTinyWebServer-v2/root
        // printf("Accessing directory: %s\n", m_real_file);
        DIR *dir = opendir(strcat(m_real_file, "/uploads"));
        if (!dir)
            return NO_RESOURCE;

        // 将列举出来的目录结果作为响应内容发送给浏览器（客户端）给显示出来
        struct dirent *entry;
        std::string json = "[";
        while ((entry = readdir(dir)) != NULL)
        {
            if (entry->d_type == DT_REG)
            {
                if (json.size() > 1)
                    json += ",";

                std::string fullpath = std::string(m_real_file) + "/" + entry->d_name;
                struct stat fileStat;
                stat(fullpath.c_str(), &fileStat);

                char dateBuf[64];
                strftime(dateBuf, sizeof(dateBuf), "%Y-%m-%d", localtime(&fileStat.st_mtime));

                json += "{";
                json += "\"name\":\"" + std::string(entry->d_name) + "\",";
                json += "\"size\":" + std::to_string(fileStat.st_size) + ",";
                json += "\"lastModified\":\"" + std::string(dateBuf) + "\"";
                json += "}";
            }
        }
        json += "]";
        closedir(dir);

        add_status_line(200, ok_200_title);
        add_headers(json.size());
        add_blank_line();
        add_content(json.c_str());
        return NO_RESOURCE;
    }
    else if (*(p + 1) == 'b')
    {
        // 添加一个登出功能，添加登出路由(既然登出之后，那么对应的cookie也就没有必要保存了)
        if (!m_session_id.empty())
        {
            // 销毁session并清除cookie
            destroy_session(m_session_id);
            // 添加清除cookie的头部
            add_response("Set-Cookie: session_id=; Path=/; Expires=Thu, 01-Jan 1970 00:00:00 GMT\r\n");
        }
        strcpy(m_url, "/log.html");
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/log.html");
        strncpy(m_real_file + len, m_url_real, FILENAME_LEN - len - 1);
        free(m_url_real);
    }
    else
        strncpy(m_real_file + len, m_url, FILENAME_LEN - len - 1);

    printf("%s %d session id = %s\n", __FILE__, __LINE__, m_session_id.c_str());
    // 打开文件
    printf("m_real_file: %s\n", m_real_file);

    // 如果是目标检测的话，就将最后的检测结果图响应给浏览器渲染出来
    if (is_objectDetect || is_segmentation)
    {
        strcpy(m_real_file, save_path);
        save_path[0] = '\0';
    }

    // 如果stat()返回负数（通常为-1），表示文件不存在或不可访问，返回NO_RESOURCE错误;
    // 如果是目录的话也会返回 >=0，并不一定是具有后缀名的文件
    if (stat(m_real_file, &m_file_stat) < 0)
        return NO_RESOURCE;
    // 检查文件读写权限（S_IROTH表示其他用户可读）
    if (!(m_file_stat.st_mode & S_IROTH))
        return FORBIDDEN_REQUEST;
    // 禁止目录访问（判断是否为目录）
    if (S_ISDIR(m_file_stat.st_mode))
    {
        printf("%s %d %s\n", __FILE__, __LINE__, "here --->");
        return BAD_REQUEST;
    }

    int fd = open(m_real_file, O_RDONLY);
    /*
        打开文件并内存映射
            0：让系统自动选择映射地址
            m_file_stat.st_size：映射的文件大小
            PROT_READ：只允许读操作
            MAP_PRIVATE：创建私有映射（修改不写回文件）
            fd：文件描述符
            0：偏移量（从文件开头映射）
        返回值：映射到内存的起始地址，赋值给m_file_address
    */
    m_file_address = (char *)mmap(0, m_file_stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    return FILE_REQUEST;
}
void http_conn::unmap()
{
    if (m_file_address)
    {
        // 释放内存映射区
        munmap(m_file_address, m_file_stat.st_size);
        m_file_address = 0;
    }
}
bool http_conn::write()
{
    int temp = 0;

    if (bytes_to_send == 0)
    {
        // 修改当前fd在epoll上的状态（读）
        modfd(m_epollfd, m_sockfd, EPOLLIN, m_TRIGMode);
        init();
        return true;
    }

    // 其实这里发送信息也可以考虑使用“零拷贝技术”sendfile来实现
    while (1)
    {
        // 使用 writev() 替代多次 write()，减少系统调用次数。
        // m_iv 本身不存储数据，它只是记录了多个数据块的位置和长度
        // 调用系统IO写操作（系统将 m_iv 描述的分散数据（iovec 结构数组）拷贝到内核的 套接字发送缓冲区）
        temp = writev(m_sockfd, m_iv, m_iv_count);

        if (temp < 0)
        {
            // 数据发送完毕之后修改当前socket fd为监听写事件
            if (errno == EAGAIN) // 该信号表示try  again，因为可能数据没有写完或者缓冲区满，因此需要epoll继续进行监听
            {
                // 如果内核缓冲区已满，可能阻塞（默认行为）或返回部分写入（非阻塞模式）
                modfd(m_epollfd, m_sockfd, EPOLLOUT, m_TRIGMode);
                return true;
            }
            unmap(); // 文件发送完毕之后，关闭文件共享映射区
            return false;
        }

        // 当前已发送的字节数
        bytes_have_send += temp;
        // 剩余多少字节数未发
        bytes_to_send -= temp;

        // 如果当前已发送的字节数大于了之前指定的m_write_idx位置，也就是头部信息发送完成，现在就是包体内容需要发送了
        if (bytes_have_send >= m_iv[0].iov_len)
        {
            m_iv[0].iov_len = 0;
            // 从共享文件映射区的地址的位置开始拷贝
            m_iv[1].iov_base = m_file_address + (bytes_have_send - m_write_idx);
            m_iv[1].iov_len = bytes_to_send;
        }
        else
        {
            // 否则移动指针的位置
            m_iv[0].iov_base = m_write_buf + bytes_have_send;
            // 剩余要发送的字节数
            m_iv[0].iov_len = m_iv[0].iov_len - bytes_have_send;
        }

        // 发送完成数据
        if (bytes_to_send <= 0)
        {
            unmap();
            modfd(m_epollfd, m_sockfd, EPOLLIN, m_TRIGMode);

            // 是否优雅的关闭
            if (m_linger)
            {
                init();
                return true;
            }
            else
            {
                return false;
            }
        }
    }
}
bool http_conn::add_response(const char *format, ...)
{
    // 如果写入位置大于了写缓冲区大小就直接返回false
    if (m_write_idx >= WRITE_BUFFER_SIZE)
    {
        return false;
    }

    va_list arg_list;
    va_start(arg_list, format);
    // 给写文件缓冲区添加响应信息
    int len = vsnprintf(m_write_buf + m_write_idx, WRITE_BUFFER_SIZE - 1 - m_write_idx,
                        format, arg_list);
    // 如果当前内容长度大于了写入缓冲区的大小，就直接返回false
    if (len >= (WRITE_BUFFER_SIZE - 1 - m_write_idx))
    {
        va_end(arg_list);
        return false;
    }
    m_write_idx += len;
    va_end(arg_list);

    LOG_INFO("request:%s", m_write_buf);

    return true;
}
bool http_conn::add_status_line(int status, const char *title)
{
    return add_response("%s %d %s\r\n", "HTTP/1.1", status, title);
}
bool http_conn::add_headers(int content_len)
{
    return add_content_length(content_len) && add_linger();
}
bool http_conn::add_content_length(int content_len)
{
    return add_response("Content-Length:%d\r\n", content_len);
}
bool http_conn::add_content_type()
{
    return add_response("Content-Type:%s\r\n", "text/html");
}
bool http_conn::add_linger()
{
    return add_response("Connection:%s\r\n", (m_linger == true) ? "keep-alive" : "close");
}
bool http_conn::add_blank_line()
{
    return add_response("%s", "\r\n"); // 添加分隔符
}
bool http_conn::add_content(const char *content)
{
    return add_response("%s", content); // 添加内容
}
bool http_conn::add_content_disposition(const char *filename)
{
    return add_response("Content-Disposition: attachment; filename=\"%s\"\r\n", filename);
}
// 响应报文的内容
bool http_conn::process_write(HTTP_CODE ret)
{
    switch (ret)
    {
    case INTERNAL_ERROR: // 服务器内部错误
    {
        printf("INTERAL_ERROR---->\n");
        add_status_line(500, error_500_title);
        add_headers(strlen(error_500_form));
        // 判断添加的内容是否超过了指定缓冲区大小
        if (!add_content(error_500_form))
            return false;
        break;
    }
    case BAD_REQUEST: // 错误请求
    {
        printf("BAD_REQUEST---->\n");
        add_status_line(404, error_404_title);
        add_headers(strlen(error_404_form));
        // 判断添加的内容是否超过了指定缓冲区大小
        if (!add_content(error_404_form))
            return false;
        break;
    }
    case FORBIDDEN_REQUEST: // 禁止访问
    {
        printf("FORBIDDEN_REQUEST---->\n");
        add_status_line(403, error_403_title);
        add_headers(strlen(error_403_form));
        // 判断添加的内容是否超过了指定缓冲区大小
        if (!add_content(error_403_form))
            return false;
        break;
    }
    case FILE_REQUEST: // 文件请求
    {
        printf("FILE_REQUEST---->\n");
        add_status_line(200, ok_200_title);
        // 如果是下载文件，添加Content-Disposition头
        if (m_upload_filename != NULL)
        {
            add_content_disposition(m_upload_filename);
            m_upload_filename = NULL;
        }
        // 只有当浏览器第一次请求的消息中没有cookie时，服务器会生成一个cookie，那么这个时候就需要去设置cookie
        if (m_need_set_cookie)
        {
            add_response("Set-Cookie: session_id=%s; Path=/; HttpOnly; SamaSite=Lax\r\n", m_session_id.c_str());
            m_need_set_cookie = false;
        }

        // 文件大小不为0（确实有信息需要发送）
        if (m_file_stat.st_size != 0)
        {
            add_headers(m_file_stat.st_size);
            // 针对图像分类
            if (is_objectDetect == false && is_segmentation == false)
            {
                // 填写自定义字段（一定要注意响应的格式： 状态行 → 标准头字段 → CORS字段 → 自定义字段）
                // 针对分类
                if (model_name && is_response_result)
                {
                    // 4. 关键改进：将关键数据同时放入头部
                    printf("add classification result to header\n");
                    add_response("X-Model-Used:%s\r\n", model_name);
                    add_response("X-Top-Class:%s\r\n", g_cls.getPredResult().c_str());
                    add_response("X-Confidence:%s\r\n", std::to_string(g_cls.getPredProb()).c_str());
                    add_response("X-Inference-Time:%s\r\n", std::to_string(g_cls.getInferTime()).c_str());

                    // 5. 可选：添加调试信息
                    add_response("X-Predictions-Count:%s\r\n", "1");
                    model_name = NULL;
                }
            }
            // 针对目标检测结果返回
            else if (is_objectDetect)
            {
                add_content_type();
                add_response("X-Model-Used:%s\r\n", model_name);
                add_response("X-Inference-Time:%s\r\n", std::to_string(g_obj.getInferTime()).c_str());
                add_response("X-Detect-Count:%s\r\n", std::to_string(g_obj.getDetectCount()).c_str());

                // 内容
                is_objectDetect = false;
                model_name = NULL;
            }
            else if (is_segmentation)
            {
                add_content_type();
                add_response("X-Model-Used:%s\r\n", model_name);
                add_response("X-Inference-Time:%s\r\n", std::to_string(g_obj.getInferTime()).c_str());
                is_segmentation = false;
                model_name = NULL;
            }
            // 空行
            add_blank_line();
            // printf("process write...");
            // 第一个iovec指针指向响应报文缓冲区，长度指向m_write_idx
            m_iv[0].iov_base = m_write_buf;
            m_iv[0].iov_len = m_write_idx;

            // 第二个iovec指针指向mmap返回的文件指针(比如.html文件之类的文件内容)，长度指向文件大小
            m_iv[1].iov_base = m_file_address;
            m_iv[1].iov_len = m_file_stat.st_size;
            m_iv_count = 2;
            // 发送的全部数据为响应报文头部信息和文件大小
            bytes_to_send = m_write_idx + m_file_stat.st_size;
            printf("m_file_stat.size: %d\n", m_file_stat.st_size);

            return true;
        }
        else
        {
            // 没有信息要发送，那么就简单拼接一个字符串发送过去
            const char *ok_string = "<html><body></body></html>";
            add_headers(strlen(ok_string));
            if (!add_content(ok_string))
                return false;
        }
    }
    default:
        printf("default ---> \n");
        // return false;
        break;
    }

    m_iv[0].iov_base = m_write_buf;
    m_iv[0].iov_len = m_write_idx;
    m_iv_count = 1;
    bytes_to_send = m_write_idx;
    return true;
}
void http_conn::process()
{
    HTTP_CODE read_ret = process_read();
    // 如果解析的请求消息不完整就需要继续解析，修改当前的socket fd依然是监听读事件操作
    if (read_ret == NO_REQUEST)
    {
        modfd(m_epollfd, m_sockfd, EPOLLIN, m_TRIGMode);
        return;
    }
    // printf("read ret = %d\n", read_ret);
    bool write_ret = process_write(read_ret);
    // 如果读取出现问题，则关闭连接，并且注册写事件到epoll上，表示需要继续监听当前写事件
    if (!write_ret)
    {
        close_conn();
    }
    modfd(m_epollfd, m_sockfd, EPOLLOUT, m_TRIGMode);
}
