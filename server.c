#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <sqlite3.h>
#include <time.h>
#include <SDL2/SDL_net.h>
#include <SDL2/SDL_thread.h>



#define MAX_CLIENTS 1000   // 最大客户端数量
bool userLoggedIn = false; // 标志用户 是否已登录
int loggedInUserId;        // 保存用户ID
char loggedInUsername[50]; // 保存用户名
int userPoints = 0;        // 获取用户点数 积分
const char *cardType = 0;  // 卡号类型

// 创建 Socket Set
SDLNet_SocketSet socketSet;
// TCP服务器
TCPsocket serverserver;
// 客户端连接
TCPsocket clientSocket;



// 用户结构体
typedef struct{ 
    int id;               // 用户唯一标识符
    char username[50];    // 用户名，最大长度为50个字符
    char password[50];    // 用户密码，最大长度为50个字符
    int points;           // 用户积分或
} User;


// 卡结构体
typedef struct {
    int id;                 // 卡的唯一标识符
    int user_id;            // 用户ID，关联卡与用户
    char type[10];          // 卡的类型，例如 "daily", "weekly", "monthly", etc.
    int duration;           // 使用时长，以秒为单位
    time_t purchase_time;   // 购买时间，记录卡的购买时刻
    time_t expiration_time; // 到期时间
} Card;

// SQLite数据库连接
sqlite3 *db;

// 定义表示连接客户端的结构体
typedef struct {
    TCPsocket socket;
    int userId;
    char username[50];
    int userLoggedIn;
    time_t lastHeartbeat;
} ConnectedClient;


// 存储连接客户端的数组
ConnectedClient clients[MAX_CLIENTS];

bool loginUser(const char *username, const char *password, User *user);

char* generateSignedToken(const char* RANDOM_TOKEN);//令牌
char* generateRandomToken();


// 变量表示当前在线的客户端数量
int onlineClientsCount = 0;
// 移除用户
void removeUser(const char *username) {
    for (int i = 0; i < onlineClientsCount; ++i) {
        if (strcmp(clients[i].username, username) == 0) {
            // 将找到的用户从列表中移除
            for (int j = i; j < onlineClientsCount - 1; ++j) {
                clients[j] = clients[j + 1];
            }
            onlineClientsCount--;
            break;
        }
    }
}

// 检查用户是否在线
int isUserOnline(const char *username) {
    for (int i = 0; i < onlineClientsCount; ++i) {
        if (strcmp(clients[i].username, username) == 0) {
            // 用户已经在线
            return 1;
        }
    }
    // 用户不在线
    return 0;
}

// 定义添加在线客户端的函数
void addOnlineClient(TCPsocket clientSocket, int userId, const char *username) {
    if (onlineClientsCount < MAX_CLIENTS) {
        // 还有空间添加新的在线客户端

        // 创建一个新的 ConnectedClient 结构体表示连接的客户端
        ConnectedClient newClient;
        newClient.socket = clientSocket;
        newClient.userId = userId;
        strncpy(newClient.username, username, sizeof(newClient.username) - 1);
        newClient.userLoggedIn = 1;  // 用户已登录
        time(&newClient.lastHeartbeat);  // 设置最后心跳时间

        // 将新客户端添加到在线客户端数组中
        clients[onlineClientsCount] = newClient;
        onlineClientsCount++;

        printf("用户 %s 已成功登录。\n", username);

    } else {
        // 在线客户端数组已满，无法添加新客户端
        printf("在线客户端数量已达到上限，无法添加新客户端。\n");

        // 发送相应的消息给客户端，通知其无法登录
        SDLNet_TCP_Send(clientSocket, "LOGIN_FAIL_SERVER_FULL",sizeof("LOGIN_FAIL_SERVER_FULL"));
    
    }
}

// 处理心跳消息的函数
void handleHeartbeat(int userId) {
    for (int i = 0; i < MAX_CLIENTS; ++i) {
        if (clients[i].socket && clients[i].userId == userId) {
            printf("处理心跳消息 - 用户 %s (ID: %d)\n", clients[i].username, clients[i].userId);
            // 更新最后一次心跳时间
            time(&clients[i].lastHeartbeat);
            break;
        }
    }
}

// 检查并断开不活跃客户端的函数
void checkInactiveClients(int userId) {
    time_t currentTime;
    time(&currentTime);

    for (int i = 0; i < MAX_CLIENTS; ++i) {
    if (clients[i].socket && clients[i].userId == userId) {
            // 检查客户端是否在最近30秒内发送过心跳消息
            if (currentTime - clients[i].lastHeartbeat > 30) {
                printf("客户端 %s (ID: %d) 因长时间不活跃而断开连接。\n", clients[i].username, clients[i].userId);

                removeUser(clients[i].username);//清楚不在用户

                SDLNet_TCP_Close(clients[i].socket);
                clients[i].socket = NULL;
            }
        }
    }
}

