#ifndef BLOCK_QUEUE_H
#define BLOCK_QUEUE_H

#include "../locker.h"

template <typename T>
class block_queue
{
private:
    int m_front;
    int m_back;
    int m_size;
    int m_max_size;
    cond m_cond;
    T *m_queue;
    locker m_mutex;

public:
    bool full()
    {
        m_mutex.lock();
        if (m_size > m_max_size)
        {
            m_mutex.unlock();
            return true;
        }
        m_mutex.unlock();
        return false;
    }

    bool empty()
    {
        m_mutex.lock();
        if (m_size == 0)
        {
            m_mutex.unlock();
            return true;
        }
        m_mutex.unlock();
        return false;
    }

    void clear()
    {
        m_mutex.lock();
        m_front = -1;
        m_back = -1;
        m_size = 0;
        m_mutex.unlock();
    }

    bool front(T &value)
    {
        m_mutex.lock();
        if (m_size == 0)
        {
            m_mutex.unlock();
            return false;
        }
        value = m_queue[m_front];
        m_mutex.unlock();
        return true;
    }

    bool back(T &value)
    {
        m_mutex.lock();
        if (m_size == 0)
        {
            m_mutex.unlock();
            return false;
        }
        value = m_queue[m_back];
        m_mutex.unlock();
        return true;
    }

    int size()
    {
        m_mutex.lock();
        int tmp = m_size;
        m_mutex.unlock();
        return tmp;
    }

    int max_size()
    {
        m_mutex.lock();
        int tmp = m_max_size;
        m_mutex.unlock();
        return tmp;
    }

    bool push(const T &value)
    {
        m_mutex.lock();
        if (m_size > m_max_size)
        {
            m_cond.broadcast();
            m_mutex.unlock();
            return false;
        }
        m_back = (m_back + 1) % m_max_size;
        m_queue[m_back] = value;
        m_size++;
        m_cond.broadcast();
        m_mutex.unlock();
        return true;
    }

    bool pop(T& value)
    {
        m_mutex.lock();
        if(m_size==0)
        {
            m_mutex.unlock();
            return false;
        }
        m_front = (m_front+1)%m_max_size;
        value = m_queue[m_front];
        m_size--;
        m_mutex.unlock();
        return true;
    }

    block_queue(int max_size = 100)
    {
        if (max_size <= 0)
            exit(-1);
        m_queue = new T[max_size];
        m_max_size = max_size;
        m_size = 0;
        m_front = -1;
        m_back = -1;
    }
    ~block_queue()
    {
        m_mutex.lock();
        if (!m_queue)
            delete[] m_queue;
        m_mutex.unlock();
    }
};

#endif