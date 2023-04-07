#ifndef PROCESSPOOL_H
#define PROCESSPOOL_H

#include <sys/types.h> //size_t/pid_t/time_t和一些数据结构
#include <sys/socket.h> // socket头文件
#include <netinet/in.h> //IP的数据结构
#include <arpa/inet.h> //转换网络字节序和本地字节序
#include <assert.h> //检测特定条件是否生成
#include <stdio.h>  //标准输入输出
#include <unistd.h> //系统调用
#include <errno.h> //出错的错误码
#include <string.h> //字符串
#include <fcntl.h> //文件描述符和文件控制符
#include <stdlib.h> //通用工具内存分配，字符串转换等
#include <sys/epoll.h> //用于访问Linux下的epoll事件通知机制
#include <signal.h> //信号处理的定义
#include <sys/wait.h> //管理进程的变量
#include <sys/stat.h> //用于获取文件的各个书信

class process{
public:
    process():m_pid(-1){}
public:
    pid_t m_pid;
    int m_pipefd[2];
};


template<typename T>
class processpool{
private:
    processpool(int listenfd,int process_number=8);
public:
    static processpool<T>*create(int listenfd, int process_num = 8){
        if(!m_instance){
            m_instance = new processpool<T>(listen)
        }
        return m_instance;
    }
    ~processpool(){
        delete [] m_sub_process;
    }
    void run();
private:
    void setup_sig_pipe();
    void run_parent();
    void run_child();
private:
    //最大子进程数量
    static const int MAX_PROCESS_NUMVER = 16;
    //每个子进程最多能处理的客户数量
    static const int USER_PER_PROCESS = 65536;    
    //epoll最多能处理的事件数
    static const int MAX_EVENT_NUMBER = 10000;
    //进程池中的进程数
    int m_process_number;
    //子进程的序号
    int m_idx;
    //每个进程的epoll内核时间表
    int m_epollfd;
    //监听 socket
    int m_listenfd;
    //子进程通过m_stop来决定是否停止运行
    int m_stop;
    //保存所有子进程的描述信息
    process* m_sub_process;
    //进程池静态实例
    static processpool<T>* m_instance;
};

template<typename T>
processpool<T>* processpool<T>::m_instance = NULL;

//信号管道
static int sig_pipefd[2];

static int setnonblocking(int fd){
    //获得fd描述符的基本信息
    int old_option = fcntl(fd,F_GETFL);
    //设置非阻塞
    int new_option = old_option|O_NONBLOCK;
    fcntl(fd,F_SETFL,new_option);
    return old_option;
}

static void addfd(int epollfd,int fd){
    epoll_event event;
    event.data.fd = fd;
    //设置边缘触发
    event.events = EPOLLIN | EPOLLET;
    //将fd文件描述符注册为epoll文件描述符
    epoll_ctl(epollfd,EPOLL_CTL_ADD,fd, &event);
    setnonblocking(fd);
}

static void removefd(int epollfd,int fd){
    //从监听事件中删除并关闭文件描述符
    epoll_ctl(epollfd,EPOLL_CTL_DEL,fd,0);
    close(fd);
}

static void sig_handler(int sig){
    int save_errno = errno;
    int msg = sig;
    //将msg进行
    send(sig_pipefd[1],(char*)&msg,1,0);
    errno = save_errno;
}

static void addsig(int sig,void(handler)(int),bool restart = true){
    struct sigaction sa;
    memset(&sa,'\0',sizeof(sa));
    sa.sa_handler = handler;
    if(restart){
        sa.sa_flags|=SA_RESTART;
    }
    sigfillset(&sa.sa_mask);
    assert(sigaction(sig,&sa,NULL)!=1);
}