// 添加新客户端到数组的函数
void addClient(TCPsocket socket, int userId, const char *username, int userLoggedIn, time_t lastHeartbeat) {
    for (int i = 0; i < MAX_CLIENTS; ++i) {
        if (!clients[i].socket) {
            clients[i].socket = socket;
            clients[i].userId = userId;
            strcpy(clients[i].username, username);
               
            clients[i].userLoggedIn = userLoggedIn;
            clients[i].lastHeartbeat= lastHeartbeat;
            time(&clients[i].lastHeartbeat);
            struct tm *purchase_tm = localtime(&clients[i].lastHeartbeat);

            printf("客户端-> (用户名: %s) (积分: %d) (卡号类型: %s) (ID: %d) (在线: %d) (登入时间: %d-%d-%d-%d-%d-%d)连接成功。\n", clients[i].username, userPoints,
            cardType, clients[i].userId, clients[i].userLoggedIn, purchase_tm->tm_year + 1900, purchase_tm->tm_mon + 1, purchase_tm->tm_mday,
                  purchase_tm->tm_hour, purchase_tm->tm_min, purchase_tm->tm_sec);

            //SDLNet_TCP_Send(socket, &clients[i].userId, sizeof(clients[i].userId));//用户ID 发送给客户端
            break;
        }
    }
}

// 处理客户端连接和消息的函数
void handleClient(void* data ) {
    char buffer[512];

   ConnectedClient* connectedClient = (ConnectedClient*)data;
    TCPsocket clientSocket = connectedClient->socket;   //客服端
    int userId = connectedClient->userId;               //用户ID
    const char* username = connectedClient->username;   //用户名
    int userLoggedIn= connectedClient->userLoggedIn;    //用户标识 1在线 0不在线

    time_t lastHeartbeat=connectedClient->lastHeartbeat;//用户在线时间

    addClient(clientSocket, userId, username, userLoggedIn, lastHeartbeat);

    while (SDLNet_TCP_Recv(clientSocket, buffer, sizeof(buffer)) > 0) {
        // 处理接收到的消息
        if (strcmp(buffer, "HEARTBEAT") == 0) {
            // 处理心跳消息
            handleHeartbeat(userId);
        } else {
            // 根据需要处理其他类型的消息
            // ...
        }

        // 检查不活跃的客户端并断开连接
        checkInactiveClients(userId);
    }

    // 客户端断开连接
    for (int i = 0; i < MAX_CLIENTS; ++i) {
        if (clients[i].socket == clientSocket) {
        //    printf("客户端 %s (ID: %d) 断开连接。\n", clients[i].username, clients[i].userId);
            clients[i].userId = userId;
            strcpy(clients[i].username, username);
               
            clients[i].userLoggedIn = 0;
            clients[i].lastHeartbeat= lastHeartbeat;
            time(&clients[i].lastHeartbeat);  // 用当前时间更新字段

            struct tm *purchase_tm = localtime(&clients[i].lastHeartbeat);

            printf("客户端-> (用户名: %s) (积分: %d) (卡号类型: %s) (ID: %d) (在线: %d) (登出时间: %d-%d-%d-%d-%d-%d)断开连接。\n", clients[i].username, userPoints,
            cardType, clients[i].userId, clients[i].userLoggedIn, purchase_tm->tm_year + 1900, purchase_tm->tm_mon + 1, purchase_tm->tm_mday,
            purchase_tm->tm_hour, purchase_tm->tm_min, purchase_tm->tm_sec);


            removeUser(clients[i].username);//清楚不在用户

            SDLNet_TCP_Close(clientSocket);
            clients[i].socket = NULL;
            break;
        }
    }
}

// 初始化数据库连接
void initDatabase() {
    int rc = sqlite3_open("user_data.db", &db);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "无法打开数据库：%s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        exit(1);
    }
}
// 创建用户表和卡表
void createTables() {
    const char *createUserTableSQL = "CREATE TABLE IF NOT EXISTS users ("
                                     "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                                     "username TEXT NOT NULL,"
                                     "password TEXT NOT NULL,"
                                     "points INTEGER NOT NULL);";

    const char *createCardTableSQL ="CREATE TABLE IF NOT EXISTS cards ("
                                    "id INTEGER PRIMARY KEY,"
                                    "user_id INTEGER NOT NULL,"
                                    "type TEXT NOT NULL,"
                                    "duration INTEGER NOT NULL,"
                                    "purchase_time INTEGER NOT NULL,"
                                    "expiration_time INTEGER NOT NULL,"
                                    "FOREIGN KEY (user_id) REFERENCES users(id)"
                                    ");";


    int rc = sqlite3_exec(db, createUserTableSQL, 0, 0, 0);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Cannot create users table: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        exit(1);
    }

    rc = sqlite3_exec(db, createCardTableSQL, 0, 0, 0);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Cannot create cards table: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        exit(1);
    }
}

