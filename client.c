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
// �����û�ID
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

    // �ָ����ƺ�ǩ��
    if (sscanf(signedToken, "%[^:]:%s", token, receivedDigest) != 2) {
        return 0; // ��ʽ����
    }

    // ������ǩ��ת��Ϊʮ�������ַ���
    int receivedDigestBinaryLength = strlen(receivedDigest) / 2;
    unsigned char* receivedDigestBinary = malloc(receivedDigestBinaryLength);
    for (int i = 0; i < receivedDigestBinaryLength; i++) {
        sscanf(receivedDigest + (i * 2), "%02x", &receivedDigestBinary[i]);
    }

    // ʹ��HMAC-SHA256������������ǩ��
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int digestLength;
    HMAC(EVP_sha256(), SECRET_KEY, strlen(SECRET_KEY), (unsigned char*)token, strlen(token), digest, &digestLength);

    // �Ƚ����ɵ�ǩ���ͽ��յ���ǩ��
    int isValidSignature = (memcmp(digest, receivedDigestBinary, digestLength) == 0);

    // ���ǩ����Ч����ԭʼ������ȡ����
    if (isValidSignature) {
        strcpy(originalToken, token);
    }

    // �ͷŶ�̬������ڴ�
    free(receivedDigestBinary);

    return isValidSignature;
}

// �����������������ĺ��� ����
void sendInt(TCPsocket socket, int value) {
    if (SDLNet_TCP_Send(socket, &value, sizeof(value)) < sizeof(value)) {
        fprintf(stderr, "SDLNet_TCP_Send: %s\n", SDLNet_GetError());
    }
}

// ������Ϣ��������
void sendMessage(TCPsocket client, const char *message) {
    SDL_LockMutex(mutex);
    SDLNet_TCP_Send(client, message, strlen(message) + 1);
    SDL_UnlockMutex(mutex);
}

// �ӷ�����������Ϣ
int receiveMessage(TCPsocket client, char *buffer, int bufferSize) {
    int bytesRead = SDLNet_TCP_Recv(client, buffer, bufferSize);
    if (bytesRead <= 0) {
        printf("������������ӶϿ���\n");
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
        // �������Ͽ����ӻ�������
        printf("�������ѶϿ����ӡ�\n");
        cleanup();
        exit(1);
    } else {
        buffer[bytesRead] = '\0';

        if (strcmp(buffer, HEARTBEAT_MESSAGE) == 0) {
            // ��������
            printf("�յ����Է�������������\n");
        } else {
            // ����������Ϣ
            printf("�յ����Է���������Ϣ��%s\n", buffer);
        }
    }
}

// ���������߳�
int heartbeatThread(void *arg) {
    while (1) {
        Uint32 currentTime = SDL_GetTicks();

        if (isLoggedIn) {
            // ��������
            if (currentTime - lastHeartbeatTime > HEARTBEAT_INTERVAL) {
                sendHeartbeat();
                printf("����������...\n");
                lastHeartbeatTime = currentTime;
            }
        }

        // ����һ��ʱ��
        SDL_Delay(1000);
    }

    return 0;
}

// �����߳�
int buyCard(void *arg) {


 
        // ���ӣ�����һ�ſ�
        sendMessage(client, "BUY_CARD");


        char username[50];
        char password[50];

        printf("�����û���: ");
        scanf("%s", username);
        sendMessage(client, username);

        printf("��������: ");
        scanf("%s", password);
        sendMessage(client, password);


        printf("������:  ");
        printf("1: daily ");
        printf("2: weekly ");
        printf("3: monthly ");
        printf("4: seasonal ");
        printf("5: yearly\n");
         printf("ѡ�񹺿�����: ");
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
                printf("��Ч�Ĺ�������ѡ��\n");
                cleanup();
                exit(1);
        }

        // ���Ϳ�����
        sendMessage(client, cardType);

        // �ӷ��������չ��򿨵���Ӧ��Ϣ
        char response[512];
        if (receiveMessage(client, response, sizeof(response))) {
            // �жϹ��򿨵���Ӧ����
            char *token = strtok(response, ",");
            if (token != NULL) {
                // ������Ӧ���ͽ��д���
                if (strcmp(token, "BUY_CARD_SUCCESS") == 0) {
                    // ��ȡ���º�Ļ���
                    token = strtok(NULL, ",");
                    if (token != NULL) {
                        int updatedPoints = atoi(token);
                        printf("���򿨳ɹ������º�Ļ��֣�%d\n", updatedPoints);
                    } else {
                        printf("���򿨳ɹ�����δ����ȡ���º�Ļ��֡�\n");
                    }
                } else if (strcmp(token, "BUY_CARD_INSUFFICIENT_POINTS") == 0) {
                    printf("����ʧ�ܡ����ֲ��㡣\n");
                } else if (strcmp(token, "BUY_CARD_FAIL") == 0) {
                    printf("����ʧ�ܡ�\n");
                } else if (strcmp(token, "CARD_ALREADY_OWNED") == 0) {
                    printf("�����ظ�����\n");
                } else {
                    printf("δ֪�Ĺ�����Ӧ���ͣ�%s\n", token);
                }
            } else {
                printf("�޷��������򿨵���Ӧ��Ϣ��\n");
            }
        } else {
            // ���������Ϣʧ�ܵ����
            printf("���չ�����Ӧ��Ϣʧ�ܡ�\n");
        }
    


    return 0;
}

