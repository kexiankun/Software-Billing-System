#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "SDL2/SDL.h"
#include "SDL2/SDL_net.h"
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <SDL2/SDL_net.h>


#define HEARTBEAT_INTERVAL 5000
const char *HEARTBEAT_MESSAGE = "HEARTBEAT";
// 保存用户ID
int loggedInUserId;

TCPsocket client;
Uint32 lastHeartbeatTime = 0;
SDL_mutex *mutex;
int isLoggedIn = 0;

#define MAX_BUFFER_SIZE 1024
#define SECRET_KEY "your_secret_key"

int verifyTokenSignature(const char* signedToken, char* originalToken) {
    char token[MAX_BUFFER_SIZE];
    char receivedDigest[MAX_BUFFER_SIZE];

    // 分割令牌和签名
    if (sscanf(signedToken, "%[^:]:%s", token, receivedDigest) != 2) {
        return 0; // 格式错误
    }

    // 将数字签名转换为十六进制字符串
    int receivedDigestBinaryLength = strlen(receivedDigest) / 2;
    unsigned char* receivedDigestBinary = malloc(receivedDigestBinaryLength);
    for (int i = 0; i < receivedDigestBinaryLength; i++) {
        sscanf(receivedDigest + (i * 2), "%02x", &receivedDigestBinary[i]);
    }

    // 使用HMAC-SHA256重新生成数字签名
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int digestLength;
    HMAC(EVP_sha256(), SECRET_KEY, strlen(SECRET_KEY), (unsigned char*)token, strlen(token), digest, &digestLength);

    // 比较生成的签名和接收到的签名
    int isValidSignature = (memcmp(digest, receivedDigestBinary, digestLength) == 0);

    // 如果签名有效，将原始令牌提取出来
    if (isValidSignature) {
        strcpy(originalToken, token);
    }

    // 释放动态分配的内存
    free(receivedDigestBinary);

    return isValidSignature;
}

// 发送整数给服务器的函数 整数
void sendInt(TCPsocket socket, int value) {
    if (SDLNet_TCP_Send(socket, &value, sizeof(value)) < sizeof(value)) {
        fprintf(stderr, "SDLNet_TCP_Send: %s\n", SDLNet_GetError());
    }
}

// 发送消息到服务器
void sendMessage(TCPsocket client, const char *message) {
    SDL_LockMutex(mutex);
    SDLNet_TCP_Send(client, message, strlen(message) + 1);
    SDL_UnlockMutex(mutex);
}

// 从服务器接收消息
int receiveMessage(TCPsocket client, char *buffer, int bufferSize) {
    int bytesRead = SDLNet_TCP_Recv(client, buffer, bufferSize);
    if (bytesRead <= 0) {
        printf("与服务器的连接断开。\n");
        SDLNet_Quit();
        SDL_Quit();
        exit(1);
    }
    buffer[bytesRead] = '\0';
    return bytesRead;
}

void cleanup() {
    SDL_DestroyMutex(mutex);

    SDLNet_TCP_Close(client);
    SDLNet_Quit();
    SDL_Quit();

}

void sendHeartbeat() {
    SDL_LockMutex(mutex);
    SDLNet_TCP_Send(client, HEARTBEAT_MESSAGE, strlen(HEARTBEAT_MESSAGE) + 1);
    SDL_UnlockMutex(mutex);
}

void handleReceivedData() {
    char buffer[1024];
    int bytesRead = SDLNet_TCP_Recv(client, buffer, sizeof(buffer));

    if (bytesRead <= 0) {
        // 服务器断开连接或发生错误
        printf("服务器已断开连接。\n");
        cleanup();
        exit(1);
    } else {
        buffer[bytesRead] = '\0';

        if (strcmp(buffer, HEARTBEAT_MESSAGE) == 0) {
            // 处理心跳
            printf("收到来自服务器的心跳。\n");
        } else {
            // 处理其他消息
            printf("收到来自服务器的消息：%s\n", buffer);
        }
    }
}

// 心跳发送线程
int heartbeatThread(void *arg) {
    while (1) {
        Uint32 currentTime = SDL_GetTicks();

        if (isLoggedIn) {
            // 发送心跳
            if (currentTime - lastHeartbeatTime > HEARTBEAT_INTERVAL) {
                sendHeartbeat();
                printf("发送心跳中...\n");
                lastHeartbeatTime = currentTime;
            }
        }

        // 休眠一段时间
        SDL_Delay(1000);
    }

    return 0;
}