// 查询操作，获取用户ID
int getUserIdByUsername(const char *username) {
    char sql[100];
    sprintf(sql, "SELECT id FROM users WHERE username='%s';", username);

    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, 0);

    if (rc != SQLITE_OK) {
        fprintf(stderr, "Failed to prepare statement: %s\n", sqlite3_errmsg(db));
        return -1;  // 返回负值表示查询失败
    }

    rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        int userId = sqlite3_column_int(stmt, 0);
        sqlite3_finalize(stmt);
        return userId;  // 返回查询到的用户ID
    } else {
        fprintf(stderr, "Failed to execute query: %s\n", sqlite3_errmsg(db));
        sqlite3_finalize(stmt);
        return -1;  // 返回负值表示查询失败
    }
}

// 获取用户积分
int getUserPoints(int userId) {
    char sql[100];
    sprintf(sql, "SELECT points FROM users WHERE id = %d;", userId);

    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, 0);

    if (rc != SQLITE_OK) {
        fprintf(stderr, "Failed to prepare statement: %s\n", sqlite3_errmsg(db));
        return -1;  // 表示获取积分失败
    }

    rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        int userPoints = sqlite3_column_int(stmt, 0);
        sqlite3_finalize(stmt);  // 释放资源
        return userPoints;
    } else {
        fprintf(stderr, "Failed to execute query: %s\n", sqlite3_errmsg(db));
        sqlite3_finalize(stmt);  // 释放资源
        return -1;  // 表示获取积分失败
    }
}

// 扣除积分
void deductPoints(int user_id, int points_to_deduct) {
    // 获取当前用户的积分
    int current_points = getUserPoints(user_id);

    // 检查积分是否足够扣除
    if (current_points >= points_to_deduct) {
        char sql[100];
        sprintf(sql, "UPDATE users SET points = points - %d WHERE id = %d;", points_to_deduct, user_id);

        int rc = sqlite3_exec(db, sql, 0, 0, 0);
        if (rc != SQLITE_OK) {
            fprintf(stderr, "Failed to deduct points: %s\n", sqlite3_errmsg(db));
        } else {
            printf("积分扣除成功。\n");
        }
    } else {
            printf("扣除积分不足。\n");
    }
}

// 更新用户积分
int updateUserPoints(int userId, int newPoints) {
    char sql[100];
    sprintf(sql, "UPDATE users SET points = %d WHERE id = %d;", newPoints, userId);

    int rc = sqlite3_exec(db, sql, 0, 0, 0);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Failed to update user points: %s\n", sqlite3_errmsg(db));
        return 0;  // 表示更新积分失败
    }

    return 1;  // 表示更新积分成功
}

// 处理积分充值逻辑
void handlePointsRecharge(TCPsocket server, int userId, int points) {
    // 用于构建SQL查询语句的字符串
    char sql[200];

    // 构建更新语句，将指定用户的积分增加给定的点数
    sprintf(sql, "UPDATE users SET points = points + %d WHERE id = %d;", points, userId);

    // 执行SQL语句，更新用户积分
    int rc = sqlite3_exec(db, sql, 0, 0, 0);

    // 检查SQL执行结果
    if (rc != SQLITE_OK) {
        // 如果执行失败，向客户端发送失败消息
        fprintf(stderr, "Failed to recharge points: %s\n", sqlite3_errmsg(db));
        SDLNet_TCP_Send(server, "RECHARGE_POINTS_FAIL", sizeof("RECHARGE_POINTS_FAIL"));
        return;
    }

    // 查询最新的积分
    sprintf(sql, "SELECT points FROM users WHERE id = %d;", userId);
    sqlite3_stmt *stmt;

    // 准备SQL查询语句
    rc = sqlite3_prepare_v2(db, sql, -1, &stmt, 0);

    // 检查SQL准备结果
    if (rc != SQLITE_OK) {
        // 如果准备失败，向客户端发送失败消息
        fprintf(stderr, "Failed to prepare statement: %s\n", sqlite3_errmsg(db));
        SDLNet_TCP_Send(server, "RECHARGE_POINTS_FAIL", sizeof("RECHARGE_POINTS_FAIL"));
        return;
    }

    // 执行SQL查询
    rc = sqlite3_step(stmt);

    // 检查SQL执行结果
    if (rc == SQLITE_ROW) {
        // 如果查询到结果，获取用户最新的积分
        int userPoints = sqlite3_column_int(stmt, 0);

        // 构建成功响应消息，包含最新的积分信息
        char response[512];
        sprintf(response, "RECHARGE_POINTS_SUCCESS,%d", userPoints);

        // 向客户端发送成功响应消息
        SDLNet_TCP_Send(server, response, sizeof(response));
    } else {
        // 如果执行查询失败，向客户端发送失败消息
        fprintf(stderr, "Failed to execute query: %s\n", sqlite3_errmsg(db));
        SDLNet_TCP_Send(server, "RECHARGE_POINTS_FAIL", sizeof("RECHARGE_POINTS_FAIL"));
    }

    // 释放SQL语句资源
    sqlite3_finalize(stmt);
}

