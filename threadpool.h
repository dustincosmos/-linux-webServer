#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <pthread.h>
#include <list>
#include "locker.h"
#include <cstdio>

// T任务类
template <typename T>
class threadpool
{
private:
    // 线程数量
    int m_thread_number;

    // 线程池数组
    pthread_t *m_threads;

    // 请求队列的最多允许等待处理请求数量
    int m_max_requests;

    // 请求队列
    std::list<T *> m_workqueue;

    // 互斥锁
    locker m_queuelocker;

    // 信号量判断是否有处理
    sem m_queuestate;

    // 是否结束
    bool m_stop;

    static void *worker(void *arg);
    void run();

public:
    // threadpool(){}
    threadpool(int m_thread_num = 8, int m_max_requests = 10000);
    ~threadpool();
    bool append(T *target);
};

template <typename T>
threadpool<T>::threadpool::threadpool(int thread_num, int max_requests) : m_thread_number(thread_num), m_max_requests(max_requests), m_threads(NULL), m_stop(false)
{
    if (m_max_requests <= 0 || thread_num <= 0)
    {
        throw std::exception();
    }

    m_threads = new pthread_t[thread_num];
    if (!m_threads)
    {
        throw std::exception();
    }

    // 创建线程
    for (int i = 0; i < thread_num; i++)
    {
        printf("create the %dth thread!\n", i);

        if (pthread_create(m_threads + i, NULL, worker, this) != 0)
        {
            delete[] m_threads;
            throw std::exception();
        }

        if (pthread_detach(m_threads[i]))
        {
            delete[] m_threads;
            throw std::exception();
        }
    }
}

template <typename T>
threadpool<T>::threadpool::~threadpool()
{
    delete[] m_threads;
    m_stop = true;
}

template <typename T>
bool threadpool<T>::append(T *request)
{
    m_queuelocker.lock();                    // 获取互斥锁，保证线程安全
    if (m_workqueue.size() > m_max_requests) // 如果工作队列中的任务数量已经达到了最大值
    {
        m_queuelocker.unlock(); // 释放互斥锁
        return false;           // 返回 false，表示添加任务失败
    }

    m_workqueue.push_back(request); // 将任务添加到工作队列的末尾
    m_queuelocker.unlock();         // 释放互斥锁
    m_queuestate.post();            // 向信号量中添加一个信号，表示有新任务添加到工作队列
    return true;                    // 返回 true，表示添加任务成功
}

template <typename T>
void *threadpool<T>::worker(void *arg)
{
    threadpool *pool = (threadpool *)arg;
    pool->run();
    return pool;
}

template <typename T>
void threadpool<T>::run()
{
    while (!m_stop)
    {
        m_queuestate.wait(); // 等待信号量，阻塞当前线程直到有新任务被添加到工作队列

        m_queuelocker.lock();    // 获取互斥锁，保证线程安全
        if (m_workqueue.empty()) // 如果工作队列为空，则释放互斥锁并继续等待新任务
        {
            m_queuelocker.unlock();
            continue;
        }

        T *request = m_workqueue.front(); // 取出工作队列中的第一个任务
        m_workqueue.pop_front();          // 从工作队列中删除该任务
        m_queuelocker.unlock();           // 释放互斥锁

        if (!request) // 如果取出的任务为空，则继续循环
            continue;

        request->process(); // 处理任务
    }
}

#endif