// 购卡线程
int buyCard(void *arg) {


 
        // 例子：购买一张卡
        sendMessage(client, "BUY_CARD");


        char username[50];
        char password[50];

        printf("输入用户名: ");
        scanf("%s", username);
        sendMessage(client, username);

        printf("输入密码: ");
        scanf("%s", password);
        sendMessage(client, password);


        printf("卡类型:  ");
        printf("1: daily ");
        printf("2: weekly ");
        printf("3: monthly ");
        printf("4: seasonal ");
        printf("5: yearly\n");
         printf("选择购卡类型: ");
        int cardTypeChoice;
        scanf("%d", &cardTypeChoice);

        char cardType[10];

        switch (cardTypeChoice) {
            case 1:
                strcpy(cardType, "daily");
                break;
            case 2:
                strcpy(cardType, "weekly");
                break;
            case 3:
                strcpy(cardType, "monthly");
                break;
            case 4:
                strcpy(cardType, "seasonal");
                break;
            case 5:
                strcpy(cardType, "yearly");
                break;
            default:
                printf("无效的购卡类型选择。\n");
                cleanup();
                exit(1);
        }

        // 发送卡类型
        sendMessage(client, cardType);

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
                } else if (strcmp(token, "CARD_ALREADY_OWNED") == 0) {
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
    


    return 0;
}

// 注册线程
int registerThread(void *arg) {
    // 发送注册请求
    sendMessage(client, "REGISTER");

    char username[50];
    char password[50];

    // 提供用户名和密码
    printf("输入用户名: ");
    scanf("%s", username);
    sendMessage(client, username);

    printf("输入密码: ");
    scanf("%s", password);
    sendMessage(client, password);

    // 从服务器接收一条注册响应消息
    char response[512];
    if (receiveMessage(client, response, sizeof(response))) {
        // 判断是否注册成功
        if (strcmp(response, "REGISTER_SUCCESS") == 0) {
            printf("注册成功！\n");
            getchar();
            // 在这里添加处理注册成功后的逻辑
        } else if (strcmp(response, "REGISTER_FAILZY") == 0) {
            printf("注册失败。用户名已被占用\n");

        }else if (strcmp(response, "REGISTER_FAILYHM") == 0) {
            printf("注册失败。检查用户名和密码是否符合要求,必须包含大小写，不小于六位数！\n");

        }else if (strcmp(response, "REGISTER_FAIL") == 0) {
            printf("注册失败。用户名可能已被占用。\n");

        }

        // 其他响应的判断
        // ...
    } else {
        // 处理接收消息失败的情况
        printf("接收服务器响应失败。\n");
    }

    return 0;
}


// 响应类型的枚举
enum ResponseType {
    LOGIN_SUCCESS,
    LOGIN_ALREADY,
    LOGIN_FAIL,
    CARD_USAGE_FAIL,
    LOGIN_FAIL_POINTS_ZERO,
    LOGIN_FAIL_SERVER_FULL,
    UNKNOWN_RESPONSE
};

// 将响应类型映射到枚举值
enum ResponseType mapResponseType(const char* response) {
    if (strcmp(response, "LOGIN_SUCCESS") == 0) {
        return LOGIN_SUCCESS;
    } else if (strcmp(response, "LOGIN_ALREADY") == 0) {
        return LOGIN_ALREADY;
    } else if (strcmp(response, "LOGIN_FAIL") == 0) {
        return LOGIN_FAIL;
    } else if (strcmp(response, "CARD_USAGE_FAIL") == 0) {
        return CARD_USAGE_FAIL;
    } else if (strcmp(response, "LOGIN_FAIL_POINTS_ZERO") == 0) {
        return LOGIN_FAIL_POINTS_ZERO;
    } else if (strcmp(response, "LOGIN_FAIL_SERVER_FULL") == 0) {
        return LOGIN_FAIL_SERVER_FULL;
    } else {
        return UNKNOWN_RESPONSE;
    }
}

