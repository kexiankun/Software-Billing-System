#include <stdio.h>
#include <sqlite3.h>
#include <SDL2/SDL_net.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <locale.h>

// 声明 SQLite 数据库连接
sqlite3 *db;

// 发送消息给服务器的函数
void sendMessage(TCPsocket socket, const char *message) {
    int messageLength = strlen(message) + 1;  // 包括字符串结束符'\0'

    if (SDLNet_TCP_Send(socket, message, messageLength) < messageLength) {
        fprintf(stderr, "SDLNet_TCP_Send: %s\n", SDLNet_GetError());
    }
}

// 发送整数给服务器的函数
void sendInt(TCPsocket socket, int value) {
    if (SDLNet_TCP_Send(socket, &value, sizeof(value)) < sizeof(value)) {
        fprintf(stderr, "SDLNet_TCP_Send: %s\n", SDLNet_GetError());
    }
}

// 从服务器接收消息的函数
bool receiveMessage(TCPsocket socket, char *buffer, int bufferSize) {
    int bytesRead = SDLNet_TCP_Recv(socket, buffer, bufferSize);
    if (bytesRead <= 0) {
        fprintf(stderr, "SDLNet_TCP_Recv: %s\n", SDLNet_GetError());
        return false;
    }
    return true;
}

// 执行查询操作，获取用户ID
int getUserIdByUsername(const char *username) {
    char sql[100];
    sprintf(sql, "SELECT id FROM users WHERE username='%s';", username);

    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, 0);

    if (rc != SQLITE_OK) {
        fprintf(stderr, "无法准备查询语句：%s\n", sqlite3_errmsg(db));
        return -1;  // 返回负值表示查询失败
    }

    rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        int userId = sqlite3_column_int(stmt, 0);
        sqlite3_finalize(stmt);
        return userId;  // 返回查询到的用户ID
    } else if (rc == SQLITE_DONE) {
        fprintf(stderr, "未找到匹配的用户：%s\n", username);
        sqlite3_finalize(stmt);
        return -1;  // 返回负值表示查询失败
    } else {
        fprintf(stderr, "查询执行失败：%s\n", sqlite3_errmsg(db));
        sqlite3_finalize(stmt);
        return -1;  // 返回负值表示查询失败
    }
}

// 处理积分充值逻辑
void handlePointsRecharge(TCPsocket client, int userId, int points) {
    // 发送积分充值请求
    sendMessage(client, "RECHARGE_POINTS");

    // 提供用户ID和充值积分
    sendMessage(client, (char *)&userId);
    sendMessage(client, (char *)&points);

    // 从服务器接收一条消息
    char response[512];
    if (receiveMessage(client, response, sizeof(response))) {
        // 判断是否积分充值成功
        if (strstr(response, "RECHARGE_POINTS_SUCCESS")) {
            int userPoints;
            sscanf(response, "RECHARGE_POINTS_SUCCESS,%d", &userPoints);
            printf("积分充值成功！当前积分：%d\n", userPoints);
            // 在这里添加处理积分充值成功后的逻辑
        } else if (strcmp(response, "RECHARGE_POINTS_FAIL") == 0) {
            printf("积分充值失败。\n");
            // 在这里添加处理积分充值失败后的逻辑
        }

        // 其他响应的判断
        // ...
    } else {
        // 处理接收消息失败的情况
        printf("接收服务器响应失败。\n");
    }
}



