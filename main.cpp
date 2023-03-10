#include "locker.h"
#include "threadpool.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <errno.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <signal.h>
#include "http_conn.h"
#include "lst_timer.h"
#define MAX_FD 65536          // 最大文件描述符个数
#define MAX_EVENT_NUMBER 1000 // 监听最大数量
#define TIMESLOT 5            // 最小超时单位

static int pipefd[2];
static sort_timer_lst timer_lst;
static int epollfd = 0;

// 添加信号处理,向操作系统注册一个信号处理函数，以便在程序接收到相应的信号时能够做出相应的处理。
void addsig(int sig, void(handler)(int), bool restart = true)
{
    struct sigaction sa;
    memset(&sa, '\0', sizeof(sa));
    sa.sa_handler = handler;
    if (restart)
        sa.sa_flags |= SA_RESTART;
    sigfillset(&sa.sa_mask);
}

// 添加文件描述符到epoll
extern void addfd(int epollfd, int fd, bool one_shot);
// 删除文件描述符
extern void removefd(int epolllfd, int fd);
extern int setnoblocking(int fd);
void sig_handler(int sig)
{
    // 为保证函数的可重入性，保留原来的errno
    int save_errno = errno;
    int msg = sig;
    send(pipefd[1], (char *)&msg, 1, 0);
    errno = save_errno;
}

void timer_handler()
{
    timer_lst.tick();
    alarm(TIMESLOT);
}

void cb_func(client_data *user_data)
{
    epoll_ctl(epollfd, EPOLL_CTL_DEL, user_data->sockfd, 0);
    close(user_data->sockfd);
    http_conn::m_user_count--;
}