// 处理登录响应
void handleLoginResponse(const char* originalToken) {
    char response[512];
    if (receiveMessage(client, response, sizeof(response))) {
        // 将响应类型映射到枚举值
        enum ResponseType responseType = mapResponseType(response);

        // 处理登录的响应类型
        switch (responseType) {
            case LOGIN_SUCCESS:
                printf("登录成功！\n");
                isLoggedIn = 1;  // 设置为已登录状态

                // 发送心跳标记
                sendMessage(client, "HEARTBEAT");

                // 创建心跳发送线程
                SDL_Thread *heartbeatThreadHandle = SDL_CreateThread(heartbeatThread, "HeartbeatThread", NULL);
                if (!heartbeatThreadHandle) {
                    fprintf(stderr, "无法创建心跳发送线程：%s\n", SDL_GetError());
                    cleanup();
                    exit(EXIT_FAILURE);
                }
                break;

            case LOGIN_ALREADY:
                printf("用户在线中......\n");
                break;

            case LOGIN_FAIL:
                printf("登录失败。用户名或密码错误。\n");
                break;

            case CARD_USAGE_FAIL:
                printf("用户没有有效的卡。\n");
                break;

            case LOGIN_FAIL_POINTS_ZERO:
                printf("登录失败。用户点数为零。\n");
                break;

            case LOGIN_FAIL_SERVER_FULL:
                printf("在线客户端数量已达到上限，无法添加新客户端。\n");
                break;

            case UNKNOWN_RESPONSE:
                printf("未知的登录响应类型：%s\n", response);
                break;
        }
    } else {
        printf("接收登录响应消息失败。\n");
    }
}


// 登录线程
int loginThread(void *arg) {
    // 发送登录请求
    sendMessage(client, "LOGIN");

    char username[50];
    char password[50];

    // 输入用户名
    printf("输入用户名: ");
    scanf("%s", username);
    sendMessage(client, username);

    // 输入密码
    printf("输入密码: ");
    scanf("%s", password);
    sendMessage(client, password);

    // 从服务器接收登录的响应消息
    char buffer[MAX_BUFFER_SIZE];
    int bytesReceived = SDLNet_TCP_Recv(client, buffer, MAX_BUFFER_SIZE);

    if (bytesReceived > 0) {
        // 创建缓冲区以存储原始令牌
        char originalToken[MAX_BUFFER_SIZE];

        // 验证签名并提取原始令牌
        if (verifyTokenSignature(buffer, originalToken)) {
            printf("客户端验证令牌签名：有效\n");
            printf("原始令牌: %s\n", originalToken);

            // 处理登录响应
            handleLoginResponse(originalToken);
        } else {
            printf("客户端验证令牌签名：无效\n");
        }
    } else {
        printf("接收登录响应消息失败。\n");
    }

    return 0;
}


int main(int argc, char *argv[]) {
    SDLNet_Init();

    // 设置服务器IP和端口
    IPaddress ip;
    SDLNet_ResolveHost(&ip, "127.0.0.1", 8989);  // 121.37.89.210

    // 打开客户端套接字
    client = SDLNet_TCP_Open(&ip);
    if (!client) {
        fprintf(stderr, "SDLNet_TCP_Open: %s\n", SDLNet_GetError());
        return 1;
    }

    mutex = SDL_CreateMutex();
    if (!mutex) {
        fprintf(stderr, "无法创建互斥锁：%s\n", SDL_GetError());
        cleanup();
        return 1;
    }

    printf("已连接到服务器。\n\n");

    while (1) {
        printf("功能:  ");
        printf("1. 登录 ");
        printf("2. 注册 ");
        printf("3. 购买卡 ");
        printf("0. 退出\n\n");
        printf("请选择操作: \n");
        int choice;
        scanf("%d", &choice);

        switch (choice) {
            case 0:
                // 退出程序
                cleanup();
                return 0;
            case 1:
                // 创建登录线程
                SDL_Thread *loginThreadHandle = SDL_CreateThread((SDL_ThreadFunction)loginThread, "LoginThread", NULL);
                if (!loginThreadHandle) {
                    fprintf(stderr, "无法创建登录线程：%s\n", SDL_GetError());
                    cleanup();
                    return 1;
                }
                SDL_WaitThread(loginThreadHandle, NULL);
                break;
            case 2:
                // 创建注册线程
                SDL_Thread *registerThreadHandle = SDL_CreateThread((SDL_ThreadFunction)registerThread, "RegisterThread", NULL);
                if (!registerThreadHandle) {
                    fprintf(stderr, "无法创建注册线程：%s\n", SDL_GetError());
                    cleanup();
                    return 1;
                }
                SDL_WaitThread(registerThreadHandle, NULL);
                break;
            case 3:
            
                 // 创建购卡线程
                 SDL_Thread *buyCardThreadId = SDL_CreateThread((SDL_ThreadFunction)buyCard, "buyCard", NULL);
                if (!buyCardThreadId) {
                    fprintf(stderr, "无法创建注册线程：%s\n", SDL_GetError());
                    cleanup();
                    return 1;
                }
                SDL_WaitThread(buyCardThreadId, NULL);     
                break;
            default:
                printf("无效的选项，请重新输入。\n");
                break;
        }
    }

    return 0;
}

