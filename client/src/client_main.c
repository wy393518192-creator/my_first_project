/*
*客户端
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
#include <time.h>


//结构体，用来分辨是信息还是心跳，以及信息长度
typedef struct
{
    int form; // 0为心跳，1为真实信息
    int len;  // 信息长度
}TYD;

void* func_hrartbest(void * arg)
{
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
    addr.sin_family = AF_INET;
    addr.sin_port = htons(8888);
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    int res = connect(sockfd,(struct sockaddr*)&addr,sizeof(addr));
    if(res != 0)
    {
        perror("socket error!");
        close(sockfd);
        return 1;
    }

    /*
    先接收一次服务器消息，查看是否真正链接成功，
    （防止出现先链接上了，但是创建子线程失败，服务器主动断开与客户端的链接，
    客户端这边打印的信息确是成功链接）
    */
    char buff[1024] = {0};
    if(recv(sockfd, &bag, sizeof(bag), 0) < 0)
    {
        perror("the first time error!");
        clsoe(sockfd);
        return 1;
    }

    if(recv(sockfd, buff, bag.len, 0) < 0)
    {
        perror("second time around error!");
        clsoe(sockfd);
        return 1;
    }

    if(strcmp(buff,"server busy...") == 0)
    {
        printf("服务器繁忙，请稍后再试！\n");
        clsoe(sockfd);
        return 0;
    }
    printf("%s\n",buff);
    memset(&bag,0,sizeof(bag));
    memset(buff,0,sizeof(buff));

    // 创建心跳线程，保持与服务器的链接状态






    return 0;
}