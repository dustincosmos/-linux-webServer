#include "http_conn.h"

// http响应的一些状态信息
const char *ok_200_title = "OK";
const char *error_400_title = "Bad Request";
const char *error_400_form = "Your request has bad syntax or is inherently impossible to staisfy.\n";
const char *error_403_title = "Forbidden";
const char *error_403_form = "You do not have permission to get file form this server.\n";
const char *error_404_title = "Not Found";
const char *error_404_form = "The requested file was not found on this server.\n";
const char *error_500_title = "Internal Error";
const char *error_500_form = "There was an unusual problem serving the request file.\n";

// #define connfdLT
#define connfdET

// #define listenfdLT
#define listenfdET

int http_conn::m_epollfd = -1;
int http_conn::m_user_count = 0;

const char *doc_root = "/home/kun/webserver/rescources";

// 设置文件描述符非阻塞
int setnoblocking(int fd)
{
    int old_flag = fcntl(fd, F_GETFL);
    int new_flag = old_flag | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_flag);
    return old_flag;
}

// 向epoll添加需要监听的文件描述符
void addfd(int epollfd, int fd, bool one_shot)
{
    epoll_event event;
    event.data.fd = fd;
    // event.events = EPOLLIN | EPOLLRDHUP;
    event.events = EPOLLIN | EPOLLRDHUP;
    if (one_shot)
        event.events | EPOLLONESHOT;
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    setnoblocking(fd);
}

// 从epoll删除文件描述符
void removefd(int epollfd, int fd)
{
    epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, 0);
    close(fd);
}

// 修改文件描述符，重置oneshot事件
void modfd(int epollfd, int fd, int ev)
{
    epoll_event event;
    event.data.fd = fd;
    event.events = ev | EPOLLONESHOT | EPOLLRDHUP;
    epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &event);
}

// 初始化连接
void http_conn::init(int sockerfd, const sockaddr_in &addr)
{
    m_socketfd = sockerfd;
    m_address = addr;

    // 设置端口复用
    int reuse = 1;
    setsockopt(m_socketfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    // 添加到epoll对象
    addfd(m_epollfd, m_socketfd, true);
    m_user_count++;

    init();
}

void http_conn::init()
{
    m_check_state = CHECK_STATE_REQUESTLINE;
    m_checked_index = 0;
    m_start_line = 0;
    m_method = GET;
    m_url = 0;
    m_version = 0;
    m_link = 0;
    m_host = 0;
    m_content_length = 0;
    m_read_idx = 0;
    m_write_idx = 0;

    bzero(m_read_buffer, READ_BUFFER_SIZE);
    bzero(m_write_buffer, WRITE_BUFFER_SIZE);
    bzero(m_real_file, FILENAME_LEN);
}

// 关闭连接
void http_conn::close_conn()
{
    if (m_socketfd != -1)
    {
        removefd(m_epollfd, m_socketfd);
        m_socketfd = -1;
        m_user_count--;
    }
}

// 一次性把数据读完
bool http_conn::read()
{
    if (m_read_idx > READ_BUFFER_SIZE)
        return false;
    // 读取到的字节
    int byte_read = 0;
#ifdef connfdET
    while (true)
    {
        byte_read = recv(m_socketfd, m_read_buffer + m_read_idx, READ_BUFFER_SIZE - m_read_idx, 0);
        if (byte_read == -1)
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                break;
            return false;
        }
        else if (byte_read == 0)
            return false;
        m_read_idx += byte_read;
    }
    printf("读取的数据:\n%s", m_read_buffer);
    return true;
#endif
}

// 解析http请求 主状态机  解析读入内容
http_conn::HTTP_CODE http_conn::process_read()
{
    LINE_STATE line_state = LINE_OK;
    HTTP_CODE ret = NO_REQUEST;

    char *text = 0;
    while (((m_check_state == CHECK_STATE_CONTENT) && (line_state == LINE_OK)) || ((line_state = parse_line()) == LINE_OK))
    {
        // 获取一行数据
        text = get_line();
        m_start_line = m_checked_index;
        printf("got one line:%s\n", text);
        switch (m_check_state)
        {
        case CHECK_STATE_REQUESTLINE:
        {
            ret = parse_request_line(text);
            if (ret == BAD_REQUEST)
                return BAD_REQUEST;
            break;
        }
        case CHECK_STATE_HEADER:
        {
            ret = parse_headers(text);
            if (ret == BAD_REQUEST)
                return BAD_REQUEST;
            else if (ret == GET_REQUEST)
                return do_request();
            break;
        }
        case CHECK_STATE_CONTENT:
        {
            ret = parse_content(text);
            if (ret == GET_REQUEST)
                return do_request();
            line_state = LINE_OPEN;
            break;
        }
        default:
        {
            return INTERNAL_ERROR;
            break;
        }
        }
    }
    return INTERNAL_ERROR; // 为了消去警告，无他
}

