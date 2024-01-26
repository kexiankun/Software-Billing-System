#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <sqlite3.h>
#include <time.h>
#include <SDL2/SDL_net.h>
#include <SDL2/SDL_thread.h>



#define MAX_CLIENTS 1000   // ���ͻ�������
bool userLoggedIn = false; // ��־�û� �Ƿ��ѵ�¼
int loggedInUserId;        // �����û�ID
char loggedInUsername[50]; // �����û���
int userPoints = 0;        // ��ȡ�û����� ����
const char *cardType = 0;  // ��������

// ���� Socket Set
SDLNet_SocketSet socketSet;
// TCP������
TCPsocket serverserver;
// �ͻ�������
TCPsocket clientSocket;



// �û��ṹ��
typedef struct{ 
    int id;               // �û�Ψһ��ʶ��
    char username[50];    // �û�������󳤶�Ϊ50���ַ�
    char password[50];    // �û����룬��󳤶�Ϊ50���ַ�
    int points;           // �û����ֻ�
} User;


// ���ṹ��
typedef struct {
    int id;                 // ����Ψһ��ʶ��
    int user_id;            // �û�ID�����������û�
    char type[10];          // �������ͣ����� "daily", "weekly", "monthly", etc.
    int duration;           // ʹ��ʱ��������Ϊ��λ
    time_t purchase_time;   // ����ʱ�䣬��¼���Ĺ���ʱ��
    time_t expiration_time; // ����ʱ��
} Card;

// SQLite���ݿ�����
sqlite3 *db;

// �����ʾ���ӿͻ��˵Ľṹ��
typedef struct {
    TCPsocket socket;
    int userId;
    char username[50];
    int userLoggedIn;
    time_t lastHeartbeat;
} ConnectedClient;


// �洢���ӿͻ��˵�����
ConnectedClient clients[MAX_CLIENTS];

bool loginUser(const char *username, const char *password, User *user);

char* generateSignedToken(const char* RANDOM_TOKEN);//����
char* generateRandomToken();


// ������ʾ��ǰ���ߵĿͻ�������
int onlineClientsCount = 0;
// �Ƴ��û�
void removeUser(const char *username) {
    for (int i = 0; i < onlineClientsCount; ++i) {
        if (strcmp(clients[i].username, username) == 0) {
            // ���ҵ����û����б����Ƴ�
            for (int j = i; j < onlineClientsCount - 1; ++j) {
                clients[j] = clients[j + 1];
            }
            onlineClientsCount--;
            break;
        }
    }
}

// ����û��Ƿ�����
int isUserOnline(const char *username) {
    for (int i = 0; i < onlineClientsCount; ++i) {
        if (strcmp(clients[i].username, username) == 0) {
            // �û��Ѿ�����
            return 1;
        }
    }
    // �û�������
    return 0;
}

// ����������߿ͻ��˵ĺ���
void addOnlineClient(TCPsocket clientSocket, int userId, const char *username) {
    if (onlineClientsCount < MAX_CLIENTS) {
        // ���пռ�����µ����߿ͻ���

        // ����һ���µ� ConnectedClient �ṹ���ʾ���ӵĿͻ���
        ConnectedClient newClient;
        newClient.socket = clientSocket;
        newClient.userId = userId;
        strncpy(newClient.username, username, sizeof(newClient.username) - 1);
        newClient.userLoggedIn = 1;  // �û��ѵ�¼
        time(&newClient.lastHeartbeat);  // �����������ʱ��

        // ���¿ͻ�����ӵ����߿ͻ���������
        clients[onlineClientsCount] = newClient;
        onlineClientsCount++;

        printf("�û� %s �ѳɹ���¼��\n", username);

    } else {
        // ���߿ͻ��������������޷�����¿ͻ���
        printf("���߿ͻ��������Ѵﵽ���ޣ��޷�����¿ͻ��ˡ�\n");

        // ������Ӧ����Ϣ���ͻ��ˣ�֪ͨ���޷���¼
        SDLNet_TCP_Send(clientSocket, "LOGIN_FAIL_SERVER_FULL",sizeof("LOGIN_FAIL_SERVER_FULL"));
    
    }
}

