/*
*服务器
*/
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <pthread.h>
#include <sys/types.h>
#include <string.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/stat.h>
#include <sys/ipc.h>
#include <sys/fcntl.h>

//结构体，用来分辨是信息还是心跳，以及信息长度
typedef struct
{
    int form; // 0为心跳，1为真实信息
    int len;  // 信息长度
}TYD;

void* func_client(void* arg)
{
    //将值保存之后，释放堆内存
    int sockfd_client = *(int *)arg;
    free(arg);

    TYD bag = {0};

    bag.form = 1, bag.len = strlen("link successful...");
    send(sockfd_client,&bag,sizeof(bag),0);
    send(sockfd_client,"link successful...",sizeof("link successful..."),0);
    while(1)
    {

    }
}


int main()
{
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
        int* ptr = (int *)malloc(sizeof(int));
        if(ptr == NULL)
        {
            printf("malloc error!\n");
            close(sockfd);
            return 1;
        }

        // 将客户端的套接字赋值给在这个堆内存中
        *ptr = sockfd_client;
        pthread_t tid = 0;
        int val = 0;
        if( (val = pthread_create(&tid,NULL,func_client,(void*)ptr)) != 0 )
        {
            fprintf(stderr,"pthread_create error:%s\n",strerror(val));
            bag.form = 1, bag.len = strlen("server busy...");
            send(sockfd_client,&bag,sizeof(bag),0);
            send(sockfd_client,"server busy...",sizeof("server busy..."),0);
            memset(&bag,0,sizeof(bag));
            free(ptr);
            close(sockfd_client);
            continue;
        }

        //创建完线程后，直接将线程分离，从而不需要主线程回收线程资源，而是线程结束后自动回收资源
        pthread_detach(tid);
    }


    return 0;
}