int main(int argc, char *argv[])
{
    if (argc <= 1)
    {
        printf("按照如下格式允许:%s port_number\n", basename(argv[0]));
        exit(-1);
    }

    // 端口号
    int port = atoi(argv[1]);

    // 对sigpie信号处理,忽略 SIGPIPE 信号，避免在网络编程中因为发送数据给一个已经关闭的套接字而导致程序崩溃。
    addsig(SIGPIPE, SIG_IGN);

    // 初始化线程池
    threadpool<http_conn> *pool = NULL;
    try
    {
        pool = new threadpool<http_conn>;
    }
    catch (const std::exception &e)
    {
        exit(-1);
    }

    // 保存客户端信息
    http_conn *users = new http_conn[MAX_FD];

    int listenfd = socket(PF_INET, SOCK_STREAM, 0);
    // 设置端口复用
    int reuse = 1;
    setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    // 绑定与监听
    struct sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);
    bind(listenfd, (struct sockaddr *)&address, sizeof(address));
    listen(listenfd, 5);

    // 创建内核时间表
    epoll_event events[MAX_EVENT_NUMBER];
    int epollfd = epoll_create(5);

    // 将监听的文件描述符添加到epoll对象
    addfd(epollfd, listenfd, false);
    http_conn::m_epollfd = epollfd;

    // 创建管道
    socketpair(PF_UNIX, SOCK_STREAM, 0, pipefd);
    setnoblocking(pipefd[1]);
    addfd(epollfd, pipefd[0], false);

    // 添加信号
    addsig(SIGALRM, sig_handler, false);
    addsig(SIGTERM, sig_handler, false);

    client_data *users_timer = new client_data[MAX_FD];
    bool timeout = false;
    // alarm(TIMESLOT);
    bool stop_server = 0;
    while (!stop_server)
    {
        int num = epoll_wait(epollfd, events, MAX_EVENT_NUMBER, -1);
        if ((num < 0) && (errno != EINTR))
        {
            printf("epoll failure\n");
            break;
        }
        for (int i = 0; i < num; i++)
        {
            int sockfd = events[i].data.fd;
            // 有连接
            if (sockfd == listenfd)
            {
                struct sockaddr_in client_address;
                socklen_t client_addrlen = sizeof(client_address);
                int connfd = accept(listenfd, (struct sockaddr *)&client_address, &client_addrlen);

                // 连接满了
                if (http_conn::m_user_count >= MAX_FD)
                {
                    close(connfd);
                    continue;
                }

                // // 将新的客户数据初始化
                users[connfd].init(connfd, client_address);

                users_timer[connfd].address = client_address;
                users_timer[connfd].sockfd = connfd;
                util_timer *timer = new util_timer;
                timer->user_data = &users_timer[connfd];
                timer->cb_func = cb_func;
                time_t cur = time(NULL);
                timer->expire = cur + 3 * TIMESLOT;
                users_timer[connfd].timer = timer;
                // timer_lst.add_timer(timer);

                // while (1)
                // {
                //     int connfd = accept(listenfd, (struct sockaddr *)&client_address, &client_addrlen);
                //     if (connfd < 0)
                //         break;
                //     if (http_conn::m_user_count >= MAX_FD)
                //         break;
                //     users[connfd].init(connfd, client_address);

                //     // 初始化client_data数据
                //     // 创建定时器，设置回调函数和超时时间，绑定用户数据，将定时器添加到链表中
                //     users_timer[connfd].address = client_address;
                //     users_timer[connfd].socfd = connfd;
                //     until_timer *timer = new until_timer;
                //     timer->user_data = &users_timer[connfd];
                //     timer->cb_func = cb_func;
                //     time_t cur = time(NULL);
                //     timer->experc = cur + 3 * TIMESLOT;
                //     users_timer[connfd].timer = timer;
                //     timer_lst.add_timer(timer);
                // }
                // continue;
            }
            else if (events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR))
            {
                // 对方异常断开
                util_timer *timer = users_timer[sockfd].timer;
                timer->cb_func(&users_timer[sockfd]);
                if (timer)
                    timer_lst.del_timer(timer);
                users[sockfd].close_conn();
            }
            // 处理信号
            else if ((sockfd == pipefd[0]) && (events[i].events & EPOLLIN))
            {
                int sig;
                char signals[1024];
                int ret = recv(pipefd[0], signals, sizeof(signals), 0);
                if (ret == -1)
                {
                    continue;
                }
                else if (ret == 0)
                {
                    continue;
                }
                else
                {
                    for (int i = 0; i < ret; ++i)
                    {
                        switch (signals[i])
                        {
                        case SIGALRM:
                        {
                            timeout = true;
                            break;
                        }
                        case SIGTERM:
                        {
                            stop_server = true;
                        }
                        }
                    }
                }
            }
            else if (events[i].events & EPOLLIN)
            {
                util_timer *timer = users_timer[sockfd].timer;
                if (users[sockfd].read())
                {
                    pool->append(users + sockfd);
                    if (timer)
                    {
                        time_t cur = time(NULL);
                        timer->expire = cur + 3 * TIMESLOT;
                        timer_lst.adjust_timer(timer);
                    }
                }
                else
                {
                    timer->cb_func(&users_timer[sockfd]);
                    if (timer)
                    {
                        timer_lst.del_timer(timer);
                    }
                    // users[sockfd].close_conn();
                }
            }
            else if (events[i].events & EPOLLOUT)
            {
                // if (!users[sockfd].write())
                // {
                //     users[sockfd].close_conn();
                // }
                util_timer *timer = users_timer[sockfd].timer;
                if (users[sockfd].write())
                {
                    // 若有数据传输，则将定时器往后延迟3个单位
                    // 并对新的定时器在链表上的位置进行调整
                    if (timer)
                    {
                        time_t cur = time(NULL);
                        timer->expire = cur + 3 * TIMESLOT;
                        timer_lst.adjust_timer(timer);
                    }
                }
                else
                {
                    timer->cb_func(&users_timer[sockfd]);
                    if (timer)
                    {
                        timer_lst.del_timer(timer);
                    }
                }
            }
        }
        if (timeout)
        {
            timer_handler();
            timeout = false;
        }
    }
    close(epollfd);
    close(listenfd);
    close(pipefd[1]);
    close(pipefd[0]);
    delete[] users;
    delete users_timer;
    delete pool;
    return 0;
}