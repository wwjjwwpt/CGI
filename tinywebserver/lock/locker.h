#ifndef LOCKER_H
#define LOCKER_H
#include<exception>//定义异常类和异常处理函数
#include<pthread.h>//线程库用于创建线程
#include<semaphore.h>//多线程编程中用于协调不同线程之间的并发访问

class sem{
public:
    //未给定信号初始量，初始化为0；
    sem()
    {
        if(sem_init(&m_sem,0,0)!=0){
            throw std::exception();
        }
    }

    sem(int num){
        if(sem_init(&m_sem,0,num)!=0){
            throw std::exception();
        }
    }

    ~sem(){
        sem_destroy(&m_sem);
    }
    
    
private:
    sem_t m_sem;
};

#endif