#include <stdio.h>
#include <sqlite3.h>
#include <SDL2/SDL_net.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <locale.h>

// ���� SQLite ���ݿ�����
sqlite3 *db;

// ������Ϣ���������ĺ���
void sendMessage(TCPsocket socket, const char *message) {
    int messageLength = strlen(message) + 1;  // �����ַ���������'\0'

    if (SDLNet_TCP_Send(socket, message, messageLength) < messageLength) {
        fprintf(stderr, "SDLNet_TCP_Send: %s\n", SDLNet_GetError());
    }
}

// �����������������ĺ���
void sendInt(TCPsocket socket, int value) {
    if (SDLNet_TCP_Send(socket, &value, sizeof(value)) < sizeof(value)) {
        fprintf(stderr, "SDLNet_TCP_Send: %s\n", SDLNet_GetError());
    }
}

// �ӷ�����������Ϣ�ĺ���
bool receiveMessage(TCPsocket socket, char *buffer, int bufferSize) {
    int bytesRead = SDLNet_TCP_Recv(socket, buffer, bufferSize);
    if (bytesRead <= 0) {
        fprintf(stderr, "SDLNet_TCP_Recv: %s\n", SDLNet_GetError());
        return false;
    }
    return true;
}

// ִ�в�ѯ��������ȡ�û�ID
int getUserIdByUsername(const char *username) {
    char sql[100];
    sprintf(sql, "SELECT id FROM users WHERE username='%s';", username);

    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, 0);

    if (rc != SQLITE_OK) {
        fprintf(stderr, "�޷�׼����ѯ��䣺%s\n", sqlite3_errmsg(db));
        return -1;  // ���ظ�ֵ��ʾ��ѯʧ��
    }

    rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        int userId = sqlite3_column_int(stmt, 0);
        sqlite3_finalize(stmt);
        return userId;  // ���ز�ѯ�����û�ID
    } else if (rc == SQLITE_DONE) {
        fprintf(stderr, "δ�ҵ�ƥ����û���%s\n", username);
        sqlite3_finalize(stmt);
        return -1;  // ���ظ�ֵ��ʾ��ѯʧ��
    } else {
        fprintf(stderr, "��ѯִ��ʧ�ܣ�%s\n", sqlite3_errmsg(db));
        sqlite3_finalize(stmt);
        return -1;  // ���ظ�ֵ��ʾ��ѯʧ��
    }
}

// ������ֳ�ֵ�߼�
void handlePointsRecharge(TCPsocket client, int userId, int points) {
    // ���ͻ��ֳ�ֵ����
    sendMessage(client, "RECHARGE_POINTS");

    // �ṩ�û�ID�ͳ�ֵ����
    sendMessage(client, (char *)&userId);
    sendMessage(client, (char *)&points);

    // �ӷ���������һ����Ϣ
    char response[512];
    if (receiveMessage(client, response, sizeof(response))) {
        // �ж��Ƿ���ֳ�ֵ�ɹ�
        if (strstr(response, "RECHARGE_POINTS_SUCCESS")) {
            int userPoints;
            sscanf(response, "RECHARGE_POINTS_SUCCESS,%d", &userPoints);
            printf("���ֳ�ֵ�ɹ�����ǰ���֣�%d\n", userPoints);
            // ��������Ӵ�����ֳ�ֵ�ɹ�����߼�
        } else if (strcmp(response, "RECHARGE_POINTS_FAIL") == 0) {
            printf("���ֳ�ֵʧ�ܡ�\n");
            // ��������Ӵ�����ֳ�ֵʧ�ܺ���߼�
        }

        // ������Ӧ���ж�
        // ...
    } else {
        // ���������Ϣʧ�ܵ����
        printf("���շ�������Ӧʧ�ܡ�\n");
    }
}