//进程池构造函数，参数listenfd是监听socket，他必须在船舰进程池之前被创建，否则子进程无法直接应用，参数process_number指定进程池中子进程的数量
template<typename T>
processpool<T>::processpool(int listenfd,int process_number):m_listenfd(listenfd),m_process_number(process_number),m_id(-1),m_stop(false){
    assert((process_number>0)&&(process_number<=MAX_PROCESS_NUMVER));
    m_sub_process = new process[process_number];
    assert(m_sub_process);
    //创建process_number个子进程，并建立他们和父进程之间的管道
    for(int i=0;i<process_number;i++){
        int res = socketpair(PF_UNIX,SOCK_STREAM,0,m_sub_process[i].m_pipefd);
        assert(res==0);
        //父进程返回
        m_sub_process[i].m_pid = fork();
        assert(m_sub_process[i].m_pid>=0);
        if(m_sub_process[i].m_pid >0){
            //父进程关闭写进程，父进程读取子进程的通知和请求，第一个i=0,将这个子进程作为父进程
            close(m_sub_process[i].m_pipefd[1]);
            continue;
        }
        else{
            //子进程关闭读端，并记录自己在数组中的下标，写入来向父进程发送通知和请求
            close(m_sub_process[i].m_pipefd[0]);
            m_idx = i;
            break;
        }
    }
}

template<typename T>
void processpool<T>:: setup_sig_pipe(){
    //创建epoll事件监听表和信号管道
    m_epollfd = epoll_create(5);
    assert(m_epollfd!=-1);

    int ret = socketpair(PF_UNIX,SOCK_STREAM,0,sig_pipefd);
    assert(ret!=-1);

    setnonblocking(sig_pipefd[1]);
    addfd(m_epollfd,sig_pipefd[0]);

    addsig(SIGCHLD,sig_handler);
    addsig(SIGTERM,sig_handler);
    addsig(SIGINT,sig_handler);
    addsig(SIGPIPE,SIG_IGN);

}

//父进程的m_id为-1，子进程m_idx大于等于0
template<typename T>
void processpool<T>::run(){
    if(m_idex!=-1){
        run_child();
        return;
    }
    run_parent();
}

template<typename T>
void processpool<T>::run_child(){

    setup_sig_pipe();
    //每个子进程通过其在进程池中的序号值m_idx找到父进程通信的管道
    int pipefd = m_sub_process[m_idx].m_pipefd[1];
    //每个子进程需要监听管道文件描述符pipefd，因为父进程通过其来通知子进程连接状态
    addfd(m_epollfd,pipefd);

    epoll_event events[MAX_EVENT_NUMBER];
    T* users =new T[USER_PER_PROCESS]
    assert(users);
    int number = 0;
    int ret = -1;
    while(!m_stop){
        number = epoll_wait(m_epollfd,events,MAX_EVENT_NUMBER,-1);
        if((number<0)&&(errno!=EINTR)){
            printf("epoll failure");
            break;
        }
        for(int i=0;i<number;i++){
            int sockfd =events[i].data.fd;
            if((sockfd==pipefd)&&(events[i].events&EPOLLIN)){
                int client = 0;
                //从父子进程的管道中读取，将结果保存在client中，读取成功表示有新客户
                ret = recv(sockfd,(char*)&client,sizeof(client),0);
                if(((ret<0)&&(errno!=EAGAIN))||ret = 0){
                    continue;
                }
                else{
                    struct sockaddr_in client_address;
                    socklen_t client_addrlength = sizeof(client_address);
                    int connfd = accept(m_listenfd,(struct sockaddr*)&client_address,&client_addrlength);
                    if(connfd<0){
                        printf("errno is:%d\n",errno);
                        continue;
                    }
                    addfd(m_epollfd,connfd);
                    //T模板类需要实现init，来初始化一个客户连接，我们直接使用connfd来索引逻辑对象
                    users[connfd].init(m_epollfd,connfd,client_address);
                }
            }
        else if((sockfd==sig_pipefd[0])&&(events[i].events&EPOLLIN)){
            int sig;
            char signals[1024];
            ret = recv(sig_pipefd[0],signals,sizeof(signals),0);
            if(ret<=0){
                continue;
            }
            else{
                for(int i=0;i<ret;++i){
                    switch (signals[i])
                    {
                    case SIGCHLE:{
                        pid_t pid;
                        int stat;
                        while((pid=waitpid(-1,&stat,WNOHANG))){
                            continue;
                        }
                        break;
                    }
                    case SIGTERM;
                    case SIGINT:
                    {
                        m_stop = true;
                        break;
                    }
                    default:{
                        break;
                    }

                }
            }
        }
        }
        //如果是其他刻度数据，就是用户请求，用逻辑处理对象的process
        else if(events[i].events&EPOLLIN){
            users[sockfd].process();
        }
        else{
            continue;
        }
        }
    }
    delete [] users;
    users = NULL;
    close(pipefd);
    close(m_epollfd);
}