// 插入卡信息 
int insertCardInfo(int userId, const char *cardType, int duration, time_t purchaseTime) {
    time_t expirationTime = purchaseTime + duration;  // 计算到期时间

    char sql[200];
    sprintf(sql, "INSERT INTO cards (user_id, type, duration, purchase_time, expiration_time) VALUES (%d, '%s', %d, %ld, %ld);",
            userId, cardType, duration, purchaseTime, expirationTime);

    int rc = sqlite3_exec(db, sql, 0, 0, 0);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Failed to insert card information: %s\n", sqlite3_errmsg(db));
        return 0;  // 表示插入卡信息失败
    }

    return 1;  // 表示插入卡信息成功
}

// 获取用户拥有的卡类型
const char * hasAnyCard(int userId) {
    char sql[200];
    
    sprintf(sql, "SELECT DISTINCT type FROM cards WHERE user_id = %d;", userId);

    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, 0);

    if (rc != SQLITE_OK) {
        fprintf(stderr, "Failed to execute SQL statement: %s\n", sqlite3_errmsg(db));
        return cardType;
    }

   // printf("User %d has the following card types", userId);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        cardType = (const char *)sqlite3_column_text(stmt, 0);
       // printf("->: %s\n", cardType);
        return cardType;
    }

    // 释放预处理语句资源
    sqlite3_finalize(stmt);

    return cardType;
}

// 购卡逻辑函数
void handleCardPurchase(TCPsocket server, int userId, const char *cardType) {
    // 检查用户是否已经拥有任何一种卡
    if (hasAnyCard(userId)) {
        // 发送相应的消息给客户端，表示用户已经有卡了
        SDLNet_TCP_Send(server, "CARD_ALREADY_OWNED", sizeof("CARD_ALREADY_OWNED"));
        return;
    }


    char sql[200];
    time_t currentTime;
    time(&currentTime);

    // 设置积分
    int cardCost;
    int duration;
    if (strcmp(cardType, "daily") == 0) {
        duration = 86400; //时间
        cardCost = 10;    //积分
    } else if (strcmp(cardType, "weekly") == 0) {
        duration = 604800;
        cardCost = 70;
    } else if (strcmp(cardType, "monthly") == 0) {
        duration = 2592000;
        cardCost = 300;
    } else if (strcmp(cardType, "seasonal") == 0) {
        duration = 7776000;
        cardCost = 900;
    } else if (strcmp(cardType, "yearly") == 0) {
        duration = 31536000;
        cardCost = 3600;
    } 

    // 获取用户积分
    int userPoints = getUserPoints(userId);

    // 如果积分足够购买卡
    if (userPoints >= cardCost) {
        // 扣除用户积分
        if (updateUserPoints(userId, userPoints - cardCost)) {
            // 插入卡信息
            if (insertCardInfo(userId, cardType, duration, currentTime)) {
                printf("Card purchased successfully.\n");

                // 发送用户积分给客户端
                char response[512];
                sprintf(response, "BUY_CARD_SUCCESS,%d", userPoints - cardCost);
                SDLNet_TCP_Send(server, response, sizeof(response));
                return;  // 函数执行成功，退出
            } else {
                fprintf(stderr, "Card purchase failed: Unable to insert card information.\n");
            }
        } else {
            fprintf(stderr, "Failed to update user points: %s\n", sqlite3_errmsg(db));
        }
    } else {
        // 积分不足
        SDLNet_TCP_Send(server, "BUY_CARD_INSUFFICIENT_POINTS", sizeof("BUY_CARD_INSUFFICIENT_POINTS"));
    }

    // 发送购买卡失败消息给客户端
    SDLNet_TCP_Send(server, "BUY_CARD_FAIL", sizeof("BUY_CARD_FAIL"));

    return;  // 函数执行成功，退出
}