// ע���߳�
int registerThread(void *arg) {
    // ����ע������
    sendMessage(client, "REGISTER");

    char username[50];
    char password[50];

    // �ṩ�û���������
    printf("�����û���: ");
    scanf("%s", username);
    sendMessage(client, username);

    printf("��������: ");
    scanf("%s", password);
    sendMessage(client, password);

    // �ӷ���������һ��ע����Ӧ��Ϣ
    char response[512];
    if (receiveMessage(client, response, sizeof(response))) {
        // �ж��Ƿ�ע��ɹ�
        if (strcmp(response, "REGISTER_SUCCESS") == 0) {
            printf("ע��ɹ���\n");
            getchar();
            // ��������Ӵ���ע��ɹ�����߼�
        } else if (strcmp(response, "REGISTER_FAILZY") == 0) {
            printf("ע��ʧ�ܡ��û����ѱ�ռ��\n");

        }else if (strcmp(response, "REGISTER_FAILYHM") == 0) {
            printf("ע��ʧ�ܡ�����û����������Ƿ����Ҫ��,���������Сд����С����λ����\n");

        }else if (strcmp(response, "REGISTER_FAIL") == 0) {
            printf("ע��ʧ�ܡ��û��������ѱ�ռ�á�\n");

        }

        // ������Ӧ���ж�
        // ...
    } else {
        // ���������Ϣʧ�ܵ����
        printf("���շ�������Ӧʧ�ܡ�\n");
    }

    return 0;
}


// ��Ӧ���͵�ö��
enum ResponseType {
    LOGIN_SUCCESS,
    LOGIN_ALREADY,
    LOGIN_FAIL,
    CARD_USAGE_FAIL,
    LOGIN_FAIL_POINTS_ZERO,
    LOGIN_FAIL_SERVER_FULL,
    UNKNOWN_RESPONSE
};

// ����Ӧ����ӳ�䵽ö��ֵ
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

// �����¼��Ӧ
void handleLoginResponse(const char* originalToken) {
    char response[512];
    if (receiveMessage(client, response, sizeof(response))) {
        // ����Ӧ����ӳ�䵽ö��ֵ
        enum ResponseType responseType = mapResponseType(response);

        // �����¼����Ӧ����
        switch (responseType) {
            case LOGIN_SUCCESS:
                printf("��¼�ɹ���\n");
                isLoggedIn = 1;  // ����Ϊ�ѵ�¼״̬

                // �����������
                sendMessage(client, "HEARTBEAT");

                // �������������߳�
                SDL_Thread *heartbeatThreadHandle = SDL_CreateThread(heartbeatThread, "HeartbeatThread", NULL);
                if (!heartbeatThreadHandle) {
                    fprintf(stderr, "�޷��������������̣߳�%s\n", SDL_GetError());
                    cleanup();
                    exit(EXIT_FAILURE);
                }
                break;

            case LOGIN_ALREADY:
                printf("�û�������......\n");
                break;

            case LOGIN_FAIL:
                printf("��¼ʧ�ܡ��û������������\n");
                break;

            case CARD_USAGE_FAIL:
                printf("�û�û����Ч�Ŀ���\n");
                break;

            case LOGIN_FAIL_POINTS_ZERO:
                printf("��¼ʧ�ܡ��û�����Ϊ�㡣\n");
                break;

            case LOGIN_FAIL_SERVER_FULL:
                printf("���߿ͻ��������Ѵﵽ���ޣ��޷�����¿ͻ��ˡ�\n");
                break;

            case UNKNOWN_RESPONSE:
                printf("δ֪�ĵ�¼��Ӧ���ͣ�%s\n", response);
                break;
        }
    } else {
        printf("���յ�¼��Ӧ��Ϣʧ�ܡ�\n");
    }
}