// ����������Ϣ�ĺ���
void handleHeartbeat(int userId) {
    for (int i = 0; i < MAX_CLIENTS; ++i) {
        if (clients[i].socket && clients[i].userId == userId) {
            printf("����������Ϣ - �û� %s (ID: %d)\n", clients[i].username, clients[i].userId);
            // �������һ������ʱ��
            time(&clients[i].lastHeartbeat);
            break;
        }
    }
}

// ��鲢�Ͽ�����Ծ�ͻ��˵ĺ���
void checkInactiveClients(int userId) {
    time_t currentTime;
    time(&currentTime);

    for (int i = 0; i < MAX_CLIENTS; ++i) {
    if (clients[i].socket && clients[i].userId == userId) {
            // ���ͻ����Ƿ������30���ڷ��͹�������Ϣ
            if (currentTime - clients[i].lastHeartbeat > 30) {
                printf("�ͻ��� %s (ID: %d) ��ʱ�䲻��Ծ���Ͽ����ӡ�\n", clients[i].username, clients[i].userId);

                removeUser(clients[i].username);//��������û�

                SDLNet_TCP_Close(clients[i].socket);
                clients[i].socket = NULL;
            }
        }
    }
}

// ����¿ͻ��˵�����ĺ���
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

            printf("�ͻ���-> (�û���: %s) (����: %d) (��������: %s) (ID: %d) (����: %d) (����ʱ��: %d-%d-%d-%d-%d-%d)���ӳɹ���\n", clients[i].username, userPoints,
            cardType, clients[i].userId, clients[i].userLoggedIn, purchase_tm->tm_year + 1900, purchase_tm->tm_mon + 1, purchase_tm->tm_mday,
                  purchase_tm->tm_hour, purchase_tm->tm_min, purchase_tm->tm_sec);

            //SDLNet_TCP_Send(socket, &clients[i].userId, sizeof(clients[i].userId));//�û�ID ���͸��ͻ���
            break;
        }
    }
}

// ����ͻ������Ӻ���Ϣ�ĺ���
void handleClient(void* data ) {
    char buffer[512];

   ConnectedClient* connectedClient = (ConnectedClient*)data;
    TCPsocket clientSocket = connectedClient->socket;   //�ͷ���
    int userId = connectedClient->userId;               //�û�ID
    const char* username = connectedClient->username;   //�û���
    int userLoggedIn= connectedClient->userLoggedIn;    //�û���ʶ 1���� 0������

    time_t lastHeartbeat=connectedClient->lastHeartbeat;//�û�����ʱ��

    addClient(clientSocket, userId, username, userLoggedIn, lastHeartbeat);

    while (SDLNet_TCP_Recv(clientSocket, buffer, sizeof(buffer)) > 0) {
        // ������յ�����Ϣ
        if (strcmp(buffer, "HEARTBEAT") == 0) {
            // ����������Ϣ
            handleHeartbeat(userId);
        } else {
            // ������Ҫ�����������͵���Ϣ
            // ...
        }

        // ��鲻��Ծ�Ŀͻ��˲��Ͽ�����
        checkInactiveClients(userId);
    }

    // �ͻ��˶Ͽ�����
    for (int i = 0; i < MAX_CLIENTS; ++i) {
        if (clients[i].socket == clientSocket) {
        //    printf("�ͻ��� %s (ID: %d) �Ͽ����ӡ�\n", clients[i].username, clients[i].userId);
            clients[i].userId = userId;
            strcpy(clients[i].username, username);
               
            clients[i].userLoggedIn = 0;
            clients[i].lastHeartbeat= lastHeartbeat;
            time(&clients[i].lastHeartbeat);  // �õ�ǰʱ������ֶ�

            struct tm *purchase_tm = localtime(&clients[i].lastHeartbeat);

            printf("�ͻ���-> (�û���: %s) (����: %d) (��������: %s) (ID: %d) (����: %d) (�ǳ�ʱ��: %d-%d-%d-%d-%d-%d)�Ͽ����ӡ�\n", clients[i].username, userPoints,
            cardType, clients[i].userId, clients[i].userLoggedIn, purchase_tm->tm_year + 1900, purchase_tm->tm_mon + 1, purchase_tm->tm_mday,
            purchase_tm->tm_hour, purchase_tm->tm_min, purchase_tm->tm_sec);


            removeUser(clients[i].username);//��������û�

            SDLNet_TCP_Close(clientSocket);
            clients[i].socket = NULL;
            break;
        }
    }
}

