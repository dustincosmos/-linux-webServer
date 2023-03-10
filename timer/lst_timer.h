#ifndef LST_TIMER_H
#define LST_TIMER_H

#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include <sys/stat.h>
#include <string.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <stdarg.h>
#include <errno.h>
#include <sys/wait.h>
#include <sys/uio.h>
#include <time.h>

class until_timer;

struct client_data
{
    sockaddr_in address;
    int socfd;
    until_timer *timer;
};

// 定时器类
class until_timer
{
public:
    time_t experc;
    void (*cb_func)(client_data *);
    until_timer *prev;
    until_timer *next;
    client_data *user_data;

    until_timer() : prev(NULL), next(NULL) {}
    ~until_timer() {}
};

class sort_timer_lst
{
private:
    void add_timer(until_timer *timer, until_timer *lst_head);

    until_timer *head;
    until_timer *tail;

public:
    sort_timer_lst();
    ~sort_timer_lst();

    void add_timer(until_timer *timer);
    void adjust_timer(until_timer *timer);
    void del_timer(until_timer *timer);
    void tick();
};

class Utils
{
private:
    static int *u_pipefd;
    sort_timer_lst m_timer_lst;
    int m_TIEMSHOT;

public:
    Utils();
    ~Utils();
    void init(int timeslot);
    int setnonblocking(int fd);
    void addfd(int epollfd, int fd, bool one_shot, int TRIGMode);
    static void sig_handler(int sig);
    void addsig(int sig, void(handlder)(int), bool restart = true);
    void time_handler();
    void show_error(int connfd, const char *info);
    static int u_epollfd;
};

void cb_func(client_data *user_data);

#endif