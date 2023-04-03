#ifndef LOG_H
#define LOG_H
#include <iostream>
#include <stdio.h>
#include <pthread.h>
#include <string>
#include "block_queue.h"
class Log
{
private:
    Log();
    virtual ~Log();
    void *async_write_log()
    {
        std::string single_log;
        while (m_log_queue->pop(single_log))
        {
            m_mutex.lock();
            fputs(single_log.c_str(), m_fp);
            m_mutex.unlock();
        }
        return NULL;
    }

private:
    char dir_name[128];
    char log_name[128];
    int m_split_lines; // 最大行数
    int m_log_buffer_size;
    long long m_count;
    int m_today;
    FILE *m_fp;
    char *m_buffer;
    block_queue<std::string> *m_log_queue;
    bool m_is_async; // 同步标志位
    locker m_mutex;

public:
    static Log *get_instance()
    {
        static Log instance;
        return &instance;
    }

    static void *flush_log_thread(void *args)
    {
        get_instance()->async_write_log();
        return NULL;
    }

    bool init(const char *file_name, int log_buffer_size = 8192, int split_lines = 5000000, int max_queue_size = 0);

    void write_log(int level, const char *format, ...);

    void flush(void);
};

#define LOG_DEBUG(format, ...) Log::get_instance()->write_log(0, format, ##__VA_ARGS__)
#define LOG_INFO(format, ...) Log::get_instance()->write_log(1, format, ##__VA_ARGS__)
#define LOG_WARN(format, ...) Log::get_instance()->write_log(2, format, ##__VA_ARGS__)
#define LOG_ERROR(format, ...) Log::get_instance()->write_log(3, format, ##__VA_ARGS__)

#endif