// /*
// *客户端
// */

#include "cctype.h"

#define VISITOR 0 // 最外层功能命令
#define USER    1 // 普通消息

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

// 发送一个命令 + 负载
void func_send_cmd(int sockfd, int chose, void* payload, int payload_len)
{
    TYD bag = {0};
    bag.typ = VISITOR;
    bag.form = 1;
    bag.len = payload_len;

    send(sockfd, &bag, sizeof(bag), 0);
    send(sockfd, &chose, sizeof(chose), 0);
    send(sockfd, payload, payload_len, 0);
}

// 发送普通文本
void func_send(int sockfd, TYD* bag, void* message, int msg_len)
{
    send(sockfd, bag, sizeof(TYD), 0);
    send(sockfd, message, msg_len, 0);
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
        send(sockfd, &best, sizeof(best), 0);
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

    /*
    先接收一次服务器消息，查看是否真正链接成功
    */
    char buff[1024] = {0};
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

    // 创建心跳线程，保持与服务器的链接状态
    pthread_t tid;
    pthread_create(&tid, NULL, func_hrartbest, (void*)&sockfd);

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

                    // 先发命令号，再发载荷
                    func_send_cmd(sockfd, 1, &info, sizeof(info));

                    recv(sockfd, &bag, sizeof(bag), 0);
                    recv(sockfd, buff, bag.len, 0);

                    if (strcmp(buff, "login success") == 0)
                    {
                        printf("登录成功\n");

                        // 登录后功能：查看个人信息、添加好友、创建群、加入群、私聊、群聊、其他功能
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
                            printf("7.其他功能\n");
                            printf("8.退出登录\n");
                            printf("-----------------------------------------------------\n");

                            printf("请输入你的选择：");
                            scanf("%d", &inner);
                            while (getchar() != '\n');

                            if (inner == 8)
                            {
                                break;
                            }

                            if (inner == 1) // 查看个人信息
                            {
                                func_send_cmd(sockfd, 15, &info, 0);
                                recv(sockfd, &bag, sizeof(bag), 0);
                                recv(sockfd, buff, bag.len, 0);
                                printf("%s\n", buff);
                            }
                            else if (inner == 2) // 添加好友
                            {
                                char friend_name[10] = {0};
                                printf("请输入要添加的好友账号：");
                                scanf("%9s", friend_name);
                                while (getchar() != '\n');

                                func_send_cmd(sockfd, 10, friend_name, sizeof(friend_name));
                                recv(sockfd, &bag, sizeof(bag), 0);
                                recv(sockfd, buff, bag.len, 0);
                                printf("%s\n", buff);
                            }
                            else if (inner == 3) // 创建群
                            {
                                char gname[10] = {0};
                                printf("请输入要创建的群账号：");
                                scanf("%9s", gname);
                                while (getchar() != '\n');

                                func_send_cmd(sockfd, 11, gname, sizeof(gname));
                                recv(sockfd, &bag, sizeof(bag), 0);
                                recv(sockfd, buff, bag.len, 0);
                                printf("%s\n", buff);
                            }
                            else if (inner == 4) // 加入群
                            {
                                char gname[10] = {0};
                                printf("请输入要加入的群账号：");
                                scanf("%9s", gname);
                                while (getchar() != '\n');

                                func_send_cmd(sockfd, 12, gname, sizeof(gname));
                                recv(sockfd, &bag, sizeof(bag), 0);
                                recv(sockfd, buff, bag.len, 0);
                                printf("%s\n", buff);
                            }
                            else if (inner == 5) // 私聊
                            {
                                char target[10] = {0};
                                char msg[256] = {0};
                                printf("请输入私聊对方账号：");
                                scanf("%9s", target);
                                while (getchar() != '\n');

                                printf("请输入私聊内容：");
                                scanf("%255s", msg);
                                while (getchar() != '\n');

                                func_send_cmd(sockfd, 13, target, sizeof(target));
                                func_send_cmd(sockfd, 13, msg, sizeof(msg));
                                recv(sockfd, &bag, sizeof(bag), 0);
                                recv(sockfd, buff, bag.len, 0);
                                printf("%s\n", buff);
                            }
                            else if (inner == 6) // 群聊
                            {
                                char gname[10] = {0};
                                char msg[256] = {0};
                                printf("请输入群账号：");
                                scanf("%9s", gname);
                                while (getchar() != '\n');

                                printf("请输入群聊内容：");
                                scanf("%255s", msg);
                                while (getchar() != '\n');

                                func_send_cmd(sockfd, 14, gname, sizeof(gname));
                                func_send_cmd(sockfd, 14, msg, sizeof(msg));
                                recv(sockfd, &bag, sizeof(bag), 0);
                                recv(sockfd, buff, bag.len, 0);
                                printf("%s\n", buff);
                            }
                            else if (inner == 7)
                            {
                                printf("其他功能接口已预留，后续自己扩展\n");
                            }
                        }

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

                // 注册命令号=2
                func_send_cmd(sockfd, 2, &info, sizeof(info));

                recv(sockfd, &bag, sizeof(bag), 0);
                recv(sockfd, buff, bag.len, 0);
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