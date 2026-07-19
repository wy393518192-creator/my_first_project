/*
*服务器
*/

#include "stype.h"
#include "user.h"

#include <signal.h>

#define VISITOR 0 // 最外层功能命令
#define USER    1 // 普通文本消息

// 条件锁，互斥锁
pthread_cond_t cond;
pthread_mutex_t mutex;

// 最大心跳时间间隔
#define MAX_TIME 15.0

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

// 释放好友链表
void free_friend_list(FRIEND* list)
{
    FRIEND* p = list;
    while (p != NULL)
    {
        FRIEND* q = p->fnext;
        free(p);
        p = q;
    }
}

// 释放群链表
void free_group_list(GROUP* list)
{
    GROUP* p = list;
    while (p != NULL)
    {
        GROUP* q = p->gnext;
        free(p);
        p = q;
    }
}

// 释放全部用户链表
void free_user_list(void)
{
    USE* p = head_user.unext;
    while (p != NULL)
    {
        USE* q = p->unext;
        free_friend_list(p->friend.fnext);
        free_group_list(p->group.gnext);
        free(p);
        p = q;
    }
    head_user.unext = NULL;
}

// 释放全部在线客户端链表
void free_client_list(void)
{
    SOC* p = head.next;
    while (p != NULL)
    {
        SOC* q = p->next;
        close(p->sockfd);
        free(p);
        p = q;
    }
    head.next = NULL;
}

// 保存用户数据
void save_user_data(void)
{
    FILE* fp = fopen("./../data/userdata.txt", "wb");
    if (fp == NULL)
    {
        fprintf(stderr, "save_user_data open fail\n");
        return;
    }

    USE* u = head_user.unext;
    while (u != NULL)
    {
        fwrite(u, sizeof(USE), 1, fp);

        FRIEND* f = u->friend.fnext;
        while (f != NULL)
        {
            fwrite(f, sizeof(FRIEND), 1, fp);
            f = f->fnext;
        }

        GROUP* g = u->group.gnext;
        while (g != NULL)
        {
            fwrite(g, sizeof(GROUP), 1, fp);
            g = g->gnext;
        }

        u = u->unext;
    }

    fclose(fp);
}

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

// 从文件加载所有用户和其好友、群信息
void func_user(void)
{
    FILE* fp = fopen("./../data/userdata.txt", "rb");
    if (fp == NULL)
    {
        printf("数据文件不存在，按空用户表启动\n");
        return;
    }

    USE temp = {0};
    while (fread(&temp, sizeof(USE), 1, fp) == 1)
    {
        USE* node = (USE*)calloc(1, sizeof(USE));
        if (node == NULL)
        {
            perror("calloc use error");
            continue;
        }

        *node = temp;

        // 先恢复好友链表
        for (int i = 0; i < temp.friend.fnum; i++)
        {
            FRIEND* f = (FRIEND*)calloc(1, sizeof(FRIEND));
            if (f == NULL)
            {
                perror("calloc friend error");
                continue;
            }
            fread(f, sizeof(FRIEND), 1, fp);
            f->fnext = node->friend.fnext;
            node->friend.fnext = f;
        }

        // 再恢复群链表
        for (int i = 0; i < temp.group.gnum; i++)
        {
            GROUP* g = (GROUP*)calloc(1, sizeof(GROUP));
            if (g == NULL)
            {
                perror("calloc group error");
                continue;
            }
            fread(g, sizeof(GROUP), 1, fp);
            g->gnext = node->group.gnext;
            node->group.gnext = g;
        }

        // 接到用户链表尾部
        USE* last = &head_user;
        while (last->unext != NULL)
        {
            last = last->unext;
        }
        last->unext = node;
    }

    fclose(fp);
}

// 把客户端节点加入链表
void node_process(SOC* head, SOC* new_node)
{
    pthread_mutex_lock(&mutex);

    new_node->next = head->next;
    head->next = new_node;

    pthread_mutex_unlock(&mutex);
}