// 将 HTTP 响应添加到写缓冲区中
bool http_conn::add_response(const char *format, ...)
{
    if (m_write_idx >= WRITE_BUFFER_SIZE)
        return false;
    va_list arg_list;
    va_start(arg_list, format);
    int len = vsnprintf(m_write_buffer + m_write_idx, WRITE_BUFFER_SIZE - 1 - m_write_idx, format, arg_list);
    if (len >= (WRITE_BUFFER_SIZE - 1 - m_write_idx))
    {
        va_end(arg_list);
        return false;
    }
    m_write_idx += len;
    va_end(arg_list);

    return true;
}

//
bool http_conn::add_state_line(int state, const char *title)
{
    return add_response("%s %d %s\r\n", "HTTP/1.1", state, title);
}

//
bool http_conn::add_headers(int content_len)
{
    return add_content_length(content_len) && add_link() && add_blank_line();
}

//
bool http_conn::add_content_length(int content_len)
{
    return add_response("Content-Length:%d\r\n", content_len);
}

//
bool http_conn::add_content_type()
{
    return add_response("Content-Type:%s\r\n", "text/html");
}

//
bool http_conn::add_link()
{
    return add_response("Connection:%s\r\n", (m_link == true) ? "keep-alive" : "close");
}

//
bool http_conn::add_blank_line()
{
    return add_response("%s", "\r\n");
}

//
bool http_conn::add_content(const char *content)
{
    return add_response("%s", content);
}

//
bool http_conn::process_write(HTTP_CODE ret)
{
    printf("jinlaile\n");
    switch (ret)
    {
    case INTERNAL_ERROR:
    {
        add_state_line(500, error_500_title);
        add_headers(strlen(error_500_form));
        if (!add_content(error_500_form))
            return false;
        break;
    }
    case BAD_REQUEST:
    {
        add_state_line(404, error_404_title);
        add_headers(strlen(error_404_form));
        if (!add_content(error_404_form))
            return false;
        break;
    }
    case FORBIDDEN_REQUEST:
    {
        add_state_line(403, error_403_title);
        add_headers(strlen(error_403_form));
        if (!add_content(error_403_form))
            return false;
        break;
    }
    case FILE_REQUEST:
    {
        add_state_line(200, ok_200_title);
        if (m_file_state.st_size != 0)
        {
            add_headers(m_file_state.st_size);
            m_iv[0].iov_base = m_write_buffer;
            m_iv[0].iov_len = m_write_idx;
            m_iv[1].iov_base = m_file_address;
            m_iv[1].iov_len = m_file_state.st_size;
            m_iv_count = 2;
            return true;
        }
    }
    default:
        return false;
    }
    printf("write_buffer : %s\n", m_write_buffer);
    return false;
}

// 解析http请求行,请求方法，url，http版本
http_conn::HTTP_CODE http_conn::parse_request_line(char *text)
{
    // GET /index.html HTTP/1.1
    m_url = strpbrk(text, " \t");
    *m_url++ = '\0';
    char *method = text;
    if (strcasecmp(method, "GET") == 0)
        m_method = GET;
    else
        return BAD_REQUEST;
    m_version = strpbrk(m_url, " \t");
    if (!m_version)
        return BAD_REQUEST;
    *m_version++ = '\0';
    if (strcasecmp(m_version, "HTTP/1.1") != 0)
        return BAD_REQUEST;
    if (strncasecmp(m_url, "http://", 7) == 0)
    {
        m_url += 7;
        m_url = strchr(m_url, '/');
    }
    if (!m_url || m_url[0] != '/')
        return BAD_REQUEST;
    m_check_state = CHECK_STATE_HEADER;
    return NO_REQUEST;
}