int gouka() {

   // setlocale(LC_ALL, "");  // 设置本地化支持，以支持中文输入

    // 初始化SDL2_net
    SDLNet_Init();

    // 设置远程数据库服务器IP和端口
    IPaddress ip;
    SDLNet_ResolveHost(&ip, "127.0.0.1", 8989);  // 请替换成远程数据库服务器的IP和端口
while (1)
{
    // 打开客户端套接字
    TCPsocket client = SDLNet_TCP_Open(&ip);
    if (!client) {
        fprintf(stderr, "SDLNet_TCP_Open: %s\n", SDLNet_GetError());
        return 1;
    }

    // 初始化数据库连接
    int rc = sqlite3_open("user_data.db", &db);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "无法打开数据库：%s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        return 1;
    }

    // 通过用户名获取用户ID
char usernameToQuery[50];

printf("输入用户名: ");
scanf("%s", usernameToQuery);

int userId = getUserIdByUsername(usernameToQuery);

if (userId != -1) {
    printf("用户 '%s' 的唯一标识符是：%d\n", usernameToQuery, userId);

      // 例子：购买一张卡
        sendMessage(client, "BUY_CARD");

        // 提供用户ID、卡类型和时长
        sendInt(client, userId);
        
        char cardType[10] = "seasonal";  // 替换为实际的卡类型 day天卡 week周卡 
        sendMessage(client, cardType);
       
        int duration = 7776000;  // 替换为实际的时长 
        //一天包含 86,400 秒       daily
        //一周包含 604,800 秒      weekly
        //一月包含 2,592,000秒     monthly
        //一季包含 7,776,000秒     seasonal
        //一年包含 31,536,000秒    yearly

        sendMessage(client, (char *)&duration);
 //getchar();
// 从服务器接收购买卡的响应消息
char response[512];
if (receiveMessage(client, response, sizeof(response))) {
    // 判断购买卡的响应类型
    char *token = strtok(response, ",");
    if (token != NULL) {
        // 根据响应类型进行处理
        if (strcmp(token, "BUY_CARD_SUCCESS") == 0) {
            // 提取更新后的积分
            token = strtok(NULL, ",");
            if (token != NULL) {
                int updatedPoints = atoi(token);
                printf("购买卡成功！更新后的积分：%d\n", updatedPoints);
                
            } else {
                printf("购买卡成功，但未能提取更新后的积分。\n");
                
            }
        } else if (strcmp(token, "BUY_CARD_INSUFFICIENT_POINTS") == 0) {
            printf("购买卡失败。积分不足。\n");
         
        } else if (strcmp(token, "BUY_CARD_FAIL") == 0) {
            printf("购买卡失败。\n");
         
        }  else if (strcmp(token, "CARD_ALREADY_OWNED") == 0) {
            printf("请勿重复购买\n");
            
        } else {
            printf("未知的购买卡响应类型：%s\n", token);
          
        }
    } else {
        printf("无法解析购买卡的响应消息。\n");
     
    }
} else {
    // 处理接收消息失败的情况
    printf("接收购买卡响应消息失败。\n");
  
}

    } else {
        fprintf(stderr, "获取用户ID失败。\n");
        
    }
    // 关闭数据库连接
    sqlite3_close(db);
    // 关闭客户端套接字
    SDLNet_TCP_Close(client);
}

    // 关闭SDL2_net
    SDLNet_Quit();

    return 0;
}


int jifen() {

   // setlocale(LC_ALL, "");  // 设置本地化支持，以支持中文输入

    // 初始化SDL2_net
    SDLNet_Init();

    // 设置远程数据库服务器IP和端口
    IPaddress ip;
    SDLNet_ResolveHost(&ip, "127.0.0.1", 8989);  // 请替换成远程数据库服务器的IP和端口
while (1)
{
    


    // 打开客户端套接字
    TCPsocket client = SDLNet_TCP_Open(&ip);
    if (!client) {
        fprintf(stderr, "SDLNet_TCP_Open: %s\n", SDLNet_GetError());
        return 1;
    }

    // 初始化数据库连接
    int rc = sqlite3_open("user_data.db", &db);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "无法打开数据库：%s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        return 1;
    }

    // 通过用户名获取用户ID
char usernameToQuery[50];

printf("输入用户名: ");
scanf("%s", usernameToQuery);

int userId = getUserIdByUsername(usernameToQuery);

if (userId != -1) {
    printf("用户 '%s' 的唯一标识符是：%d\n", usernameToQuery, userId);

/////////////////////////////////////////////////////////////////////////

int rechargePoints; // 直接定义为整数变量
// 例子：积分充值
printf("输入积分: ");
scanf("%d", &rechargePoints); // 使用 &rechargePoints 获取整数变量的地址

// 发送积分充值请求
sendMessage(client, "RECHARGE_POINTS");

// 提供用户ID和充值积分
sendInt(client, userId);
SDLNet_TCP_Send(client,&rechargePoints,sizeof(rechargePoints));


// 从服务器接收一条消息
char response[512];
if (receiveMessage(client, response, sizeof(response))) {
    // 判断是否积分充值成功
    if (strstr(response, "RECHARGE_POINTS_SUCCESS")) {
        int userPoints;
        sscanf(response, "RECHARGE_POINTS_SUCCESS,%d", &userPoints);
        printf("积分充值成功！当前积分：%d\n", userPoints);
    } else if (strcmp(response, "RECHARGE_POINTS_FAIL") == 0) {
        printf("积分充值失败。\n");
        // 在这里添加处理积分充值失败后的逻辑
    }

    // 其他响应的判断
    // ...
} else {
    // 处理接收消息失败的情况
    printf("接收服务器响应失败。\n");

}
    } else {
        fprintf(stderr, "获取用户ID失败。\n");
        
    }

    // 关闭数据库连接
    sqlite3_close(db);
    // 关闭客户端套接字
    SDLNet_TCP_Close(client);
}


    // 关闭SDL2_net
    SDLNet_Quit();

    return 0;
}


int main(int argc, char *argv[]) {

   jifen();

   // gouka();

    return  0;
}