// ��ʼ�����ݿ�����
void initDatabase() {
    int rc = sqlite3_open("user_data.db", &db);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "�޷������ݿ⣺%s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        exit(1);
    }
}
// �����û���Ϳ���
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

// ��ѯ��������ȡ�û�ID
int getUserIdByUsername(const char *username) {
    char sql[100];
    sprintf(sql, "SELECT id FROM users WHERE username='%s';", username);

    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, 0);

    if (rc != SQLITE_OK) {
        fprintf(stderr, "Failed to prepare statement: %s\n", sqlite3_errmsg(db));
        return -1;  // ���ظ�ֵ��ʾ��ѯʧ��
    }

    rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        int userId = sqlite3_column_int(stmt, 0);
        sqlite3_finalize(stmt);
        return userId;  // ���ز�ѯ�����û�ID
    } else {
        fprintf(stderr, "Failed to execute query: %s\n", sqlite3_errmsg(db));
        sqlite3_finalize(stmt);
        return -1;  // ���ظ�ֵ��ʾ��ѯʧ��
    }
}

// ��ȡ�û�����
int getUserPoints(int userId) {
    char sql[100];
    sprintf(sql, "SELECT points FROM users WHERE id = %d;", userId);

    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, 0);

    if (rc != SQLITE_OK) {
        fprintf(stderr, "Failed to prepare statement: %s\n", sqlite3_errmsg(db));
        return -1;  // ��ʾ��ȡ����ʧ��
    }

    rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        int userPoints = sqlite3_column_int(stmt, 0);
        sqlite3_finalize(stmt);  // �ͷ���Դ
        return userPoints;
    } else {
        fprintf(stderr, "Failed to execute query: %s\n", sqlite3_errmsg(db));
        sqlite3_finalize(stmt);  // �ͷ���Դ
        return -1;  // ��ʾ��ȡ����ʧ��
    }
}

// �۳�����
void deductPoints(int user_id, int points_to_deduct) {
    // ��ȡ��ǰ�û��Ļ���
    int current_points = getUserPoints(user_id);

    // �������Ƿ��㹻�۳�
    if (current_points >= points_to_deduct) {
        char sql[100];
        sprintf(sql, "UPDATE users SET points = points - %d WHERE id = %d;", points_to_deduct, user_id);

        int rc = sqlite3_exec(db, sql, 0, 0, 0);
        if (rc != SQLITE_OK) {
            fprintf(stderr, "Failed to deduct points: %s\n", sqlite3_errmsg(db));
        } else {
            printf("���ֿ۳��ɹ���\n");
        }
    } else {
            printf("�۳����ֲ��㡣\n");
    }
}

// �����û�����
int updateUserPoints(int userId, int newPoints) {
    char sql[100];
    sprintf(sql, "UPDATE users SET points = %d WHERE id = %d;", newPoints, userId);

    int rc = sqlite3_exec(db, sql, 0, 0, 0);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Failed to update user points: %s\n", sqlite3_errmsg(db));
        return 0;  // ��ʾ���»���ʧ��
    }

    return 1;  // ��ʾ���»��ֳɹ�
}

