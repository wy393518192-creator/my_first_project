// /*
// *客户端
// */

#include "cctype.h"

#define VISITOR 0
#define USER    1

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

static int send_all(int fd, const void* data, size_t len)
{
    const char* p = (const char*)data;
    while (len > 0)
    {
        ssize_t n = send(fd, p, len, 0);
        if (n <= 0)
        {
            return -1;
        }
        p += n;
        len -= (size_t)n;
    }
    return 0;
}

// 发送一个命令 + 负载（单帧：TYD头 + chose + payload）
void func_send_cmd(int sockfd, int chose, void* payload, int payload_len)
{
    TYD bag = {0};
    bag.typ = VISITOR;
    bag.form = 1;
    bag.len = payload_len;

    send_all(sockfd, &bag, sizeof(bag));
    send_all(sockfd, &chose, sizeof(chose));
    if (payload_len > 0 && payload != NULL)
    {
        send_all(sockfd, payload, (size_t)payload_len);
    }
}

// 心跳线程
void* func_hrartbest(void* arg)
{
    int sockfd = *(int*)arg;

    TYD best;
    best.form = 0;
    best.len = 0;
    best.typ = 0;

    while (1)
    {
        if (send(sockfd, &best, sizeof(best), 0) <= 0)
        {
            break;
        }
        sleep(5);
    }

    return NULL;
}

static void input_user_info(USER_INFO* info)
{
    printf("账号：");
    scanf("%9s", info->account);
    while (getchar() != '\n');

    printf("密码：");
    scanf("%9s", info->password);
    while (getchar() != '\n');
}

// ---------------- 接收线程 + 响应信箱 ----------------

// 命令响应信箱：接收线程把命令回执放这里，主线程来取
static char g_resp_buff[4096];
static int g_resp_ready = 0;
static pthread_mutex_t g_resp_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_resp_cond = PTHREAD_COND_INITIALIZER;

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

// 接收线程：统一收所有帧，按类型分发
// - form==0 心跳：忽略
// - typ==USER 聊天消息：直接打印
// - typ==VISITOR 命令回执：写入信箱，唤醒主线程
static void* recv_thread_func(void* arg)
{
    int sockfd = *(int*)arg;
    char frame_buff[4096];

    while (1)
    {
        TYD bag = {0};
        if (recv_exact(sockfd, &bag, sizeof(bag)) < 0)
        {
            printf("\n[系统] 与服务器断开连接\n");
            exit(0);
        }

        if (bag.form == 0)
        {
            continue; // 心跳帧，忽略
        }

        int body_len = bag.len;
        if (body_len < 0) body_len = 0;
        if (body_len >= (int)sizeof(frame_buff)) body_len = sizeof(frame_buff) - 1;

        memset(frame_buff, 0, sizeof(frame_buff));
        if (body_len > 0 && recv_exact(sockfd, frame_buff, (size_t)body_len) < 0)
        {
            printf("\n[系统] 接收数据失败，连接断开\n");
            exit(0);
        }
        frame_buff[body_len] = '\0';

        if (bag.typ == USER)
        {
            // 聊天消息：直接显示，不阻塞主流程
            printf("\n[消息] %s\n", frame_buff);
            printf("请输入：");
            fflush(stdout);
        }
        else if (bag.typ == VISITOR)
        {
            // 命令回执：写入信箱，通知主线程
            pthread_mutex_lock(&g_resp_mutex);
            memset(g_resp_buff, 0, sizeof(g_resp_buff));
            strncpy(g_resp_buff, frame_buff, sizeof(g_resp_buff) - 1);
            g_resp_ready = 1;
            pthread_cond_signal(&g_resp_cond);
            pthread_mutex_unlock(&g_resp_mutex);
        }
    }

    return NULL;
}

