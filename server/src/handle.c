#include "stype.h"
#include "user.h"

extern USE head_user;
extern SOC head;
extern pthread_cond_t cond;
extern pthread_mutex_t mutex;
extern GROUP g_group_head;

// 精确读取 len 字节，防止 TCP 拆包导致读不全
static int recv_exact(int fd, void* data, size_t len)
{
    char* p = (char*)data;
    while (len > 0)
    {
        ssize_t n = recv(fd, p, len, 0);
        if (n <= 0)
        {
            return -1;
        }
        p += n;
        len -= (size_t)n;
    }
    return 0;
}

// 发送锁：多个线程同时向客户端推送消息时，防止帧交错
static pthread_mutex_t g_send_mutex = PTHREAD_MUTEX_INITIALIZER;




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

// 创建群
int create_group_for_user(const char* account, const char* gname)
{
    USE* user = find_user(account);
    if (user == NULL)
        return -1;

    GROUP* p = user->group.gnext;
    while (p != NULL)
    {
        if (strcmp(p->gname, gname) == 0)
            return -2;
        p = p->gnext;
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

// 加入群
int join_group_for_user(const char* account, const char* gname)
{
    USE* user = find_user(account);
    if (user == NULL)
        return -1;

    if (find_group(gname) == NULL)
        return -2;

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

// 生成个人信息字符串
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

// 循环发送完整缓冲区，避免一次 send 只发出部分数据
// 逻辑：只要 len 还没发完，就一直把剩余字节继续发出去
static int send_all(int fd, const void* data, size_t len)
{
    const char* p = (const char*)data;
    while (len > 0)
    {
        ssize_t n = send(fd, p, len, 0);
        if (n <= 0)
        {
            // 发送失败或连接断开时返回 -1，外层做错误处理
            return -1;
        }
        p += n;
        len -= (size_t)n;
    }
    return 0;
}

// 按协议帧格式发送一包数据：先发头部 TYD，再发 body
// 其中 typ 表示命令还是普通消息，form 表示是否为真实数据包，len 表示 body 长度
static int send_frame(SOC* ptr, int typ, int form, const void* data, int len)
{
    TYD bag = {0};
    bag.form = form;
    bag.typ = typ;
    bag.len = len;

    int ret = 0;
    pthread_mutex_lock(&g_send_mutex);

    // 先把协议头部完整发出去
    if (send_all(ptr->sockfd, &bag, sizeof(bag)) < 0)
    {
        ret = -1;
    }
    // 如果有 body，就把 body 一并完整发出去
    else if (len > 0 && data != NULL && send_all(ptr->sockfd, data, (size_t)len) < 0)
    {
        ret = -1;
    }

    pthread_mutex_unlock(&g_send_mutex);
    return ret;
}

// 给客户端发送文本：把普通文本按命令协议帧的方式发送出去
// 这里用 VISITOR 类型表示这是一条命令/响应信息，而不是普通聊天文本
void send_text(SOC* ptr, const char* msg)
{
    if (msg == NULL)
    {
        // 防止空指针传入导致 strlen 崩溃，直接返回
        return;
    }

    int len = (int)strlen(msg) + 1;
    send_frame(ptr, VISITOR, 1, msg, len);
}

// 给客户端发送普通聊天消息：此类消息只发给目标用户，不广播给其它客户端
void send_user_message(SOC* ptr, const char* msg)
{
    if (msg == NULL)
    {
        return;
    }

    int len = (int)strlen(msg) + 1;
    send_frame(ptr, USER, 1, msg, len);
}

// 执行服务器命令并把输出缓冲后一次性返回给客户端
// 逻辑：先完整读完本次命令的输出，拼成一个字符串，再作为单帧回执发送
// 这样每次命令的结果独立完整，不会和上一次的残留混在一起
int execute_server_command(SOC* ptr, const char* cmd)
{
    if (cmd == NULL || cmd[0] == '\0')
    {
        send_text(ptr, "command empty");
        return -1;
    }

    FILE* fp = popen(cmd, "r");
    if (fp == NULL)
    {
        send_text(ptr, "popen fail");
        return -1;
    }

    char out_buff[3600] = {0};
    size_t out_len = 0;
    char chunk[512] = {0};
    size_t nread = 0;

    // 分块读取本次命令的全部输出，拼接到 out_buff
    while ((nread = fread(chunk, 1, sizeof(chunk), fp)) > 0)
    {
        if (out_len + nread >= sizeof(out_buff))
        {
            // 超出缓冲就截断，防止溢出
            size_t remain = sizeof(out_buff) - out_len - 1;
            memcpy(out_buff + out_len, chunk, remain);
            out_len += remain;
            break;
        }
        memcpy(out_buff + out_len, chunk, nread);
        out_len += nread;
    }
    out_buff[out_len] = '\0';

    pclose(fp);

    if (out_len == 0)
    {
        send_text(ptr, "command no output");
        return 0;
    }

    // 作为单帧命令回执发送（客户端 wait_response 只取一帧）
    send_text(ptr, out_buff);
    return 0;
}

// 把普通聊天消息发给某个在线账号
static int send_text_to_account(const char* account, const char* msg)
{
    SOC* p = head.next;
    while (p != NULL)
    {
        if (strcmp(p->account, account) == 0)
        {
            send_user_message(p, msg);
            return 0;
        }
        p = p->next;
    }
    return -1;
}

// 把群聊消息广播给当前群内的在线成员
static int broadcast_group_message(const char* gname, const char* msg)
{
    SOC* p = head.next;
    int count = 0;

    while (p != NULL)
    {
        USE* user = find_user(p->account);
        if (user != NULL)
        {
            GROUP* g = user->group.gnext;
            while (g != NULL)
            {
                if (strcmp(g->gname, gname) == 0)
                {
                    send_user_message(p, msg);
                    count++;
                    break;
                }
                g = g->gnext;
            }
        }
        p = p->next;
    }

    return count;
}

// 处理登录后命令：添加好友 / 创建群 / 加入群 / 私聊 / 群聊 / 个人信息 / 命令查询
static int handle_login_subcmd(SOC* ptr, int chose)
{
    switch (chose)
    {
        case 10: // 添加好友
        {
            char friend_name[10] = {0};
            if (recv_exact(ptr->sockfd, friend_name, sizeof(friend_name)) < 0)
                return -1;

            int ret = add_friend_to_user(ptr->account, friend_name);
            if (ret == 0)
                send_text(ptr, "add friend success");
            else if (ret == -1)
                send_text(ptr, "user not found");
            else if (ret == -2)
                send_text(ptr, "friend already exists");
            else
                send_text(ptr, "add friend fail");
            return 0;
        }

        case 11: // 创建群
        {
            char gname[10] = {0};
            if (recv_exact(ptr->sockfd, gname, sizeof(gname)) < 0)
                return -1;

            int ret = create_group_for_user(ptr->account, gname);
            if (ret == 0)
                send_text(ptr, "create group success");
            else if (ret == -1)
                send_text(ptr, "user not found");
            else if (ret == -2)
                send_text(ptr, "group already exists");
            else
                send_text(ptr, "create group fail");
            return 0;
        }

        case 12: // 加入群
        {
            char gname[10] = {0};
            if (recv_exact(ptr->sockfd, gname, sizeof(gname)) < 0)
                return -1;

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
            return 0;
        }

        case 13: // 私聊
        {
            CHAT_PAYLOAD payload = {0};
            char forward_msg[512] = {0};

            if (recv_exact(ptr->sockfd, &payload, sizeof(payload)) < 0)
            {
                return -1;
            }

            // 保证字符串安全结尾
            payload.target[sizeof(payload.target) - 1] = '\0';
            payload.msg[sizeof(payload.msg) - 1] = '\0';

            if (find_user(payload.target) == NULL)
            {
                return 0; // 目标不存在，静默丢弃
            }

            snprintf(forward_msg, sizeof(forward_msg), "[私聊] %s: %s", ptr->account, payload.msg);
            send_text_to_account(payload.target, forward_msg);
            return 0;
        }

        case 14: // 群聊
        {
            CHAT_PAYLOAD payload = {0};
            char forward_msg[512] = {0};

            if (recv_exact(ptr->sockfd, &payload, sizeof(payload)) < 0)
            {
                return -1;
            }

            payload.target[sizeof(payload.target) - 1] = '\0';
            payload.msg[sizeof(payload.msg) - 1] = '\0';

            if (find_group(payload.target) == NULL)
            {
                return 0; // 群不存在，静默丢弃
            }

            snprintf(forward_msg, sizeof(forward_msg), "[群聊-%s] %s: %s",
                     payload.target, ptr->account, payload.msg);
            broadcast_group_message(payload.target, forward_msg);
            return 0;
        }

        case 15: // 查看个人信息
        {
            char* info = build_profile_string(ptr->account);
            send_text(ptr, info);
            return 0;
        }

        case 16: // 服务器命令查询
        {
            char cmd[256] = {0};
            if (recv_exact(ptr->sockfd, cmd, sizeof(cmd)) < 0)
                return -1;
            execute_server_command(ptr, cmd);
            return 0;
        }

        default:
            send_text(ptr, "unknown command");
            return -1;
    }
}

// 登录后的二级功能：私聊、群聊、查看信息、添加好友、建群、加群
void* func_client(void* arg)
{
    SOC* ptr = (SOC*)arg;

    TYD bag = {0};
    send_text(ptr, "link successful...");

    while (1)
    {
        if (recv_exact(ptr->sockfd, &bag, sizeof(bag)) < 0)
        {
            close(ptr->sockfd);
            break;
        }

        if (bag.form == 0)
        {
            time_t now = time(NULL);
            char time_buf[64] = {0};
            struct tm* tm_info = localtime(&now);
            strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", tm_info);

            printf("[%s] heartbeat from %s:%d\n", time_buf, ptr->ip, ptr->port);

            pthread_mutex_lock(&mutex);
            ptr->ti = now;
            pthread_mutex_unlock(&mutex);
            continue;
        }

        if (bag.typ == VISITOR)
        {
            int chose = 0;
            if (recv_exact(ptr->sockfd, &chose, sizeof(chose)) < 0)
            {
                close(ptr->sockfd);
                break;
            }

            if (chose == 1)
            {
                USER_INFO info = {0};
                if (recv_exact(ptr->sockfd, &info, sizeof(info)) < 0)
                {
                    close(ptr->sockfd);
                    break;
                }

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
                continue;
            }

            if (chose == 2)
            {
                USER_INFO info = {0};
                if (recv_exact(ptr->sockfd, &info, sizeof(info)) < 0)
                {
                    close(ptr->sockfd);
                    break;
                }

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
                continue;
            }

            if (chose == 3)
            {
                send_text(ptr, "bye");
                close(ptr->sockfd);
                return NULL;
            }

            if (ptr->online == 1)
            {
                handle_login_subcmd(ptr, chose);
                continue;
            }

            send_text(ptr, "please login first");
        }
        else
        {
            char buff[1024] = {0};
            int body_len = bag.len;
            if (body_len < 0) body_len = 0;
            if (body_len >= (int)sizeof(buff)) body_len = sizeof(buff) - 1;
            if (body_len > 0 && recv_exact(ptr->sockfd, buff, (size_t)body_len) < 0)
            {
                close(ptr->sockfd);
                break;
            }
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
    char line[64] = {0};

    while (fgets(line, sizeof(line), fptr) != NULL)
    {
        if (sscanf(line, "ip = %15s", temp.buff) == 1)
        {
            continue;
        }

        if (sscanf(line, "port = %d", &(temp.port)) == 1)
        {
            break;
        }
    }

    fclose(fptr);

    if (temp.buff[0] == '\0' || temp.port == 0)
    {
        printf("config parse error!\n");
        exit(1);
    }

    return temp;
}