// 处理登录逻辑
void handleCardPurchaseLogin(TCPsocket server, const char *username, const char *password, const char *cardType) {

    User currentUser;
    
    // 进行用户登录验证
    if (loginUser(username, password, &currentUser)) {
       // printf("Login successful. Welcome, %s!\n", currentUser.username);

        // 设置用户已登录标志
        userLoggedIn = true;
        // 保存用户信息 获取用户标识ID
        loggedInUserId = currentUser.id;
        strcpy(loggedInUsername, currentUser.username);
       //printf("User loggedInUsername: %s\n", loggedInUsername);
        // 获取用户点数 积分
         userPoints = getUserPoints(loggedInUserId);
       
       // printf("User points: %d\n", userPoints);
     
         handleCardPurchase(server,loggedInUserId, cardType);
    } else {
        // 登录失败
        SDLNet_TCP_Send(server, "LOGIN_FAIL", sizeof("LOGIN_FAIL"));
    }

}

// 获取卡剩余时长
int getCardRemainingDuration(int user_id, const char *card_type) {
    // 使用参数化查询构建 SQL 语句
    const char *sql = "SELECT expiration_time FROM cards WHERE user_id=? AND type=? AND expiration_time > ?;";
    // 获取当前时间
    time_t current_time;
    time(&current_time);

    // 编译 SQL 语句
    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, 0);

    if (rc != SQLITE_OK) {
        fprintf(stderr, "Failed to fetch card details: %s\n", sqlite3_errmsg(db));
        return 0; // 查询失败，假设剩余时长为0
    }

    // 绑定参数
    sqlite3_bind_int(stmt, 1, user_id);
    sqlite3_bind_text(stmt, 2, card_type, -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 3, current_time);

    // 执行 SQL 语句
    rc = sqlite3_step(stmt);

    if (rc == SQLITE_ROW) {
        // 获取到期时间
        time_t expiration_time = sqlite3_column_int(stmt, 0);

    struct tm *purchase_tm = localtime(&expiration_time);

    // 打印购买时刻的日期和时间
    printf("账号到期时间：%d-%d-%d-%d-%d-%d  ",
           purchase_tm->tm_year + 1900, purchase_tm->tm_mon + 1, purchase_tm->tm_mday,
           purchase_tm->tm_hour, purchase_tm->tm_min, purchase_tm->tm_sec);

        // 计算剩余时长
        int remaining_duration = expiration_time - current_time;

        // 打印购买时刻的日期和时间
        printf("剩余时间：%d 秒\n",remaining_duration);

        // 释放预处理语句资源
        sqlite3_finalize(stmt);

        return remaining_duration > 0 ? remaining_duration : 0;
    } else {
        // 未找到有效的卡，返回剩余时长为0
        sqlite3_finalize(stmt);
        return 0;
    }
}
// 使用时长
void simulateUsage(int user_id, const char *card_type, int usage_time) {
    char sql[200];
    time_t current_time;
    time(&current_time);

    sprintf(sql, "SELECT * FROM cards WHERE user_id=%d AND type='%s';", user_id, card_type);

    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, 0);

    if (rc != SQLITE_OK) {
        fprintf(stderr, "Failed to fetch card details: %s\n", sqlite3_errmsg(db));
        return;
    }

    rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        int card_id = sqlite3_column_int(stmt, 0);
        int remaining_duration = sqlite3_column_int(stmt, 3) - usage_time;

        if (remaining_duration > 0) {
            sprintf(sql, "UPDATE cards SET duration = %d WHERE id = %d;", remaining_duration, card_id);
            rc = sqlite3_exec(db, sql, 0, 0, 0);
            if (rc != SQLITE_OK) {
                fprintf(stderr, "Failed to update card duration: %s\n", sqlite3_errmsg(db));
            } else {
                printf("购买卡时间：%d 秒\n", usage_time);
            }
        } else {
           // printf("Card usage time expired. Deducting points...\n");
            deductPoints(user_id, 10); // 假设每次使用超过卡的时长，扣除10点
        }
    } else {
        printf("未找到用户 %d 卡类型 %s 卡使用时长 %d\n", user_id, card_type);
    }
}
// 检查用户是否有有效的卡
int hasValidCard(int user_id, const char *card_type) {
    char sql[200];
    time_t current_time;
    time(&current_time);

    sprintf(sql, "SELECT * FROM cards WHERE user_id=%d AND type='%s' AND expiration_time > %ld;", user_id, card_type, current_time);

    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, 0);

    if (rc != SQLITE_OK) {
        fprintf(stderr, "Failed to fetch card details: %s\n", sqlite3_errmsg(db));
        return 0; // 查询失败，假设无效卡
    }

    rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        // 存在有效的卡
        sqlite3_finalize(stmt);
        return 1;
    } else {
        // 不存在有效的卡
        sqlite3_finalize(stmt);
        return 0;
    }
}