// 主线程等待一次命令回执（阻塞，直到接收线程收到 VISITOR 帧）
static int wait_response(char* out_buff, int out_size)
{
    pthread_mutex_lock(&g_resp_mutex);
    while (!g_resp_ready)
    {
        pthread_cond_wait(&g_resp_cond, &g_resp_mutex);
    }
    g_resp_ready = 0;
    memset(out_buff, 0, (size_t)out_size);
    strncpy(out_buff, g_resp_buff, (size_t)out_size - 1);
    pthread_mutex_unlock(&g_resp_mutex);
    return 0;
}

// ---------------- 聊天循环 ----------------

static void client_private_chat_loop(int sockfd, const char* target)
{
    char msg[256] = {0};
    CHAT_PAYLOAD payload = {0};

    printf("已进入与 [%s] 的私聊，输入 /exit 退出私聊\n", target);

    while (1)
    {
        printf("请输入：");
        if (fgets(msg, sizeof(msg), stdin) == NULL)
        {
            break;
        }
        msg[strcspn(msg, "\n")] = '\0';

        if (strcmp(msg, "/exit") == 0)
        {
            printf("已退出私聊\n");
            break;
        }

        if (msg[0] == '\0')
        {
            continue;
        }

        memset(&payload, 0, sizeof(payload));
        snprintf(payload.target, sizeof(payload.target), "%s", target);
        snprintf(payload.msg, sizeof(payload.msg), "%s", msg);

        func_send_cmd(sockfd, 13, &payload, sizeof(payload));
    }
}

static void client_group_chat_loop(int sockfd, const char* gname)
{
    char msg[256] = {0};
    CHAT_PAYLOAD payload = {0};

    printf("已进入群 [%s] 的群聊，输入 /exit 退出群聊\n", gname);

    while (1)
    {
        printf("请输入：");
        if (fgets(msg, sizeof(msg), stdin) == NULL)
        {
            break;
        }
        msg[strcspn(msg, "\n")] = '\0';

        if (strcmp(msg, "/exit") == 0)
        {
            printf("已退出群聊\n");
            break;
        }

        if (msg[0] == '\0')
        {
            continue;
        }

        memset(&payload, 0, sizeof(payload));
        snprintf(payload.target, sizeof(payload.target), "%s", gname);
        snprintf(payload.msg, sizeof(payload.msg), "%s", msg);

        func_send_cmd(sockfd, 14, &payload, sizeof(payload));
    }
}

// 登录后菜单：查看个人信息、添加好友、创建群、加入群、私聊、群聊、服务器命令查询
static int client_login_menu(int sockfd, USER_INFO* info)
{
    char buff[4096] = {0};

    while (1)
    {
        int inner = 0;
        printf("---------------------登录后界面-----------------------\n");
        printf("1.查看个人信息\n");
        printf("2.添加好友\n");
        printf("3.创建群\n");
        printf("4.加入群\n");
        printf("5.私聊\n");
        printf("6.群聊\n");
        printf("7.服务器命令查询\n");
        printf("8.退出登录\n");
        printf("-----------------------------------------------------\n");

        printf("请输入你的选择：");
        scanf("%d", &inner);
        while (getchar() != '\n');

        if (inner == 8)
        {
            return 0;
        }

        if (inner == 1)
        {
            func_send_cmd(sockfd, 15, NULL, 0);
            wait_response(buff, sizeof(buff));
            printf("%s\n", buff);
            continue;
        }

        if (inner == 2)
        {
            char friend_name[10] = {0};
            printf("请输入要添加的好友账号：");
            scanf("%9s", friend_name);
            while (getchar() != '\n');

            func_send_cmd(sockfd, 10, friend_name, sizeof(friend_name));
            wait_response(buff, sizeof(buff));
            printf("%s\n", buff);
            continue;
        }

        if (inner == 3)
        {
            char gname[10] = {0};
            printf("请输入要创建的群账号：");
            scanf("%9s", gname);
            while (getchar() != '\n');

            func_send_cmd(sockfd, 11, gname, sizeof(gname));
            wait_response(buff, sizeof(buff));
            printf("%s\n", buff);
            continue;
        }

        if (inner == 4)
        {
            char gname[10] = {0};
            printf("请输入要加入的群账号：");
            scanf("%9s", gname);
            while (getchar() != '\n');

            func_send_cmd(sockfd, 12, gname, sizeof(gname));
            wait_response(buff, sizeof(buff));
            printf("%s\n", buff);
            continue;
        }

        if (inner == 5)
        {
            char target[10] = {0};

            printf("请输入私聊对方账号：");
            scanf("%9s", target);
            while (getchar() != '\n');

            client_private_chat_loop(sockfd, target);
            continue;
        }

        if (inner == 6)
        {
            char gname[10] = {0};

            printf("请输入群账号：");
            scanf("%9s", gname);
            while (getchar() != '\n');

            client_group_chat_loop(sockfd, gname);
            continue;
        }

        if (inner == 7)
        {
            char cmd[256] = {0};

            printf("请输入服务器命令：");
            fgets(cmd, sizeof(cmd), stdin);
            cmd[strcspn(cmd, "\n")] = '\0';

            if (cmd[0] == '\0')
            {
                printf("命令为空\n");
                continue;
            }

            func_send_cmd(sockfd, 16, cmd, sizeof(cmd));
            wait_response(buff, sizeof(buff));
            printf("%s\n", buff);
            continue;
        }
    }

    return 0;
}