// ������ֳ�ֵ�߼�
void handlePointsRecharge(TCPsocket server, int userId, int points) {
    // ���ڹ���SQL��ѯ�����ַ���
    char sql[200];

    // ����������䣬��ָ���û��Ļ������Ӹ����ĵ���
    sprintf(sql, "UPDATE users SET points = points + %d WHERE id = %d;", points, userId);

    // ִ��SQL��䣬�����û�����
    int rc = sqlite3_exec(db, sql, 0, 0, 0);

    // ���SQLִ�н��
    if (rc != SQLITE_OK) {
        // ���ִ��ʧ�ܣ���ͻ��˷���ʧ����Ϣ
        fprintf(stderr, "Failed to recharge points: %s\n", sqlite3_errmsg(db));
        SDLNet_TCP_Send(server, "RECHARGE_POINTS_FAIL", sizeof("RECHARGE_POINTS_FAIL"));
        return;
    }

    // ��ѯ���µĻ���
    sprintf(sql, "SELECT points FROM users WHERE id = %d;", userId);
    sqlite3_stmt *stmt;

    // ׼��SQL��ѯ���
    rc = sqlite3_prepare_v2(db, sql, -1, &stmt, 0);

    // ���SQL׼�����
    if (rc != SQLITE_OK) {
        // ���׼��ʧ�ܣ���ͻ��˷���ʧ����Ϣ
        fprintf(stderr, "Failed to prepare statement: %s\n", sqlite3_errmsg(db));
        SDLNet_TCP_Send(server, "RECHARGE_POINTS_FAIL", sizeof("RECHARGE_POINTS_FAIL"));
        return;
    }

    // ִ��SQL��ѯ
    rc = sqlite3_step(stmt);

    // ���SQLִ�н��
    if (rc == SQLITE_ROW) {
        // �����ѯ���������ȡ�û����µĻ���
        int userPoints = sqlite3_column_int(stmt, 0);

        // �����ɹ���Ӧ��Ϣ���������µĻ�����Ϣ
        char response[512];
        sprintf(response, "RECHARGE_POINTS_SUCCESS,%d", userPoints);

        // ��ͻ��˷��ͳɹ���Ӧ��Ϣ
        SDLNet_TCP_Send(server, response, sizeof(response));
    } else {
        // ���ִ�в�ѯʧ�ܣ���ͻ��˷���ʧ����Ϣ
        fprintf(stderr, "Failed to execute query: %s\n", sqlite3_errmsg(db));
        SDLNet_TCP_Send(server, "RECHARGE_POINTS_FAIL", sizeof("RECHARGE_POINTS_FAIL"));
    }

    // �ͷ�SQL�����Դ
    sqlite3_finalize(stmt);
}

// ���뿨��Ϣ 
int insertCardInfo(int userId, const char *cardType, int duration, time_t purchaseTime) {
    time_t expirationTime = purchaseTime + duration;  // ���㵽��ʱ��

    char sql[200];
    sprintf(sql, "INSERT INTO cards (user_id, type, duration, purchase_time, expiration_time) VALUES (%d, '%s', %d, %ld, %ld);",
            userId, cardType, duration, purchaseTime, expirationTime);

    int rc = sqlite3_exec(db, sql, 0, 0, 0);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Failed to insert card information: %s\n", sqlite3_errmsg(db));
        return 0;  // ��ʾ���뿨��Ϣʧ��
    }

    return 1;  // ��ʾ���뿨��Ϣ�ɹ�
}

// ��ȡ�û�ӵ�еĿ�����
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

    // �ͷ�Ԥ���������Դ
    sqlite3_finalize(stmt);

    return cardType;
}