// ��¼�߳�
int loginThread(void *arg) {
    // ���͵�¼����
    sendMessage(client, "LOGIN");

    char username[50];
    char password[50];

    // �����û���
    printf("�����û���: ");
    scanf("%s", username);
    sendMessage(client, username);

    // ��������
    printf("��������: ");
    scanf("%s", password);
    sendMessage(client, password);

    // �ӷ��������յ�¼����Ӧ��Ϣ
    char buffer[MAX_BUFFER_SIZE];
    int bytesReceived = SDLNet_TCP_Recv(client, buffer, MAX_BUFFER_SIZE);

    if (bytesReceived > 0) {
        // �����������Դ洢ԭʼ����
        char originalToken[MAX_BUFFER_SIZE];

        // ��֤ǩ������ȡԭʼ����
        if (verifyTokenSignature(buffer, originalToken)) {
            printf("�ͻ�����֤����ǩ������Ч\n");
            printf("ԭʼ����: %s\n", originalToken);

            // �����¼��Ӧ
            handleLoginResponse(originalToken);
        } else {
            printf("�ͻ�����֤����ǩ������Ч\n");
        }
    } else {
        printf("���յ�¼��Ӧ��Ϣʧ�ܡ�\n");
    }

    return 0;
}


int main(int argc, char *argv[]) {
    SDLNet_Init();

    // ���÷�����IP�Ͷ˿�
    IPaddress ip;
    SDLNet_ResolveHost(&ip, "127.0.0.1", 8989);  // 121.37.89.210

    // �򿪿ͻ����׽���
    client = SDLNet_TCP_Open(&ip);
    if (!client) {
        fprintf(stderr, "SDLNet_TCP_Open: %s\n", SDLNet_GetError());
        return 1;
    }

    mutex = SDL_CreateMutex();
    if (!mutex) {
        fprintf(stderr, "�޷�������������%s\n", SDL_GetError());
        cleanup();
        return 1;
    }

    printf("�����ӵ���������\n\n");

    while (1) {
        printf("����:  ");
        printf("1. ��¼ ");
        printf("2. ע�� ");
        printf("3. ���� ");
        printf("0. �˳�\n\n");
        printf("��ѡ�����: \n");
        int choice;
        scanf("%d", &choice);

        switch (choice) {
            case 0:
                // �˳�����
                cleanup();
                return 0;
            case 1:
                // ������¼�߳�
                SDL_Thread *loginThreadHandle = SDL_CreateThread((SDL_ThreadFunction)loginThread, "LoginThread", NULL);
                if (!loginThreadHandle) {
                    fprintf(stderr, "�޷�������¼�̣߳�%s\n", SDL_GetError());
                    cleanup();
                    return 1;
                }
                SDL_WaitThread(loginThreadHandle, NULL);
                break;
            case 2:
                // ����ע���߳�
                SDL_Thread *registerThreadHandle = SDL_CreateThread((SDL_ThreadFunction)registerThread, "RegisterThread", NULL);
                if (!registerThreadHandle) {
                    fprintf(stderr, "�޷�����ע���̣߳�%s\n", SDL_GetError());
                    cleanup();
                    return 1;
                }
                SDL_WaitThread(registerThreadHandle, NULL);
                break;
            case 3:
            
                 // ���������߳�
                 SDL_Thread *buyCardThreadId = SDL_CreateThread((SDL_ThreadFunction)buyCard, "buyCard", NULL);
                if (!buyCardThreadId) {
                    fprintf(stderr, "�޷�����ע���̣߳�%s\n", SDL_GetError());
                    cleanup();
                    return 1;
                }
                SDL_WaitThread(buyCardThreadId, NULL);     
                break;
            default:
                printf("��Ч��ѡ����������롣\n");
                break;
        }
    }

    return 0;
}