// 根据账号查找用户
USE* find_user(const char* account)
{
    USE* p = head_user.unext;
    while (p != NULL)
    {
        if (strcmp(p->account, account) == 0)
            return p;
        p = p->unext;
    }
    return NULL;
}

// 检查群名是否存在
GROUP* find_group(const char* gname)
{
    GROUP* p = g_group_head.gnext;
    while (p != NULL)
    {
        if (strcmp(p->gname, gname) == 0)
            return p;
        p = p->gnext;
    }
    return NULL;
}

// 向全局群注册表中插入群账号
void add_group_registry(const char* gname)
{
    if (find_group(gname) != NULL)
        return;

    GROUP* node = (GROUP*)calloc(1, sizeof(GROUP));
    if (node == NULL)
        return;

    snprintf(node->gname, sizeof(node->gname), "%s", gname);

    node->gnext = g_group_head.gnext;
    g_group_head.gnext = node;
}

// 注册用户
int register_user(const char* account, const char* password, const char* name)
{
    if (find_user(account) != NULL)
        return -1;

    USE* node = (USE*)calloc(1, sizeof(USE));
    if (node == NULL)
        return -2;

    snprintf(node->account, sizeof(node->account), "%s", account);
    snprintf(node->password, sizeof(node->password), "%s", password);
    snprintf(node->name, sizeof(node->name), "%s", name);

    node->friend.fnum = 0;
    node->friend.fnext = NULL;
    node->group.gnum = 0;
    node->group.gnext = NULL;

    USE* last = &head_user;
    while (last->unext != NULL)
    {
        last = last->unext;
    }
    last->unext = node;

    save_user_data();
    return 0;
}

// 登录用户
int login_user(SOC* ptr, const char* account, const char* password)
{
    USE* user = find_user(account);
    if (user == NULL)
        return 1;

    if (strcmp(user->password, password) != 0)
        return 2;

    snprintf(ptr->account, sizeof(ptr->account), "%s", account);
    ptr->online = 1;

    return 0;
}

// 追加好友
int add_friend_to_user(const char* account, const char* friend_name)
{
    USE* user = find_user(account);
    if (user == NULL)
        return -1;

    // 不能重复添加
    FRIEND* p = user->friend.fnext;
    while (p != NULL)
    {
        if (strcmp(p->fname, friend_name) == 0)
            return -2;
        p = p->fnext;
    }

    FRIEND* node = (FRIEND*)calloc(1, sizeof(FRIEND));
    if (node == NULL)
        return -3;

    snprintf(node->fname, sizeof(node->fname), "%s", friend_name);
    node->fnext = user->friend.fnext;
    user->friend.fnext = node;
    user->friend.fnum++;

    save_user_data();
    return 0;
}

// 创建群: 只创建群账号名字，放入当前用户的群列表中
int create_group_for_user(const char* account, const char* gname)
{
    USE* user = find_user(account);
    if (user == NULL)
        return -1;

    // 不允许重复创建同名群
    GROUP* p = user->group.gnext;
    while (p != NULL)
    {
        if (strcmp(p->gname, gname) == 0)
            return -2;
    }

    GROUP* node = (GROUP*)calloc(1, sizeof(GROUP));
    if (node == NULL)
        return -3;

    snprintf(node->gname, sizeof(node->gname), "%s", gname);
    node->gnext = user->group.gnext;
    user->group.gnext = node;
    user->group.gnum++;

    add_group_registry(gname);

    save_user_data();
    return 0;
}

// 加入群：用户加入某个群账号
int join_group_for_user(const char* account, const char* gname)
{
    USE* user = find_user(account);
    if (user == NULL)
        return -1;

    // 如果这个群名不存在于注册表，不能加入
    if (find_group(gname) == NULL)
        return -2;

    // 不允许重复加入
    GROUP* p = user->group.gnext;
    while (p != NULL)
    {
        if (strcmp(p->gname, gname) == 0)
            return -3;
        p = p->gnext;
    }

    GROUP* node = (GROUP*)calloc(1, sizeof(GROUP));
    if (node == NULL)
        return -4;

    snprintf(node->gname, sizeof(node->gname), "%s", gname);
    node->gnext = user->group.gnext;
    user->group.gnext = node;
    user->group.gnum++;

    save_user_data();
    return 0;
}