// 解析http请求头
http_conn::HTTP_CODE http_conn::parse_headers(char *text)
{
    if (text[0] == '\0')
    {
        if (m_content_length != 0)
        {
            m_check_state = CHECK_STATE_CONTENT;
            return NO_REQUEST;
        }
        return GET_REQUEST;
    }
    else if (strncasecmp(text, "Connection:", 11) == 0)
    {
        text += 11;
        text += strspn(text, "\t");
        if (strcasecmp(text, "keep-alive") == 0)
        {
            m_link = true;
        }
        return NO_REQUEST;
    }
    else if (strncasecmp(text, "Content-Length:", 15) == 0)
    {
        text += 15;
        text += strspn(text, "\t");
        m_content_length = atol(text);
    }
    else if (strncasecmp(text, "Host:", 5) == 0)
    {
        text += 5;
        text += strspn(text, "\t");
        m_host = text;
    }
    else
    {
        printf("oop! unkonwn header%s\n", text);
    }
    return NO_REQUEST;
}

// 解析http请求体
http_conn::HTTP_CODE http_conn::parse_content(char *text)
{
    if (m_read_idx >= (m_content_length + m_checked_index))
    {
        text[m_content_length] = '\0';
        return GET_REQUEST;
    }
    return NO_REQUEST;
}

// 解析行
http_conn::LINE_STATE http_conn::parse_line()
{
    char temp;
    for (; m_checked_index < m_read_idx; ++m_checked_index)
    {
        temp = m_read_buffer[m_checked_index];
        if (temp == '\r')
        {
            if ((m_checked_index + 1) == m_read_idx)
                return LINE_OPEN;
            else if (m_read_buffer[m_checked_index + 1] == '\n')
            {
                m_read_buffer[m_checked_index++] = '\0';
                m_read_buffer[m_checked_index++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }
        else if (temp == '\n')
        {
            if ((m_checked_index > 1) && (m_read_buffer[m_checked_index - 1] == '\r'))
            {
                m_read_buffer[m_checked_index - 1] = '\0';
                m_read_buffer[m_checked_index++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }
    }
    return LINE_OPEN;
}

//
http_conn::HTTP_CODE http_conn::do_request()
{
    strcpy(m_real_file, doc_root);
    int len = strlen(doc_root);
    strncpy(m_real_file + len, m_url, FILENAME_LEN - len - 1);
    if (stat(m_real_file, &m_file_state) < 0)
        return NO_REQUEST;
    // 访问权限
    if (!(m_file_state.st_mode) & S_IROTH)
        return FORBIDDEN_REQUEST;
    // 判断是否目录
    if (S_ISDIR(m_file_state.st_mode))
        return BAD_REQUEST;
    int fd = open(m_real_file, O_RDONLY);
    m_file_address = (char *)mmap(0, m_file_state.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    return FILE_REQUEST;
}

void http_conn::unmap()
{
    if (m_file_address)
    {
        munmap(m_file_address, m_file_state.st_size);
        m_file_address = 0;
    }
}

bool http_conn::write()
{
    int temp = 0;
    int bytes_have_send = 0;
    int bytes_to_send = m_write_idx;
    if (bytes_to_send == 0)
    {
        modfd(m_epollfd, m_socketfd, EPOLLIN);
        init();
        return true;
    }
    while (1)
    {
        temp = writev(m_socketfd, m_iv, m_iv_count);
        if (temp < 0)
        {
            if (errno == EAGAIN)
            {
                modfd(m_epollfd, m_socketfd, EPOLLOUT);
                return true;
            }
            unmap();
            return false;
        }
    }
    bytes_to_send -= temp;
    bytes_have_send += temp;
    if (bytes_to_send <= bytes_have_send)
    {
        unmap();
        if (m_link)
        {
            init();
            modfd(m_epollfd, m_socketfd, EPOLLIN);
            return true;
        }
        else
        {
            modfd(m_epollfd, m_socketfd, EPOLLIN);
            return false;
        }
    }
    printf("写的数据：\n%s", m_write_buffer);
    return true;
}

// 线程池中工作线程调用
void http_conn::process()
{
    // 解析http请求
    HTTP_CODE read_ret = process_read();
    if (read_ret == NO_REQUEST)
    {
        modfd(m_epollfd, m_socketfd, EPOLLIN);
        return;
    }
    // 响应
    bool write_ret = process_write(read_ret);
    if (!write_ret)
        close_conn();
    modfd(m_epollfd, m_socketfd, EPOLLOUT);
    return;
}