// 处理使用时长逻辑
void handleCardUsage(TCPsocket server) {
    char buffer[512];
    if (SDLNet_TCP_Recv(server, buffer, sizeof(buffer)) > 0) {
        // 解析使用信息
        char *token = strtok(buffer, "|");
        int user_id = atoi(token);

        token = strtok(NULL, "|");
        const char *card_type = token;

        token = strtok(NULL, "|");
        int usage_time = atoi(token);

        // 模拟使用时长
        simulateUsage(user_id, card_type, usage_time);

        // 发送使用结果给客户端（可以根据具体情况返回不同的消息）
        SDLNet_TCP_Send(server, "LOGIN_SUCCESS", sizeof("LOGIN_SUCCESS"));
    }
}

// 卡通用函数
int useCard(TCPsocket server, int user_id, const char *cardType) {

    // 假设用户有有效的卡
    if (hasValidCard(user_id, cardType)) {
        // 获取卡的剩余时长

            if (isUserOnline(loggedInUsername)) {
        // 用户已经在线，处理相应逻辑
        printf("用户 %s 已经在线，不允许重复登录。\n", loggedInUsername);

        SDLNet_TCP_Send(server, "LOGIN_ALREADY", sizeof("LOGIN_ALREADY"));
    } else {

        
        // 添加用户到在线客户端列表
        addOnlineClient(clientSocket, onlineClientsCount, loggedInUsername);
        int remaining_duration = getCardRemainingDuration(user_id, cardType);

        // 模拟使用时长
        simulateUsage(user_id, cardType, remaining_duration);

        // 在这里添加其他处理逻辑，比如发送使用成功的消息给客户端
      //  SDLNet_TCP_Send(server, "LOGIN_SUCCESS", sizeof("LOGIN_SUCCESS"));

        // 假设你有一个随机生成的令牌
        const char* randomToken = "LOGIN_SUCCESS";//generateRandomToken();

        // 生成带有数字签名的令牌
        char* signedToken = generateSignedToken(randomToken);
        printf("服务器发送了带有数字签名的令牌: %s\n", signedToken);

        // 发送令牌到客户端
        SDLNet_TCP_Send(server, signedToken, strlen(signedToken));
        // 发送令牌到客户端
        SDLNet_TCP_Send(server, randomToken, strlen(randomToken));

        time_t currentTime;
        time(&currentTime);
        //time_t currentTime = time(NULL);//获取当前登入时间
        ConnectedClient* connectedClient = (ConnectedClient*)malloc(sizeof(ConnectedClient));
        connectedClient->socket = clientSocket;                 //客户端
        connectedClient->userId = loggedInUserId;               //用户标识
        strcpy(connectedClient->username, loggedInUsername);    //用户名
        connectedClient->userLoggedIn =userLoggedIn;            //登入状态 1在线 0不在线
        connectedClient->lastHeartbeat= currentTime;            //用户登入时间



        SDL_Thread* clientThread = SDL_CreateThread((SDL_ThreadFunction)handleClient, "ClientThread", (void*)connectedClient);
    }
    } else {
        // 用户没有有效的卡，可以发送相应的消息给客户端
        SDLNet_TCP_Send(server, "CARD_USAGE_FAIL", sizeof("CARD_USAGE_FAIL"));
    }
    return 0;
}

// 检查用户名是否已存在
bool isUsernameTaken(const char *username) {
    char sql[100];
    sprintf(sql, "SELECT COUNT(*) FROM users WHERE username='%s';", username);

    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, 0);

    if (rc != SQLITE_OK) {
        fprintf(stderr, "Failed to prepare statement: %s\n", sqlite3_errmsg(db));
        return true;  // 当发生错误时，假设用户名已被占用
    }

    rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        int count = sqlite3_column_int(stmt, 0);
        return count > 0;  // 如果 count 大于 0，说明用户名已被占用
    } else {
        fprintf(stderr, "Failed to execute query: %s\n", sqlite3_errmsg(db));
        return true;  // 当发生错误时，假设用户名已被占用
    }
}


// 检查用户名是否包含大小写字母
int isUsernameValid(const char *username) {
    int hasLower = 0;
    int hasUpper = 0;

    while (*username) {
        if (islower(*username)) {
            hasLower = 1;
        } else if (isupper(*username)) {
            hasUpper = 1;
        }

        ++username;
    }

    return hasLower && hasUpper;
}