// �����߼�����
void handleCardPurchase(TCPsocket server, int userId, const char *cardType) {
    // ����û��Ƿ��Ѿ�ӵ���κ�һ�ֿ�
    if (hasAnyCard(userId)) {
        // ������Ӧ����Ϣ���ͻ��ˣ���ʾ�û��Ѿ��п���
        SDLNet_TCP_Send(server, "CARD_ALREADY_OWNED", sizeof("CARD_ALREADY_OWNED"));
        return;
    }


    char sql[200];
    time_t currentTime;
    time(&currentTime);

    // ���û���
    int cardCost;
    int duration;
    if (strcmp(cardType, "daily") == 0) {
        duration = 86400; //ʱ��
        cardCost = 10;    //����
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

    // ��ȡ�û�����
    int userPoints = getUserPoints(userId);

    // ��������㹻����
    if (userPoints >= cardCost) {
        // �۳��û�����
        if (updateUserPoints(userId, userPoints - cardCost)) {
            // ���뿨��Ϣ
            if (insertCardInfo(userId, cardType, duration, currentTime)) {
                printf("Card purchased successfully.\n");

                // �����û����ָ��ͻ���
                char response[512];
                sprintf(response, "BUY_CARD_SUCCESS,%d", userPoints - cardCost);
                SDLNet_TCP_Send(server, response, sizeof(response));
                return;  // ����ִ�гɹ����˳�
            } else {
                fprintf(stderr, "Card purchase failed: Unable to insert card information.\n");
            }
        } else {
            fprintf(stderr, "Failed to update user points: %s\n", sqlite3_errmsg(db));
        }
    } else {
        // ���ֲ���
        SDLNet_TCP_Send(server, "BUY_CARD_INSUFFICIENT_POINTS", sizeof("BUY_CARD_INSUFFICIENT_POINTS"));
    }

    // ���͹���ʧ����Ϣ���ͻ���
    SDLNet_TCP_Send(server, "BUY_CARD_FAIL", sizeof("BUY_CARD_FAIL"));

    return;  // ����ִ�гɹ����˳�
}

// �����¼�߼�
void handleCardPurchaseLogin(TCPsocket server, const char *username, const char *password, const char *cardType) {

    User currentUser;
    
    // �����û���¼��֤
    if (loginUser(username, password, &currentUser)) {
       // printf("Login successful. Welcome, %s!\n", currentUser.username);

        // �����û��ѵ�¼��־
        userLoggedIn = true;
        // �����û���Ϣ ��ȡ�û���ʶID
        loggedInUserId = currentUser.id;
        strcpy(loggedInUsername, currentUser.username);
       //printf("User loggedInUsername: %s\n", loggedInUsername);
        // ��ȡ�û����� ����
         userPoints = getUserPoints(loggedInUserId);
       
       // printf("User points: %d\n", userPoints);
     
         handleCardPurchase(server,loggedInUserId, cardType);
    } else {
        // ��¼ʧ��
        SDLNet_TCP_Send(server, "LOGIN_FAIL", sizeof("LOGIN_FAIL"));
    }

}

// ��ȡ��ʣ��ʱ��
int getCardRemainingDuration(int user_id, const char *card_type) {
    // ʹ�ò�������ѯ���� SQL ���
    const char *sql = "SELECT expiration_time FROM cards WHERE user_id=? AND type=? AND expiration_time > ?;";
    // ��ȡ��ǰʱ��
    time_t current_time;
    time(&current_time);

    // ���� SQL ���
    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, 0);

    if (rc != SQLITE_OK) {
        fprintf(stderr, "Failed to fetch card details: %s\n", sqlite3_errmsg(db));
        return 0; // ��ѯʧ�ܣ�����ʣ��ʱ��Ϊ0
    }

    // �󶨲���
    sqlite3_bind_int(stmt, 1, user_id);
    sqlite3_bind_text(stmt, 2, card_type, -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 3, current_time);

    // ִ�� SQL ���
    rc = sqlite3_step(stmt);

    if (rc == SQLITE_ROW) {
        // ��ȡ����ʱ��
        time_t expiration_time = sqlite3_column_int(stmt, 0);

    struct tm *purchase_tm = localtime(&expiration_time);

    // ��ӡ����ʱ�̵����ں�ʱ��
    printf("�˺ŵ���ʱ�䣺%d-%d-%d-%d-%d-%d  ",
           purchase_tm->tm_year + 1900, purchase_tm->tm_mon + 1, purchase_tm->tm_mday,
           purchase_tm->tm_hour, purchase_tm->tm_min, purchase_tm->tm_sec);

        // ����ʣ��ʱ��
        int remaining_duration = expiration_time - current_time;

        // ��ӡ����ʱ�̵����ں�ʱ��
        printf("ʣ��ʱ�䣺%d ��\n",remaining_duration);

        // �ͷ�Ԥ���������Դ
        sqlite3_finalize(stmt);

        return remaining_duration > 0 ? remaining_duration : 0;
    } else {
        // δ�ҵ���Ч�Ŀ�������ʣ��ʱ��Ϊ0
        sqlite3_finalize(stmt);
        return 0;
    }
}
// ʹ��ʱ��
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
                printf("����ʱ�䣺%d ��\n", usage_time);
            }
        } else {
           // printf("Card usage time expired. Deducting points...\n");
            deductPoints(user_id, 10); // ����ÿ��ʹ�ó�������ʱ�����۳�10��
        }
    } else {
        printf("δ�ҵ��û� %d ������ %s ��ʹ��ʱ�� %d\n", user_id, card_type);
    }
}
// ����û��Ƿ�����Ч�Ŀ�
int hasValidCard(int user_id, const char *card_type) {
    char sql[200];
    time_t current_time;
    time(&current_time);

    sprintf(sql, "SELECT * FROM cards WHERE user_id=%d AND type='%s' AND expiration_time > %ld;", user_id, card_type, current_time);

    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, 0);

    if (rc != SQLITE_OK) {
        fprintf(stderr, "Failed to fetch card details: %s\n", sqlite3_errmsg(db));
        return 0; // ��ѯʧ�ܣ�������Ч��
    }

    rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        // ������Ч�Ŀ�
        sqlite3_finalize(stmt);
        return 1;
    } else {
        // ��������Ч�Ŀ�
        sqlite3_finalize(stmt);
        return 0;
    }
}

