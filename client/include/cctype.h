#ifndef CCTYPE_H
#define CCTYPE_H

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


// 真正的 群 的节点
typedef struct group
{
    char gname[10]; // 群名字
    struct group* gnext;
} GROUP;

// 账号中 裙 头节点，不存储数据
typedef struct
{
    int gnum; // 群数量
    GROUP* gnext; // 指向第一个群
} HEAD_GROUP;

// 真正的 朋友 的节点
typedef struct friend
{
    char fname[10]; // 群名字
    struct friend* fnext;
} FRIEND;

// 账号中 朋友 头节点，不存储数据
typedef struct
{
    int fnum; // 朋友数量
    GROUP* fnext; // 指向第一个朋友
} HEAD_FRIEND;

//账号结构体
typedef struct use
{
    char account[10]; // 账号
    char password[10]; // 密码

    char name[10]; // 别名

    HEAD_FRIEND friend; // 朋友链表
    HEAD_GROUP group; // 群链表

    struct use* unext; // 下一个用户


} USE;


//结构体，用来分辨是信息还是心跳，以及信息长度
typedef struct
{
    int typ;     // 0代表此信息为执行信息，1为普通消息
    int form; // 0为心跳，1为真实信息·  
    int len;  // 信息长度
}TYD;

//心跳线程
void* func_hrartbest(void * arg)
{
    int sockfd = *(int*)arg;

    TYD best;
    best.form = 0;
    best.len = 0;
    best.typ = 0;

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


#endif