// 处理用户注册逻辑
void handleRegistration(TCPsocket server, const char *username, const char *password) {
    // 检查用户名是否已被占用
    if (isUsernameTaken(username)) {
        // 向客户端发送注册失败消息（用户名已被占用）
        SDLNet_TCP_Send(server, "REGISTER_FAILZY", sizeof("REGISTER_FAILZY"));
    } else if (!isUsernameValid(password) || strlen(username) < 6) {
        // 检查用户名和密码是否符合要求
        // 向客户端发送注册失败消息
        SDLNet_TCP_Send(server, "REGISTER_FAILYHM", sizeof("REGISTER_FAILYHM"));
    } else {
        // 构建插入用户数据的SQL语句
        char sql[100];
        sprintf(sql, "INSERT INTO users (username, password, points) VALUES ('%s', '%s', 0);", username, password);

        // 执行SQL语句插入新用户数据
        int rc = sqlite3_exec(db, sql, 0, 0, 0);

        // 检查SQL执行结果
        if (rc != SQLITE_OK) {
            // 如果执行失败，向客户端发送注册失败消息
            fprintf(stderr, "Registration failed: %s\n", sqlite3_errmsg(db));
            SDLNet_TCP_Send(server, "REGISTER_FAIL", sizeof("REGISTER_FAIL"));
        } else {
            // 注册成功，向客户端发送注册成功消息
            printf("Registration successful.\n");
            SDLNet_TCP_Send(server, "REGISTER_SUCCESS", sizeof("REGISTER_SUCCESS"));
        }
    }
}


// 用户登录验证
bool loginUser(const char *username, const char *password, User *user) {
    char sql[100];
    sprintf(sql, "SELECT * FROM users WHERE username='%s' AND password='%s';", username, password);

    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, 0);

    if (rc != SQLITE_OK) {
        fprintf(stderr, "Login failed: %s\n", sqlite3_errmsg(db));
        return false;
    }

    rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        user->id = sqlite3_column_int(stmt, 0);
        strcpy(user->username, sqlite3_column_text(stmt, 1));
        strcpy(user->password, sqlite3_column_text(stmt, 2));
        user->points = sqlite3_column_int(stmt, 3);

        // 输出登录成功信息到服务器端
       // printf("Login successful. Welcome, %s!\n", user->username);
        
        return true;
    } else {
        // 输出登录失败信息到服务器端
       // printf("Login failed. Invalid username or password.\n");
        return false;
    }
}
// 处理登录逻辑
void handleLogin(TCPsocket server, const char *username, const char *password) {

    User currentUser;
    
    // 进行用户登录验证
    if (loginUser(username, password, &currentUser)) {
       // printf("Login successful. Welcome, %s!\n", currentUser.username);

        // 设置用户已登录标志
        userLoggedIn = true;

        // 保存用户信息 获取用户标识ID
        loggedInUserId = currentUser.id;

        strcpy(loggedInUsername, currentUser.username);

       //printf("User loggedInUsername: %s\n", loggedInUsername);


        // 获取用户点数 积分
         userPoints = getUserPoints(loggedInUserId);
        // 点数不为0，登录成功
       // printf("User points: %d\n", userPoints);
 if(userPoints >= 0){

    const char *cardType = hasAnyCard(loggedInUserId);
   // printf("User cardType: %s\n", cardType);
if (cardType != NULL) {
    if (strcmp(cardType, "daily") == 0) {
        // 使用一天卡
        useCard(server, loggedInUserId, "daily");
    } else if (strcmp(cardType, "weekly") == 0) {
        // 使用周卡
        useCard(server, loggedInUserId, "weekly");
    } else if (strcmp(cardType, "monthly") == 0) {
        // 使用月卡
        useCard(server, loggedInUserId, "monthly");
    } else if (strcmp(cardType, "seasonal") == 0) {
        // 使用季卡
        useCard(server, loggedInUserId, "seasonal");
    } else if (strcmp(cardType, "yearly") == 0) {
        // 使用年卡
        useCard(server, loggedInUserId, "yearly");} 
    
     }

    
    }else{ // 积分为0 
    
    SDLNet_TCP_Send(server, "LOGIN_FAIL_POINTS_ZERO", sizeof("LOGIN_FAIL_POINTS_ZERO"));}
       
    }else {// 登录失败
    SDLNet_TCP_Send(server, "LOGIN_FAIL", sizeof("LOGIN_FAIL"));}
}