// ����ʹ��ʱ���߼�
void handleCardUsage(TCPsocket server) {
    char buffer[512];
    if (SDLNet_TCP_Recv(server, buffer, sizeof(buffer)) > 0) {
        // ����ʹ����Ϣ
        char *token = strtok(buffer, "|");
        int user_id = atoi(token);

        token = strtok(NULL, "|");
        const char *card_type = token;

        token = strtok(NULL, "|");
        int usage_time = atoi(token);

        // ģ��ʹ��ʱ��
        simulateUsage(user_id, card_type, usage_time);

        // ����ʹ�ý�����ͻ��ˣ����Ը��ݾ���������ز�ͬ����Ϣ��
        SDLNet_TCP_Send(server, "LOGIN_SUCCESS", sizeof("LOGIN_SUCCESS"));
    }
}

// ��ͨ�ú���
int useCard(TCPsocket server, int user_id, const char *cardType) {

    // �����û�����Ч�Ŀ�
    if (hasValidCard(user_id, cardType)) {
        // ��ȡ����ʣ��ʱ��

            if (isUserOnline(loggedInUsername)) {
        // �û��Ѿ����ߣ�������Ӧ�߼�
        printf("�û� %s �Ѿ����ߣ��������ظ���¼��\n", loggedInUsername);

        SDLNet_TCP_Send(server, "LOGIN_ALREADY", sizeof("LOGIN_ALREADY"));
    } else {

        
        // ����û������߿ͻ����б�
        addOnlineClient(clientSocket, onlineClientsCount, loggedInUsername);
        int remaining_duration = getCardRemainingDuration(user_id, cardType);

        // ģ��ʹ��ʱ��
        simulateUsage(user_id, cardType, remaining_duration);

        // ������������������߼������緢��ʹ�óɹ�����Ϣ���ͻ���
      //  SDLNet_TCP_Send(server, "LOGIN_SUCCESS", sizeof("LOGIN_SUCCESS"));

        // ��������һ��������ɵ�����
        const char* randomToken = "LOGIN_SUCCESS";//generateRandomToken();

        // ���ɴ�������ǩ��������
        char* signedToken = generateSignedToken(randomToken);
        printf("�����������˴�������ǩ��������: %s\n", signedToken);

        // �������Ƶ��ͻ���
        SDLNet_TCP_Send(server, signedToken, strlen(signedToken));
        // �������Ƶ��ͻ���
        SDLNet_TCP_Send(server, randomToken, strlen(randomToken));

        time_t currentTime;
        time(&currentTime);
        //time_t currentTime = time(NULL);//��ȡ��ǰ����ʱ��
        ConnectedClient* connectedClient = (ConnectedClient*)malloc(sizeof(ConnectedClient));
        connectedClient->socket = clientSocket;                 //�ͻ���
        connectedClient->userId = loggedInUserId;               //�û���ʶ
        strcpy(connectedClient->username, loggedInUsername);    //�û���
        connectedClient->userLoggedIn =userLoggedIn;            //����״̬ 1���� 0������
        connectedClient->lastHeartbeat= currentTime;            //�û�����ʱ��



        SDL_Thread* clientThread = SDL_CreateThread((SDL_ThreadFunction)handleClient, "ClientThread", (void*)connectedClient);
    }
    } else {
        // �û�û����Ч�Ŀ������Է�����Ӧ����Ϣ���ͻ���
        SDLNet_TCP_Send(server, "CARD_USAGE_FAIL", sizeof("CARD_USAGE_FAIL"));
    }
    return 0;
}

// ����û����Ƿ��Ѵ���
bool isUsernameTaken(const char *username) {
    char sql[100];
    sprintf(sql, "SELECT COUNT(*) FROM users WHERE username='%s';", username);

    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, 0);

    if (rc != SQLITE_OK) {
        fprintf(stderr, "Failed to prepare statement: %s\n", sqlite3_errmsg(db));
        return true;  // ����������ʱ�������û����ѱ�ռ��
    }

    rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        int count = sqlite3_column_int(stmt, 0);
        return count > 0;  // ��� count ���� 0��˵���û����ѱ�ռ��
    } else {
        fprintf(stderr, "Failed to execute query: %s\n", sqlite3_errmsg(db));
        return true;  // ����������ʱ�������û����ѱ�ռ��
    }
}