// 生成个人信息字符串：账号、名字、好友列表、群列表
char* build_profile_string(const char* account)
{
    static char buf[2048];
    memset(buf, 0, sizeof(buf));

    USE* user = find_user(account);
    if (user == NULL)
    {
        snprintf(buf, sizeof(buf), "account not found");
        return buf;
    }

    snprintf(buf, sizeof(buf),
        "account=%s\nname=%s\nfriend_count=%d\ngroup_count=%d\n",
        user->account, user->name, user->friend.fnum, user->group.gnum);

    strncat(buf, "friends:", sizeof(buf) - strlen(buf) - 1);
    FRIEND* f = user->friend.fnext;
    while (f != NULL)
    {
        strncat(buf, f->fname, sizeof(buf) - strlen(buf) - 1);
        strncat(buf, ",", sizeof(buf) - strlen(buf) - 1);
        f = f->fnext;
    }

    strncat(buf, "\ngroups:", sizeof(buf) - strlen(buf) - 1);
    GROUP* g = user->group.gnext;
    while (g != NULL)
    {
        strncat(buf, g->gname, sizeof(buf) - strlen(buf) - 1);
        strncat(buf, ",", sizeof(buf) - strlen(buf) - 1);
        g = g->gnext;
    }

    return buf;
}

// 给客户端发送文本
void send_text(SOC* ptr, const char* msg)
{
    TYD bag = {0};
    bag.form = 1;
    bag.typ = 0;
    bag.len = (int)strlen(msg) + 1;

    send(ptr->sockfd, &bag, sizeof(bag), 0);
    send(ptr->sockfd, msg, bag.len, 0);
}