int main(void)
{
    TYD bag = {0};

    int sockfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sockfd < 0)
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

    int res = connect(sockfd, (struct sockaddr*)&addr, sizeof(addr));
    if (res != 0)
    {
        perror("connect error!");
        close(sockfd);
        return 1;
    }

    // 读欢迎消息（直接读，此时接收线程还没启动）
    char buff[4096] = {0};
    if (recv(sockfd, &bag, sizeof(bag), 0) < 0)
    {
        perror("the first time error!");
        close(sockfd);
        return 1;
    }

    if (recv(sockfd, buff, bag.len, 0) < 0)
    {
        perror("second time around error!");
        close(sockfd);
        return 1;
    }

    printf("%s\n", buff);
    memset(&bag, 0, sizeof(bag));
    memset(buff, 0, sizeof(buff));

    // 启动心跳线程
    pthread_t tid;
    pthread_create(&tid, NULL, func_hrartbest, (void*)&sockfd);

    // 启动接收线程：统一处理所有服务器下行帧
    pthread_t recv_tid;
    pthread_create(&recv_tid, NULL, recv_thread_func, (void*)&sockfd);

    int logo = 1;
    while (logo)
    {
        int chose = 0;
        printf("---------------------客户界面-----------------------\n");
        printf("1.登录\n");
        printf("2.注册\n");
        printf("3.退出\n");
        printf("-----------------------------------------------------\n");
        printf("请输入你的选择：");
        scanf("%d", &chose);
        while (getchar() != '\n');

        switch (chose)
        {
            case 1:
            {
                USER_INFO info = {0};
                int fail_count = 0;

                while (fail_count < 3)
                {
                    input_user_info(&info);

                    func_send_cmd(sockfd, 1, &info, sizeof(info));
                    wait_response(buff, sizeof(buff));

                    if (strcmp(buff, "login success") == 0)
                    {
                        printf("登录成功\n");
                        client_login_menu(sockfd, &info);
                        break;
                    }

                    printf("%s\n", buff);
                    fail_count++;

                    if (fail_count >= 3)
                    {
                        printf("登录失败次数已达3次，客户端退出\n");
                        close(sockfd);
                        return 0;
                    }
                }
                break;
            }

            case 2:
            {
                USER_INFO info = {0};
                input_user_info(&info);

                func_send_cmd(sockfd, 2, &info, sizeof(info));
                wait_response(buff, sizeof(buff));
                printf("%s\n", buff);
                break;
            }

            case 3:
                logo = 0;
                printf("程序退出\n");
                break;

            default:
                break;
        }
    }

    close(sockfd);
    return 0;
}