// ����û����Ƿ������Сд��ĸ
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

// �����û�ע���߼�
void handleRegistration(TCPsocket server, const char *username, const char *password) {
    // ����û����Ƿ��ѱ�ռ��
    if (isUsernameTaken(username)) {
        // ��ͻ��˷���ע��ʧ����Ϣ���û����ѱ�ռ�ã�
        SDLNet_TCP_Send(server, "REGISTER_FAILZY", sizeof("REGISTER_FAILZY"));
    } else if (!isUsernameValid(password) || strlen(username) < 6) {
        // ����û����������Ƿ����Ҫ��
        // ��ͻ��˷���ע��ʧ����Ϣ
        SDLNet_TCP_Send(server, "REGISTER_FAILYHM", sizeof("REGISTER_FAILYHM"));
    } else {
        // ���������û����ݵ�SQL���
        char sql[100];
        sprintf(sql, "INSERT INTO users (username, password, points) VALUES ('%s', '%s', 0);", username, password);

        // ִ��SQL���������û�����
        int rc = sqlite3_exec(db, sql, 0, 0, 0);

        // ���SQLִ�н��
        if (rc != SQLITE_OK) {
            // ���ִ��ʧ�ܣ���ͻ��˷���ע��ʧ����Ϣ
            fprintf(stderr, "Registration failed: %s\n", sqlite3_errmsg(db));
            SDLNet_TCP_Send(server, "REGISTER_FAIL", sizeof("REGISTER_FAIL"));
        } else {
            // ע��ɹ�����ͻ��˷���ע��ɹ���Ϣ
            printf("Registration successful.\n");
            SDLNet_TCP_Send(server, "REGISTER_SUCCESS", sizeof("REGISTER_SUCCESS"));
        }
    }
}


// �û���¼��֤
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

        // �����¼�ɹ���Ϣ����������
       // printf("Login successful. Welcome, %s!\n", user->username);
        
        return true;
    } else {
        // �����¼ʧ����Ϣ����������
       // printf("Login failed. Invalid username or password.\n");
        return false;
    }
}
// �����¼�߼�
void handleLogin(TCPsocket server, const char *username, const char *password) {

    User currentUser;
    
    // �����û���¼��֤
    if (loginUser(username, password, &currentUser)) {
       // printf("Login successful. Welcome, %s!\n", currentUser.username);

        // �����û��ѵ�¼��־
        userLoggedIn = true;

        // �����û���Ϣ ��ȡ�û���ʶID
        loggedInUserId = currentUser.id;

        strcpy(loggedInUsername, currentUser.username);

       //printf("User loggedInUsername: %s\n", loggedInUsername);


        // ��ȡ�û����� ����
         userPoints = getUserPoints(loggedInUserId);
        // ������Ϊ0����¼�ɹ�
       // printf("User points: %d\n", userPoints);
 if(userPoints >= 0){

    const char *cardType = hasAnyCard(loggedInUserId);
   // printf("User cardType: %s\n", cardType);
if (cardType != NULL) {
    if (strcmp(cardType, "daily") == 0) {
        // ʹ��һ�쿨
        useCard(server, loggedInUserId, "daily");
    } else if (strcmp(cardType, "weekly") == 0) {
        // ʹ���ܿ�
        useCard(server, loggedInUserId, "weekly");
    } else if (strcmp(cardType, "monthly") == 0) {
        // ʹ���¿�
        useCard(server, loggedInUserId, "monthly");
    } else if (strcmp(cardType, "seasonal") == 0) {
        // ʹ�ü���
        useCard(server, loggedInUserId, "seasonal");
    } else if (strcmp(cardType, "yearly") == 0) {
        // ʹ���꿨
        useCard(server, loggedInUserId, "yearly");} 
    
     }

    
    }else{ // ����Ϊ0 
    
    SDLNet_TCP_Send(server, "LOGIN_FAIL_POINTS_ZERO", sizeof("LOGIN_FAIL_POINTS_ZERO"));}
       
    }else {// ��¼ʧ��
    SDLNet_TCP_Send(server, "LOGIN_FAIL", sizeof("LOGIN_FAIL"));}
}

