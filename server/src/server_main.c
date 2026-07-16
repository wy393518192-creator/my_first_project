/*
*服务器
*/

// #include <stdio.h>
// #include <stdlib.h>
// #include <unistd.h>
// #include <sys/socket.h>
// #include <pthread.h>
// #include <sys/types.h>
// #include <string.h>
// #include <netinet/in.h>
// #include <arpa/inet.h>
// #include <sys/stat.h>
// #include <sys/ipc.h>
// #include <sys/fcntl.h>
// #include <time.h>

#include "type.h"

//条件锁，互斥锁，读写锁
pthread_cond_t cond;
pthread_mutex_t mutex;
pthread_rwlock_t rwlock;

//最大心跳时间间隔
#define MAX_TIME  15.0

//结构体，用来分辨是信息还是心跳，以及信息长度
typedef struct
{
    int form; // 0为心跳，1为真实信息
    int len;  // 信息长度
}TYD;

typedef struct clinet_node
{
    int sockfd; // accept之后，与客户端的套接字
    pthread_t tid; // 所处线程id

    char ip[16]; // 客户端IP
    int port; // 客户端的port端口

    time_t ti; // 上次心跳时间
    int online; // 在线状态，1在线，0掉线

    int form; // 0为心跳，1为真实信息
    int len;  // 信息长度

    struct clinet_node* next;
}SOC;

static SOC head = {0};//初始化，next指针为null

//将线程的信息放入一个结构体中的函数
void node_process(SOC* head, SOC* new_node)
{
    // 头插
    if(head->next == NULL)
    {
        pthread_mutex_lock(&mutex);
        head->next = new_node;
        pthread_cond_signal(&cond);
        pthread_mutex_unlock(&mutex);
    }else
    {
        SOC* temp = head->next;
        head->next = new_node;
        new_node->next = temp;
    }
    return;
}


//每个客户端所在线程的 函数
void* func_client(void* arg)
{
    //将值保存之后，释放堆内存
    SOC* ptr = (SOC *)arg;

    TYD bag = {0};
    // 发送是成功链接
    bag.form = 1, bag.len = strlen("link successful...");
    send(ptr->sockfd,&bag,sizeof(bag),0);
    send(ptr->sockfd,"link successful...",sizeof("link successful..."),0);
    while(1)
    {
        int res = recv(ptr->sockfd,&bag,sizeof(bag),0);
        if(res <= 0)
        {
            close(ptr->sockfd);
            printf("sockfd error or end\n");
            break;
        }

        if(bag.form == 0)
        {
            printf("ip为：%s客户端的心跳....\n",ptr->ip);

            pthread_rwlock_wrlock(&rwlock);
            ptr->ti = time(NULL);
            pthread_rwlock_unlock(&rwlock);
        
        }else
        {
            char buff[1024] = {0};
            printf("真实信息\n");
            recv(ptr->sockfd,buff,ptr->len,0);

        }
    }
    return NULL;
}

//监控函数，判断心跳是否超时
void monitor_func(void)
{
    //如果是头节点后面为空，先睡觉，等插了第一个节点被唤醒
    if(head.next == NULL)
    {
        pthread_mutex_lock(&mutex);
        pthread_cond_wait(&cond,&mutex);
        pthread_mutex_unlock(&mutex);
    }

    SOC* ptr = head.next;
    SOC* ptr_temp = &head;
    while(1)
    {
        //心跳超时了
        time_t now_time = time(NULL);
        if(difftime(ptr->ti,now_time) > MAX_TIME)
        {
            pthread_rwlock_wrlock(&rwlock);

            ptr_temp->next = ptr->next;
            close(ptr->sockfd);
            printf("free sockfd %d,id %s,tid %d\n", ptr->sockfd, ptr->ip, ptr->tid);
            free(ptr);
            ptr = ptr_temp->next;

            pthread_rwlock_unlock(&rwlock);
        }

        ptr_temp = ptr_temp->next;
        ptr = ptr->next;
        if(ptr == NULL)
        {
            ptr_temp = &head;
            ptr = head.next;
        }
    }

}

int main()
{
    pthread_cond_init(&cond,NULL);
    pthread_mutex_init(&mutex,NULL);
    pthread_rwlock_init(&rwlock,NULL);

    int ttid = 0;
    int result = 0;
    //监控心跳是否超时线程
    if( (result = pthread_create(&ttid,NULL,(void*(*)(void*))monitor_func,NULL)) != 0 )
    {
        // 线程开启失败，发送链接失败信号
        fprintf(stderr,"pthread_create error:%s\n",strerror(result));
        printf("monitor error!\n");
        exit(1);
    }

    TYD bag = {0};
    int sockfd = socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);
    if(sockfd < 0)
    {
        perror("socket error!");
        return 1;
    }

    struct sockaddr_in addr = {0};
    socklen_t len = sizeof(addr);
    addr.sin_family = AF_INET;
    addr.sin_port = htons(8888);
    addr.sin_addr.s_addr = inet_addr(INADDR_ANY);

    if(bind(sockfd,&addr,len) < 0)
    {
        perror("bind error!");
        close(sockfd);
        return 1;
    }

    if(listen(sockfd,50) < 0)
    {
        perror("listen error!");
        close(sockfd);
        return 1;
    }

    printf("服务器已启动，本服务器端口为%d,ip地址为：%s,监听中...\n",
        ntohs(addr.sin_port), inet_ntoa(addr.sin_addr));

    while(1)
    {
        //本结构体用来存储客户端的信息,以及其大小信息
        struct sockaddr_in addr_client = {0};
        socklen_t len_client = sizeof(addr_client);

        int sockfd_client = 0;
        if( (sockfd_client = accept(sockfd,(struct sockaddr*)&addr_client,&len_client)) < 0 )
        {
            //出错就跳过本次接听
            perror("accept error!");
            continue;
        }

        //在堆上创建空间，防止将套接字传入线程函数的时候，在栈上的套接字已经销毁
        SOC* ptr = (SOC *)malloc(sizeof(SOC));
        if(ptr == NULL)
        {
            printf("malloc error!\n");
            close(sockfd);
            return 1;
        }

        memset(ptr,0,sizeof(SOC));

        // 将客户端的套接字赋值给在这个堆内存中
        ptr->sockfd = sockfd_client;
        ptr->ti = time(NULL);
        strcpy(ptr->ip, inet_ntoa(addr_client.sin_addr));
        ptr->port = ntohs(addr_client.sin_port);
        ptr->form = 0;
        ptr->online = 1;
        ptr->len = 0;
        ptr->next = NULL;
        
        pthread_t tid = 0;
        int val = 0;
        if( (val = pthread_create(&tid,NULL,func_client,(void*)ptr)) != 0 )
        {
            // 线程开启失败，发送链接失败信号
            fprintf(stderr,"pthread_create error:%s\n",strerror(val));
            bag.form = 1, bag.len = strlen("server busy...");
            send(sockfd_client,&bag,sizeof(bag),0);
            send(sockfd_client,"server busy...",sizeof("server busy..."),0);
            memset(&bag,0,sizeof(bag));
            free(ptr);
            close(sockfd_client);
            continue;
        }
        ptr->tid = tid;

        //创建完线程后，直接将线程分离，从而不需要主线程回收线程资源，而是线程结束后自动回收资源
        pthread_detach(tid);
    }

    pthread_mutex_destroy(&mutex);
    pthread_cond_destroy(&cond);
    pthread_rwlock_destroy(&rwlock);

    return 0;
}