// 服务器监听并处理客户端连接
void serverListen() {

    printf("服务器监听在端口 %d...\n", 8989);  // 开放端口
    
    while (1) {
 
    // 初始化 Socket Set
    socketSet = SDLNet_AllocSocketSet(MAX_CLIENTS);

    // 存储客户端的 TCP sockets
    TCPsocket clientSockets[MAX_CLIENTS];

    int numClients = 0;

    // 接受客户端连接
              clientSocket = SDLNet_TCP_Accept(serverserver);

        if (clientSocket) {
            //printf("clientSocket connected.\n");


            char buffer[512];
            if (SDLNet_TCP_Recv(clientSocket, buffer, sizeof(buffer)) > 0) {
                // 在这里处理客户端发送的消息，根据需要调用你的其他函数
                // 例子：根据消息内容执行相应操作
                if (strcmp(buffer, "REGISTER") == 0) {
                    // 处理注册逻辑
                    char username[50];
                    char password[50];
                    SDLNet_TCP_Recv(clientSocket, username, sizeof(username));
                    SDLNet_TCP_Recv(clientSocket, password, sizeof(password));
                    handleRegistration(clientSocket, username, password);
                
                } else if (strcmp(buffer, "LOGIN") == 0) {
                    // 处理登录逻辑
                    char username[50];
                    char password[50];
                    SDLNet_TCP_Recv(clientSocket, username, sizeof(username));
                    SDLNet_TCP_Recv(clientSocket, password, sizeof(password));
                    handleLogin(clientSocket, username, password);

                } else if (strcmp(buffer, "BUY_CARD") == 0) {
                   
                    // 用户ID
                    int userId;
                    // 卡类型，最大长度为10
                    char cardType[256];
                    // 卡的使用时长
                    int duration;

                    // 处理购买卡逻辑
                    char username[50];
                    char password[50];
                    SDLNet_TCP_Recv(clientSocket, username, sizeof(username));
                    SDLNet_TCP_Recv(clientSocket, password, sizeof(password));
                    SDLNet_TCP_Recv(clientSocket, cardType, sizeof(cardType));
                   // SDLNet_TCP_Recv(clientSocket, &duration, sizeof(duration)); //远程执行内存修改数值漏洞
                    handleCardPurchaseLogin(clientSocket, username, password, cardType);
                } else if (strcmp(buffer, "RECHARGE_POINTS") == 0) {
                            // 用户ID
                            int userId;
                            // 充值的积分
                            int rechargePoints;

                            // 处理积分充值逻辑
                            SDLNet_TCP_Recv(clientSocket, &userId, sizeof(userId));
                            printf("userId %d \n",userId);
                            SDLNet_TCP_Recv(clientSocket, &rechargePoints, sizeof(rechargePoints));
                            printf("rechargePoints %d \n",rechargePoints);

                            handlePointsRecharge(clientSocket, userId, rechargePoints);
                           
                        }
            }

           // SDLNet_TCP_Close(server);  
           // printf("server disconnected.\n");
        }

        SDL_Delay(10);  // Avoid high CPU usage
    }
}

int main(int argc, char *argv[]) {
    initDatabase();
    createTables();

    // 初始化SDL2_net
    SDLNet_Init();

    // 设置服务器IP和端口
    IPaddress ip;
    SDLNet_ResolveHost(&ip, NULL, 8989);
    // 打开服务器套接字
    serverserver = SDLNet_TCP_Open(&ip);
    if (!serverserver) {
        fprintf(stderr, "SDLNet_TCP_Open: %s\n", SDLNet_GetError());
        return 1;
    }

    // 创建新线程运行服务器监听函数
    SDL_Thread *serverThread = SDL_CreateThread((SDL_ThreadFunction)serverListen, "ServerThread", NULL);

    // 等待服务器线程结束
    int threadReturnValue;
    SDL_WaitThread(serverThread, &threadReturnValue);

    // 在这里可以处理服务器线程结束后的逻辑
    printf("Server thread ended with return value: %d\n", threadReturnValue);


    // 关闭SDL2_net
    SDLNet_Quit();

    // 关闭数据库连接
    sqlite3_close(db);

    return 0;
}



#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <SDL2/SDL_net.h>
#define TOKEN_LENGTH 16
#define MAX_BUFFER_SIZE 1024
#define SECRET_KEY "your_secret_key"

char* generateSignedToken(const char* RANDOM_TOKEN) {
    char token[MAX_BUFFER_SIZE];
    snprintf(token, sizeof(token), "%s", RANDOM_TOKEN);

    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int digestLength;
    HMAC(EVP_sha256(), SECRET_KEY, strlen(SECRET_KEY), (unsigned char*)token, strlen(token), digest, &digestLength);

    // 将数字签名转换为十六进制字符串
    char hexDigest[EVP_MAX_MD_SIZE * 2 + 1];
    for (unsigned int i = 0; i < digestLength; i++) {
        snprintf(hexDigest + (i * 2), sizeof(hexDigest) - (i * 2), "%02x", digest[i]);
    }

    // 合并令牌和签名
    char* signedToken = malloc(MAX_BUFFER_SIZE);
    snprintf(signedToken, MAX_BUFFER_SIZE, "%s:%s", RANDOM_TOKEN, hexDigest);

    return signedToken;
}
char* generateRandomToken() {
    const char charset[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    char* token = malloc(TOKEN_LENGTH + 1);

    srand((unsigned int)time(NULL));

    for (int i = 0; i < TOKEN_LENGTH; i++) {
        int index = rand() % (int)(sizeof(charset) - 1);
        token[i] = charset[index];
    }

    token[TOKEN_LENGTH] = '\0';  // 添加字符串结束符

    return token;
}
