/*
*服务器
*/
#ifndef STYPE_H
#define STYPE_H

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

#define VISITOR 0 // 最外层功能命令
#define USER    1 // 普通文本消息

// 最大心跳时间间隔
#define MAX_TIME 15.0


// 真正的 群 的节点
typedef struct group
{
    char gname[10]; // 群名字，也就是群账号
    struct group* gnext;
} GROUP;

// 账号中 群 头节点，不存储数据
typedef struct
{
    int gnum; // 群数量
    GROUP* gnext; // 指向第一个群
} HEAD_GROUP;

// 真正的 朋友 的节点
typedef struct friend
{
    char fname[10]; // 朋友名字，也就是朋友账号
    struct friend* fnext;
} FRIEND;

// 账号中 朋友 头节点，不存储数据
typedef struct
{
    int fnum; // 朋友数量
    FRIEND* fnext; // 指向第一个朋友
} HEAD_FRIEND;

// 账号结构体
typedef struct use
{
    char account[10]; // 账号
    char password[10]; // 密码

    char name[10]; // 别名

    HEAD_FRIEND friend; // 朋友链表
    HEAD_GROUP group;   // 群链表

    struct use* unext; // 下一个用户
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
    int typ;   // 0代表此信息为执行信息，1为普通消息
    int form;  // 0为心跳，1为真实信息
    int len;   // 信息长度
} TYD;

// 客户端节点
typedef struct clinet_node
{
    int sockfd; // accept之后，与客户端的套接字
    pthread_t tid; // 所处线程id

    char ip[16]; // 客户端IP
    int port; // 客户端的port端口

    time_t ti; // 上次心跳时间
    int online; // 在线状态，1在线，0访客
    char account[10]; // 在线时，本成员才有意义

    int form; // 0为心跳，1为真实信息
    int len;  // 信息长度

    struct clinet_node* next;
} SOC;

// 从配置文件中读取本服务器的ip和端口
typedef struct
{
    char buff[16]; // 配置文件中的IP地址
    int port;      // 配置文件中的port端口
} IP_CF;

#endif