template<typename T>
void processpool<T>::run_parent(){
    setup_sig_pipe();
    //监听m_listened
    addfd(m_epollfd,m_listenfd);

    epoll_event events[MAX_EVENT_NUMBER];
    int sub_process_counter = 0;
    int new_conn = 1;
    int number =0;
    ret =-1;
    while(!m_stop){
        number = epoll_wait(m_epollfd,events,MAX_EVENT_NUMBER,-1);
        if((number<0)&&(errno!=EINTR)){
            print("epoll failure\n");
            break;
        }
        for(int i=0;i<number;i++){
            int socfd = events[i].data.fd;
            if(sockfd == m_listenfd){
                int i =sub_process_counter;
                do{
                    if(m_sub_process[i].m_pid!=-1){
                        break;
                    }
                    i = (i+1)%m_process_number;
                }
                while(i!=sub_process_counter);
                if(m_sub_process[i].m_pid==-1){
                    m_stop=true;
                    break;
                }
                sub_process_counter = (i+1)%m_process_number;
                send(m_sub_process[i].m_pipefd[0],(char*) &new_conn,sizeof(new_conn),0);
                printf("send request to child %d\n",i);
            }
            else if((sockfd==sig_pipefd[0])&&(events[i].events&EPOLLIN)){
                int sig;
                char signals[1024];
                ret = recv(sig_pipefd[0],signals,sizeof(signals),0);
                if(ret<=0){
                    continue;
                }
                else{
                    for(int i=0;i<ret;i++){
                        switch(signals[i]){
                            case SIGCHLD:{
                                pid_t pid;
                                int stat;
                                while((pid=waitpid(-1,&stat,WNOHANG))>0){
                                    for(int i=0;i<m_process_number;i++){
                                        //如果经常中第i个子进程退出，则主进程关闭相应的通信通道，并设置m_pid=-1，来标记子进程已经退出
                                        if(m_sub_process[i].m_pid==pid){
                                            printf("child %d join",i);
                                            close(m_sub_process[i].m_pipefd[0]);
                                            m_sub_process[i].m_pid = -1;
                                        }
                                    }
                                }
                                m_stop = true;
                                for(int i=0;i<m_process_number;i++){
                                    if(m_sub_process[i].m_pid!=-1){
                                        m_stop =false;
                                    }
                                }
                                break;
                            }
                            case SIGTERM:
                            case SIGINT:
                            {
                                //如果父进程接收到终止信号，那么杀死所有子进程，并等待他们全部结束，子进程结束的方法是向父子进程之间的通信管道发送特殊数据。
                                printf("kill all the child now");
                                for(int i=0;i<m_process_number;i++){
                                    int pid = m_sub_process[i].m_pid;
                                    if(pid!=-1){
                                        kill(pid,SIGTERM);
                                    }
                                }
                                break;
                            }
                            default:{
                                break;
                            }
                            }
                        }
                    }
                }
                else{
                    continue;
                }
            }
        }
        close(m_epollfd); 
    }





#endif