// ����������������ͻ�������
void serverListen() {

    printf("�����������ڶ˿� %d...\n", 8989);  // ���Ŷ˿�
    
    while (1) {
 
    // ��ʼ�� Socket Set
    socketSet = SDLNet_AllocSocketSet(MAX_CLIENTS);

    // �洢�ͻ��˵� TCP sockets
    TCPsocket clientSockets[MAX_CLIENTS];

    int numClients = 0;

    // ���ܿͻ�������
              clientSocket = SDLNet_TCP_Accept(serverserver);

        if (clientSocket) {
            //printf("clientSocket connected.\n");


            char buffer[512];
            if (SDLNet_TCP_Recv(clientSocket, buffer, sizeof(buffer)) > 0) {
                // �����ﴦ��ͻ��˷��͵���Ϣ��������Ҫ���������������
                // ���ӣ�������Ϣ����ִ����Ӧ����
                if (strcmp(buffer, "REGISTER") == 0) {
                    // ����ע���߼�
                    char username[50];
                    char password[50];
                    SDLNet_TCP_Recv(clientSocket, username, sizeof(username));
                    SDLNet_TCP_Recv(clientSocket, password, sizeof(password));
                    handleRegistration(clientSocket, username, password);
                
                } else if (strcmp(buffer, "LOGIN") == 0) {
                    // �����¼�߼�
                    char username[50];
                    char password[50];
                    SDLNet_TCP_Recv(clientSocket, username, sizeof(username));
                    SDLNet_TCP_Recv(clientSocket, password, sizeof(password));
                    handleLogin(clientSocket, username, password);

                } else if (strcmp(buffer, "BUY_CARD") == 0) {
                   
                    // �û�ID
                    int userId;
                    // �����ͣ���󳤶�Ϊ10
                    char cardType[256];
                    // ����ʹ��ʱ��
                    int duration;

                    // �������߼�
                    char username[50];
                    char password[50];
                    SDLNet_TCP_Recv(clientSocket, username, sizeof(username));
                    SDLNet_TCP_Recv(clientSocket, password, sizeof(password));
                    SDLNet_TCP_Recv(clientSocket, cardType, sizeof(cardType));
                   // SDLNet_TCP_Recv(clientSocket, &duration, sizeof(duration)); //Զ��ִ���ڴ��޸���ֵ©��
                    handleCardPurchaseLogin(clientSocket, username, password, cardType);
                } else if (strcmp(buffer, "RECHARGE_POINTS") == 0) {
                            // �û�ID
                            int userId;
                            // ��ֵ�Ļ���
                            int rechargePoints;

                            // ������ֳ�ֵ�߼�
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

    // ��ʼ��SDL2_net
    SDLNet_Init();

    // ���÷�����IP�Ͷ˿�
    IPaddress ip;
    SDLNet_ResolveHost(&ip, NULL, 8989);
    // �򿪷������׽���
    serverserver = SDLNet_TCP_Open(&ip);
    if (!serverserver) {
        fprintf(stderr, "SDLNet_TCP_Open: %s\n", SDLNet_GetError());
        return 1;
    }

    // �������߳����з�������������
    SDL_Thread *serverThread = SDL_CreateThread((SDL_ThreadFunction)serverListen, "ServerThread", NULL);

    // �ȴ��������߳̽���
    int threadReturnValue;
    SDL_WaitThread(serverThread, &threadReturnValue);

    // ��������Դ���������߳̽�������߼�
    printf("Server thread ended with return value: %d\n", threadReturnValue);


    // �ر�SDL2_net
    SDLNet_Quit();

    // �ر����ݿ�����
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

    // ������ǩ��ת��Ϊʮ�������ַ���
    char hexDigest[EVP_MAX_MD_SIZE * 2 + 1];
    for (unsigned int i = 0; i < digestLength; i++) {
        snprintf(hexDigest + (i * 2), sizeof(hexDigest) - (i * 2), "%02x", digest[i]);
    }

    // �ϲ����ƺ�ǩ��
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

    token[TOKEN_LENGTH] = '\0';  // ����ַ���������

    return token;
}
