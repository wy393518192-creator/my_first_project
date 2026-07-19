/*
*服务器
*/

#include "stype.h"
#include "user.h"

#include <signal.h>



// 条件锁，互斥锁
pthread_cond_t cond;
pthread_mutex_t mutex;




// 定义一个指向客户端信息的头指针（不存储数据）
SOC head = {0};

// 定义一个客户账号信息的头节点（不存储数据）
USE head_user = {0};

// 全局群注册表（只保存群账号，不重复保存）
GROUP g_group_head = {0};

// 全局服务器socket
int g_server_sock = -1;

// 日志文件
FILE* g_log_fp = NULL;

// 下面这些函数都不要写 static，后续你要拆到其他文件里



// 统一清理函数：正常退出 / Ctrl+C 强制退出都统一保存
void cleanup_and_exit(int code)
{
    save_user_data();

    free_client_list();
    free_user_list();

    if (g_log_fp != NULL)
    {
        fprintf(g_log_fp, "服务器关闭，状态码=%d\n", code);
        fflush(g_log_fp);
        fclose(g_log_fp);
        g_log_fp = NULL;
    }

    if (g_server_sock >= 0)
    {
        close(g_server_sock);
        g_server_sock = -1;
    }

    pthread_cond_destroy(&cond);
    pthread_mutex_destroy(&mutex);
    exit(code);
}

// Ctrl+C 和 SIGTERM 退出时统一调用保存
void sig_handler(int sig)
{
    (void)sig;
    cleanup_and_exit(0);
}








int main(void)
{
    // 加载数据到全局head_user（已注册用户）
    func_user();

    pthread_cond_init(&cond, NULL);
    pthread_mutex_init(&mutex, NULL);

    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);

    g_log_fp = fopen("log.txt", "a");
    if (g_log_fp == NULL)
    {
        perror("log open error");
        return 1;
    }

    int result = 0;
    pthread_t ttid = 0;
    result = pthread_create(&ttid, NULL, (void*(*)(void*))monitor_func, NULL);
    if (result != 0)
    {
        perror("pthread_create monitor fail");
        return 1;
    }

    g_server_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (g_server_sock < 0)
    {
        perror("socket error!");
        cleanup_and_exit(1);
    }

    IP_CF ip_cf = func_ipconfig();

    struct sockaddr_in addr = {0};
    socklen_t len = sizeof(addr);
    addr.sin_family = AF_INET;
    addr.sin_port = htons(ip_cf.port);
    addr.sin_addr.s_addr = inet_addr(ip_cf.buff);

    if (bind(g_server_sock, (struct sockaddr*)&addr, len) < 0)
    {
        perror("bind error!");
        cleanup_and_exit(1);
    }

    if (listen(g_server_sock, 50) < 0)
    {
        perror("listen error!");
        cleanup_and_exit(1);
    }

    printf("服务器已启动，监听中...\n");

    while (1)
    {
        struct sockaddr_in addr_client = {0};
        socklen_t len_client = sizeof(addr_client);

        int sockfd_client = accept(g_server_sock, (struct sockaddr*)&addr_client, &len_client);
        if (sockfd_client < 0)
        {
            perror("accept error!");
            continue;
        }

        SOC* ptr = (SOC*)calloc(1, sizeof(SOC));
        if (ptr == NULL)
        {
            perror("malloc error!");
            close(sockfd_client);
            continue;
        }

        ptr->sockfd = sockfd_client;
        ptr->ti = time(NULL);
        ptr->online = 0;
        strcpy(ptr->ip, inet_ntoa(addr_client.sin_addr));
        ptr->port = ntohs(addr_client.sin_port);
        ptr->next = NULL;

        pthread_t tid = 0;
        if (pthread_create(&tid, NULL, func_client, (void*)ptr) != 0)
        {
            perror("pthread_create error");
            close(sockfd_client);
            free(ptr);
            continue;
        }

        ptr->tid = tid;
        node_process(&head, ptr);
    }

    return 0;
}