// 登录后的二级功能：私聊、群聊、查看信息、添加好友、建群、加群
void* func_client(void* arg)
{
    SOC* ptr = (SOC*)arg;

    TYD bag = {0};
    send_text(ptr, "link successful...");

    while (1)
    {
        int res = recv(ptr->sockfd, &bag, sizeof(bag), 0);
        if (res <= 0)
        {
            close(ptr->sockfd);
            break;
        }

        // 心跳包
        if (bag.form == 0)
        {
            pthread_mutex_lock(&mutex);
            ptr->ti = time(NULL);
            pthread_mutex_unlock(&mutex);
            continue;
        }

        if (bag.typ == VISITOR)
        {
            int chose = 0;
            recv(ptr->sockfd, &chose, sizeof(chose), 0);

            switch (chose)
            {
                case 1: // 登录
                {
                    USER_INFO info = {0};
                    recv(ptr->sockfd, &info, sizeof(info), 0);

                    int status = login_user(ptr, info.account, info.password);
                    if (status == 0)
                    {
                        send_text(ptr, "login success");
                    }
                    else if (status == 1)
                    {
                        send_text(ptr, "account not found");
                    }
                    else if (status == 2)
                    {
                        send_text(ptr, "password wrong");
                    }
                    break;
                }

                case 2: // 注册
                {
                    USER_INFO info = {0};
                    recv(ptr->sockfd, &info, sizeof(info), 0);

                    int status = register_user(info.account, info.password, info.account);
                    if (status == 0)
                    {
                        send_text(ptr, "register success");
                    }
                    else if (status == -1)
                    {
                        send_text(ptr, "account exists");
                    }
                    else
                    {
                        send_text(ptr, "register fail");
                    }
                    break;
                }

                case 3: // 退出
                    send_text(ptr, "bye");
                    close(ptr->sockfd);
                    return NULL;

                // 登录后功能：
                case 10: // 添加好友
                {
                    char friend_name[10] = {0};
                    recv(ptr->sockfd, friend_name, sizeof(friend_name), 0);
                    int ret = add_friend_to_user(ptr->account, friend_name);
                    if (ret == 0)
                        send_text(ptr, "add friend success");
                    else if (ret == -1)
                        send_text(ptr, "user not found");
                    else if (ret == -2)
                        send_text(ptr, "friend already exists");
                    else
                        send_text(ptr, "add friend fail");
                    break;
                }

                case 11: // 创建群
                {
                    char gname[10] = {0};
                    recv(ptr->sockfd, gname, sizeof(gname), 0);
                    int ret = create_group_for_user(ptr->account, gname);
                    if (ret == 0)
                        send_text(ptr, "create group success");
                    else if (ret == -1)
                        send_text(ptr, "user not found");
                    else if (ret == -2)
                        send_text(ptr, "group already exists");
                    else
                        send_text(ptr, "create group fail");
                    break;
                }

                case 12: // 加入群
                {
                    char gname[10] = {0};
                    recv(ptr->sockfd, gname, sizeof(gname), 0);
                    int ret = join_group_for_user(ptr->account, gname);
                    if (ret == 0)
                        send_text(ptr, "join group success");
                    else if (ret == -1)
                        send_text(ptr, "user not found");
                    else if (ret == -2)
                        send_text(ptr, "group not found");
                    else if (ret == -3)
                        send_text(ptr, "already joined");
                    else
                        send_text(ptr, "join group fail");
                    break;
                }

                case 13: // 私聊（指定好友）
                {
                    char target[10] = {0};
                    char msg[256] = {0};

                    recv(ptr->sockfd, target, sizeof(target), 0);
                    recv(ptr->sockfd, msg, sizeof(msg), 0);

                    if (find_user(target) == NULL)
                    {
                        send_text(ptr, "target not found");
                    }
                    else
                    {
                        send_text(ptr, "private chat ready");
                    }
                    break;
                }

                case 14: // 群聊（指定群）
                {
                    char gname[10] = {0};
                    char msg[256] = {0};

                    recv(ptr->sockfd, gname, sizeof(gname), 0);
                    recv(ptr->sockfd, msg, sizeof(msg), 0);

                    if (find_group(gname) == NULL)
                    {
                        send_text(ptr, "group not found");
                    }
                    else
                    {
                        send_text(ptr, "group chat ready");
                    }
                    break;
                }

                case 15: // 个人信息
                {
                    char* info = build_profile_string(ptr->account);
                    send_text(ptr, info);
                    break;
                }

                case 16: // 其他功能预留接口
                {
                    send_text(ptr, "other function interface reserved");
                    break;
                }

                default:
                    send_text(ptr, "unknown command");
                    break;
            }
        }
        else
        {
            char buff[1024] = {0};
            recv(ptr->sockfd, buff, bag.len, 0);
            printf("真实信息：%s\n", buff);
        }
    }

    return NULL;
}

// 监控心跳是否超时
void monitor_func(void* arg)
{
    (void)arg;

    while (1)
    {
        sleep(1);
        pthread_mutex_lock(&mutex);

        SOC* ptr = head.next;
        SOC* pre = &head;
        while (ptr != NULL)
        {
            if (time(NULL) - ptr->ti > MAX_TIME)
            {
                SOC* q = ptr;
                pre->next = ptr->next;
                ptr = ptr->next;

                close(q->sockfd);
                free(q);
                continue;
            }

            pre = ptr;
            ptr = ptr->next;
        }

        pthread_mutex_unlock(&mutex);
    }
}

// 获取配置文件中的值，来设置服务器的IP和端口
IP_CF func_ipconfig(void)
{
    FILE* fptr = fopen("./../server_ipconfigfile.txt", "r");
    if (fptr == NULL)
    {
        printf("fopen error!\n");
        exit(1);
    }

    IP_CF temp = {0};
    fscanf(fptr, "ip = %s", temp.buff);
    fscanf(fptr, "port = %d", &(temp.port));

    fclose(fptr);
    return temp;
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