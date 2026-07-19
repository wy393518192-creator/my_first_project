#ifndef USER_H
#define USER_H

#include "stype.h"

void func_user(void);
void save_user_data(void);
void node_process(SOC* head, SOC* new_node);
void free_user_list(void);
void free_client_list(void);

USE* find_user(const char* account);
GROUP* find_group(const char* gname);
int register_user(const char* account, const char* password, const char* name);
int login_user(SOC* ptr, const char* account, const char* password);
int add_friend_to_user(const char* account, const char* friend_name);
int create_group_for_user(const char* account, const char* gname);
int join_group_for_user(const char* account, const char* gname);
char* build_profile_string(const char* account);

void send_text(SOC* ptr, const char* msg);
int execute_server_command(SOC* ptr, const char* cmd);
void* func_client(void* arg);
void monitor_func(void* arg);
IP_CF func_ipconfig(void);

#endif