int gouka() {

   // setlocale(LC_ALL, "");  // ���ñ��ػ�֧�֣���֧����������

    // ��ʼ��SDL2_net
    SDLNet_Init();

    // ����Զ�����ݿ������IP�Ͷ˿�
    IPaddress ip;
    SDLNet_ResolveHost(&ip, "127.0.0.1", 8989);  // ���滻��Զ�����ݿ��������IP�Ͷ˿�
while (1)
{
    // �򿪿ͻ����׽���
    TCPsocket client = SDLNet_TCP_Open(&ip);
    if (!client) {
        fprintf(stderr, "SDLNet_TCP_Open: %s\n", SDLNet_GetError());
        return 1;
    }

    // ��ʼ�����ݿ�����
    int rc = sqlite3_open("user_data.db", &db);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "�޷������ݿ⣺%s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        return 1;
    }

    // ͨ���û�����ȡ�û�ID
char usernameToQuery[50];

printf("�����û���: ");
scanf("%s", usernameToQuery);

int userId = getUserIdByUsername(usernameToQuery);

if (userId != -1) {
    printf("�û� '%s' ��Ψһ��ʶ���ǣ�%d\n", usernameToQuery, userId);

      // ���ӣ�����һ�ſ�
        sendMessage(client, "BUY_CARD");

        // �ṩ�û�ID�������ͺ�ʱ��
        sendInt(client, userId);
        
        char cardType[10] = "seasonal";  // �滻Ϊʵ�ʵĿ����� day�쿨 week�ܿ� 
        sendMessage(client, cardType);
       
        int duration = 7776000;  // �滻Ϊʵ�ʵ�ʱ�� 
        //һ����� 86,400 ��       daily
        //һ�ܰ��� 604,800 ��      weekly
        //һ�°��� 2,592,000��     monthly
        //һ������ 7,776,000��     seasonal
        //һ����� 31,536,000��    yearly

        sendMessage(client, (char *)&duration);
 //getchar();
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
         
        }  else if (strcmp(token, "CARD_ALREADY_OWNED") == 0) {
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

    } else {
        fprintf(stderr, "��ȡ�û�IDʧ�ܡ�\n");
        
    }
    // �ر����ݿ�����
    sqlite3_close(db);
    // �رտͻ����׽���
    SDLNet_TCP_Close(client);
}

    // �ر�SDL2_net
    SDLNet_Quit();

    return 0;
}


int jifen() {

   // setlocale(LC_ALL, "");  // ���ñ��ػ�֧�֣���֧����������

    // ��ʼ��SDL2_net
    SDLNet_Init();

    // ����Զ�����ݿ������IP�Ͷ˿�
    IPaddress ip;
    SDLNet_ResolveHost(&ip, "127.0.0.1", 8989);  // ���滻��Զ�����ݿ��������IP�Ͷ˿�
while (1)
{
    


    // �򿪿ͻ����׽���
    TCPsocket client = SDLNet_TCP_Open(&ip);
    if (!client) {
        fprintf(stderr, "SDLNet_TCP_Open: %s\n", SDLNet_GetError());
        return 1;
    }

    // ��ʼ�����ݿ�����
    int rc = sqlite3_open("user_data.db", &db);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "�޷������ݿ⣺%s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        return 1;
    }

    // ͨ���û�����ȡ�û�ID
char usernameToQuery[50];

printf("�����û���: ");
scanf("%s", usernameToQuery);

int userId = getUserIdByUsername(usernameToQuery);

if (userId != -1) {
    printf("�û� '%s' ��Ψһ��ʶ���ǣ�%d\n", usernameToQuery, userId);

/////////////////////////////////////////////////////////////////////////

int rechargePoints; // ֱ�Ӷ���Ϊ��������
// ���ӣ����ֳ�ֵ
printf("�������: ");
scanf("%d", &rechargePoints); // ʹ�� &rechargePoints ��ȡ���������ĵ�ַ

// ���ͻ��ֳ�ֵ����
sendMessage(client, "RECHARGE_POINTS");

// �ṩ�û�ID�ͳ�ֵ����
sendInt(client, userId);
SDLNet_TCP_Send(client,&rechargePoints,sizeof(rechargePoints));


// �ӷ���������һ����Ϣ
char response[512];
if (receiveMessage(client, response, sizeof(response))) {
    // �ж��Ƿ���ֳ�ֵ�ɹ�
    if (strstr(response, "RECHARGE_POINTS_SUCCESS")) {
        int userPoints;
        sscanf(response, "RECHARGE_POINTS_SUCCESS,%d", &userPoints);
        printf("���ֳ�ֵ�ɹ�����ǰ���֣�%d\n", userPoints);
    } else if (strcmp(response, "RECHARGE_POINTS_FAIL") == 0) {
        printf("���ֳ�ֵʧ�ܡ�\n");
        // ��������Ӵ�����ֳ�ֵʧ�ܺ���߼�
    }

    // ������Ӧ���ж�
    // ...
} else {
    // ���������Ϣʧ�ܵ����
    printf("���շ�������Ӧʧ�ܡ�\n");

}
    } else {
        fprintf(stderr, "��ȡ�û�IDʧ�ܡ�\n");
        
    }

    // �ر����ݿ�����
    sqlite3_close(db);
    // �رտͻ����׽���
    SDLNet_TCP_Close(client);
}


    // �ر�SDL2_net
    SDLNet_Quit();

    return 0;
}


int main(int argc, char *argv[]) {

   jifen();

   // gouka();

    return  0;
}