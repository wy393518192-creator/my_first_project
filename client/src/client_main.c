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

//心跳线程
void* func_hrartbest(void * arg)
{
    int sockfd = *(int*)arg;

    TYD best;
    best.form = 0;
    best.len = 0;

    while(1)
    {
        send(sockfd,&best,sizeof(best),0);
        sleep(5);
    }
    return NULL;
}

typedef struct
{
    char buff[16]; //配置文件中的IP地址
    int port;      //配置文件中的port端口

}IP_CF;

//获取配置文件中的值，来设置服务器的IP和端口
IP_CF func_ipconfig()
{
    FILE* fptr = fopen("./../server_ipconfigfile.txt","r");
    if(fptr == NULL)
    {
        printf("fopen error!\n");
        fflush(stdout);
        exit(1);
    }

    IP_CF temp = {0};
    fscanf(fptr, "ip = %s", temp.buff);
    fscanf(fptr, "port = %d", &(temp.port));

    fclose(fptr);

    return temp;
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

   IP_CF ip_cf = func_ipconfig();

    struct sockaddr_in addr = {0};
    socklen_t len = sizeof(addr);
    addr.sin_family = AF_INET;
    addr.sin_port = htons(ip_cf.port);
    addr.sin_addr.s_addr = inet_addr(ip_cf.buff);

    int res = connect(sockfd,(struct sockaddr*)&addr,sizeof(addr));
    if(res != 0)
    {
        perror("connect error!");
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
    pthread_t tid;
    pthread_create(&tid,NULL,func_hrartbest,(void*)&sockfd);

    int  logo = 1;

    while(logo)
    {
        
        int chose = 0;
        do
        {
            printf("---------------------客户界面-----------------------\n");
            printf("1.登录\n");
            printf("2.注册\n");
            printf("3.退出\n");
            printf("-----------------------------------------------------\n");
            printf("请输入你的选择：");
            int res = scanf("%d", &chose);
            char ch;
            while((ch = getchar()) != '\n' && ch != EOF);
            if(res == 1)
            {
                break;   
            }
            printf("输入有误，请重新输入！\n");

        }while(1);

        switch(chose)
        {
            case 1:
                break;
            case 2:
                break;
            case 3:
                logo = 0;
                break;
            default:
                break;
        }

    }


    return 0;
}