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
    char gname[10];
    struct group* gnext;
} GROUP;

// 账号中 群 头节点，不存储数据
typedef struct
{
    int gnum;
    GROUP* gnext;
} HEAD_GROUP;

// 真正的 朋友 的节点
typedef struct friend
{
    char fname[10];
    struct friend* fnext;
} FRIEND;

// 账号中 朋友 头节点，不存储数据
typedef struct
{
    int fnum;
    FRIEND* fnext;
} HEAD_FRIEND;

// 账号结构体
typedef struct use
{
    char account[10];
    char password[10];
    char name[10];

    HEAD_FRIEND friend;
    HEAD_GROUP group;

    struct use* unext;
} USE;

// 登录/注册请求载荷
typedef struct
{
    char account[10];
    char password[10];
} USER_INFO;

// 结构体，用来分辨是信息还是心跳，以及信息长度
typedef struct
{
    int typ;
    int form;
    int len;
} TYD;

// 心跳线程
void* func_hrartbest(void* arg);

typedef struct
{
    char buff[16];
    int port;
} IP_CF;

#endif