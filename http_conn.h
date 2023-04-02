#ifndef HTTPCONNECTION_H
#define HTTPCONNECTION_H
#include <sys/epoll.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <stdarg.h>
#include <errno.h>
#include "locker.h"
#include <sys/uio.h>
#include <string.h>
#include "CGImysql/sql_connection_pool.h"
class http_conn
{

public:
    static int m_epollfd;                      // 所有socket被注册到同一个epoll上
    static int m_user_count;                   // 统计用户数量
    static const int FILENAME_LEN = 200;       // 文件名长度
    static const int READ_BUFFER_SIZE = 2048;  // 读入缓冲区的最大容纳
    static const int WRITE_BUFFER_SIZE = 1024; // 写缓冲区的最大容纳

    // http请求方法
    enum METHOD
    {
        GET = 0,
        POST,
        HEAD,
        PUT,
        DELETE,
        TRACE,
        OPTIONS,
        CONNECT
    };

    // CHECK_STATE枚举定义了HTTP解析请求时的状态，包括解析请求行、解析头部和解析内容
    enum CHECK_STATE
    {
        CHECK_STATE_REQUESTLINE = 0,
        CHECK_STATE_HEADER,
        CHECK_STATE_CONTENT
    };

    enum LINE_STATE
    {
        LINE_OK = 0,
        LINE_BAD,
        LINE_OPEN // 读取行不完整
    };

    // HTTP_CODE枚举定义了HTTP请求处理的结果状态码，
    // 包括无请求、GET请求、错误请求、无资源、禁止请求、文件请求、内部错误和关闭连接
    enum HTTP_CODE
    {
        NO_REQUEST = 0,
        GET_REQUEST,
        BAD_REQUEST,
        FORBIDDEN_REQUEST,
        FILE_REQUEST,
        INTERNAL_ERROR,
        CLOSE_CONNECTION
    };

    http_conn() {}
    ~http_conn() {}

    void process(); // 处理请求

    void init(int sockerfd, const sockaddr_in &addr); // 初始化新的连接
    void close_conn();                                // 断开连接
    bool read();                                      // 非阻塞
    bool write();                                     // 非阻塞
    void unmap();

    HTTP_CODE process_read();                 // 解析http请求
    HTTP_CODE parse_request_line(char *text); // 解析http请求首行
    HTTP_CODE parse_headers(char *text);      // 解析http请求头
    HTTP_CODE parse_content(char *text);      // 解析http请求体
    LINE_STATE parse_line();
    bool process_write(HTTP_CODE read_ret);

    HTTP_CODE do_request();
    void initmysql_result(connection_pool *connPool);

    char *get_line() { return m_read_buffer + m_start_line; }

private:
    int m_socketfd;        // http连接的socket
    sockaddr_in m_address; // 通信地址
    char m_read_buffer[READ_BUFFER_SIZE];
    int m_read_idx;
    char m_write_buffer[WRITE_BUFFER_SIZE];
    int m_write_idx;
    int m_checked_index; // 当前字符在缓冲区的位置
    int m_start_line;    // 当前解析行起始位置
    char *m_url;         // 文件名
    char *m_version;     // http1.1
    METHOD m_method;     // 请求方法
    char *m_host;        // 主机
    bool m_link;         // http是否保持连接
    long m_content_length;
    char m_real_file[FILENAME_LEN];
    char *m_file_address;
    struct stat m_file_state; // 获取文件属性
    struct iovec m_iv[2];     // 向量，用于存储多元素数组
    int m_iv_count;
    bool cgi;       // 登录校验位
    char *m_string; // 请求头数据
    MYSQL *mysql;
    

    CHECK_STATE m_check_state; // 主状态机当前状态

    void init(); // 初始化状态机
    bool add_response(const char *fonmat, ...);
    bool add_state_line(int state, const char *title);
    bool add_headers(int len);
    bool add_content_length(int len);
    bool add_content_type();
    bool add_link();
    bool add_blank_line();
    bool add_content(const char *content);
};

#endif