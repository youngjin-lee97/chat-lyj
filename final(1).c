#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <pthread.h>

// mysql 연결
#include <mysql/mysql.h>
#define DB_HOST "10.10.21.103"
#define DB_USER "user1"
#define DB_PASS "1234"
#define DB_NAME "chatdb"
// gcc server.c -o server -lmysqlclient

#define BUF_SIZE 1024
#define MAX_CLNT 256
#define MAX_ID_LEN 20
#define MAX_PRIVATE_CHAT 100
#define MAX_PRIVATE_CHAT_MEMBER 10

void *handle_clnt(void *arg);
void error_handling(char *msg);

// 클라이언트 카운트
int clnt_cnt = 0;
// 방 개설시 방 갯수
int room_cnt = 0;

// 클라이언트 소켓 저장
int clnt_socks[MAX_CLNT];
// 클라이언트 아이디 저장
char clnt_ids[MAX_CLNT][MAX_ID_LEN];
// 개인 방 정보(인원, 참여인원 소켓)
// [채팅방index-개설된 순서대로0부터 증가][n]
// [n][0]참여인원 수(방장포함), [n][1]방장소켓번호, [n][2]그 다음 참여자 소캣....
int private_chat_sock[MAX_PRIVATE_CHAT][MAX_PRIVATE_CHAT_MEMBER];

// 클라이언트 소통용 구조체
typedef struct {
    char page_id[20];         // 페이지 구분
    char id[10];              // 사용자 ID
    char pwd[10];             // 비밀번호
    char change[10];          // 새 비밀번호 (비밀번호 변경용)
    char nickname[10];        // 닉네임
    char text[300];           // 채팅 내용
    char time[20];            // 채팅 시간
    char whisper[2];          // 귓속말 여부
    char whisper_target[20];  // 귓속말 보낼 상대
    char chat_alarm[2];       // 웰컴메세지 용
    char private_room[2];
} ClntInfo;

// typedef struct {
//     char host_id[10];
//     int host_socket;
//     int members;
// } PrivateChat;

// 클라이언트 전송용 구조체
typedef struct {
    char type[16];        // 메시지 종류
    char data[BUF_SIZE];  // 메시지 내용
} MsgPacket;

pthread_mutex_t mutx;

int main(int argc, char *argv[])
{
    int serv_sock;
    int clnt_sock;
    struct sockaddr_in serv_adr;
    struct sockaddr_in clnt_adr;
    socklen_t clnt_adr_sz;
    pthread_t t_id;

    if (argc != 2)
    {
        printf("Usage : %s <port>\n", argv[0]);
        exit(1);
    }

    serv_sock = socket(PF_INET, SOCK_STREAM, 0);
    if (serv_sock == -1)
    {
        error_handling("socket error");
    }

    memset(&serv_adr, 0, sizeof(serv_adr));
    serv_adr.sin_family = AF_INET;
    serv_adr.sin_addr.s_addr = htonl(INADDR_ANY);
    serv_adr.sin_port = htons(atoi(argv[1]));

    if (bind(serv_sock, (struct sockaddr*)&serv_adr, sizeof(serv_adr)) == -1)
    {
        error_handling("bind error");
    }
    
    if (listen(serv_sock, 5) == -1)
    {
        printf("listening error");
    }

    pthread_mutex_init(&mutx, NULL);


    while (1)
    {
        clnt_adr_sz = sizeof(clnt_adr);
        clnt_sock = accept(serv_sock, (struct sockaddr*)&clnt_adr, &clnt_adr_sz);
        if (clnt_sock == -1)
        {
            error_handling("accept error");
        }
    
        pthread_mutex_lock(&mutx);

        clnt_socks[clnt_cnt] = clnt_sock;
        printf("client count1 : %d\n", clnt_cnt);
        clnt_cnt++;

        pthread_mutex_unlock(&mutx);

        pthread_create(&t_id, NULL, handle_clnt, (void*)&clnt_sock);
        pthread_detach(t_id);
        printf("Connected client IP : %s \n", inet_ntoa(clnt_adr.sin_addr));
    
    }
    
    close(serv_sock);
    return 0;
}

void manage_users(ClntInfo info, int clnt_sock, MYSQL *conn);

void *handle_clnt(void *arg)
{
    int clnt_sock = *((int*)arg);

    // 디비 연결
    MYSQL *conn = mysql_init(NULL);
    if (!mysql_real_connect(conn, DB_HOST, DB_USER, DB_PASS, DB_NAME, 0, NULL, 0))
    {
        fprintf(stderr, "DB 연결 실패: %s\n", mysql_error(conn));
        mysql_close(conn);
        close(clnt_sock);
        return NULL;
    }

    ClntInfo clnt_info;

    while (1)
    {
        read(clnt_sock, &clnt_info, sizeof(clnt_info));

        // 종료 입력시 == 클라이언트 아웃
        if (strcmp(clnt_info.page_id, "exit") == 0)
        {
            break;
        }
        
        manage_users(clnt_info, clnt_sock, conn);
    }
    
    pthread_mutex_lock(&mutx);
    for (int i = 0; i < clnt_cnt; i++)
    {
        if (clnt_sock == clnt_socks[i])
        {
            while (i++ < clnt_cnt-1)
            {
                clnt_socks[i] = clnt_socks[i+1];
                strcpy(clnt_ids[i], clnt_ids[i + 1]);  // 아이디도 같이 지움
            }
            break;
        }
    }
    clnt_cnt--;
    pthread_mutex_unlock(&mutx);

    close(clnt_sock);
    mysql_close(conn); 
    return NULL;
}


void manage_users(ClntInfo clnt_info, int clnt_sock, MYSQL *conn)
{
    pthread_mutex_lock(&mutx);
    
    MsgPacket msg_packet;

    // 메세지 잘 들어오는지
    // printf("[DEBUG] clnt_info.text: %s\n", clnt_info.text);
    
    // 회원가입 - 아이디 중복 체크
    if (strcmp(clnt_info.page_id, "join_id") == 0)
    {
        printf("join_id\n");
        strcpy(msg_packet.type, "join_id");
        
        char query[256];
        sprintf(query, "SELECT * FROM user_info_tbl WHERE user_id='%s';", clnt_info.id);
        if (mysql_query(conn, query))
        {
            fprintf(stderr, "Login query error: %s\n", mysql_error(conn));
            strcpy(msg_packet.data, "error");
        }
        else
        {
            MYSQL_RES *res = mysql_store_result(conn);
            
            if (mysql_num_rows(res) < 1)
            {
                strcpy(msg_packet.data, "success");
            }
            else
            {
                strcpy(msg_packet.data, "fail");
            }
            mysql_free_result(res);
        }
        write(clnt_sock, &msg_packet, sizeof(msg_packet));
    }
    // 회원가입
    else if (strcmp(clnt_info.page_id, "join") == 0)
    {
        printf("join\n");
        strcpy(msg_packet.type, "join");
        
        char query[256];
        sprintf(query, "INSERT INTO user_info_tbl(user_id, user_pwd) " "VALUES ('%s', '%s');", clnt_info.id, clnt_info.pwd);
        if (mysql_query(conn, query))
        {
            fprintf(stderr, "Join Error: %s\n", mysql_error(conn));
            strcpy(msg_packet.data, "error");
        }
        else
        {
            my_ulonglong affected = mysql_affected_rows(conn);

            if (affected > 0)
            {
                printf("회원가입 완\n");
                strcpy(msg_packet.data, "success");
            }
            else
            {
                printf("회원가입 실패\n");
                strcpy(msg_packet.data, "fail");
            }
        }
        write(clnt_sock, &msg_packet, sizeof(msg_packet));
    }
    // 로그인
    else if (strcmp(clnt_info.page_id, "login") == 0)
    {
        printf("login\n");
        strcpy(msg_packet.type, "login");

        char query[256];
        sprintf(query, "SELECT * FROM user_info_tbl WHERE user_id='%s' AND user_pwd='%s';", clnt_info.id, clnt_info.pwd);

        if (mysql_query(conn, query))
        {
            fprintf(stderr, "Login query error: %s\n", mysql_error(conn));
            strcpy(msg_packet.data, "error");
        }
        else
        {
            MYSQL_RES *res = mysql_store_result(conn);

            if (mysql_num_rows(res) > 0)
            {
                strcpy(msg_packet.data, "success");
            }
            else
            {
                strcpy(msg_packet.data, "fail");
            }
            mysql_free_result(res);
        }
        write(clnt_sock, &msg_packet, sizeof(msg_packet));
    }
    // 비밀번호 변경
    else if (strcmp(clnt_info.page_id, "pwd_change") == 0)
    {
        printf("pwd_change\n");
        strcpy(msg_packet.type, "pwd_change");

        char query[256];
        sprintf(query, "UPDATE user_info_tbl SET user_pwd='%s' WHERE user_id='%s';", clnt_info.change, clnt_info.id);
            
        if (mysql_query(conn, query))
        {
            fprintf(stderr, "Password change error: %s\n", mysql_error(conn));
            strcpy(msg_packet.data, "error");
        }
        else
        {
            my_ulonglong affected = mysql_affected_rows(conn);

            if (affected > 0)
            {
                printf("비밀번호 변경 완\n");
                strcpy(msg_packet.data, "success");
            }
            else
            {
                printf("비밀번호 변경 실패 또는 해당 ID가 존재하지 않습니다.\n");
                strcpy(msg_packet.data, "fail");
            }
        }
        write(clnt_sock, &msg_packet, sizeof(msg_packet));
    }
    // 채팅 전체
    else if (strcmp(clnt_info.page_id, "groupchat") == 0)
    {
        printf("groupchat\n");
        strcpy(msg_packet.type, "groupchat");
        
        // 채팅방 입장 시 clnt_ids에 채팅입장한 유저 id 들 모아놓기
        for (int i = 0; i < clnt_cnt; i++)
        {
            if (clnt_socks[i] == clnt_sock)
            {
                strncpy(clnt_ids[i], clnt_info.id, sizeof(clnt_ids[i]) - 1);
                clnt_ids[i][sizeof(clnt_ids[i]) - 1] = '\0';
                break;
            }
        }

        // 개인방 개설
        if (strncmp(clnt_info.text, "/m", 2) == 0)
        {
            room_cnt = room_cnt + 1;
            // 개설 메세지
            snprintf(msg_packet.data, sizeof(msg_packet.data), "[%s]의 대화방이 생성되었습니다.", clnt_info.id);
            write(clnt_sock, &msg_packet, sizeof(msg_packet));
            
            // [n][0]은 방 인원수
            private_chat_sock[room_cnt-1][0] = 1;
            // [n][1] 에 host socket 추가
            private_chat_sock[room_cnt-1][1] = clnt_sock;

            char query[500];
            sprintf(query, "INSERT INTO private_chat_tbl(host_user)" "VALUES ('%s');", clnt_info.id);
            if (mysql_query(conn, query))
            {
                fprintf(stderr, "Create Private Chat Error: %s\n", mysql_error(conn));
            }
        }
        // 개인방 참여
        else if (strncmp(clnt_info.text, "/j ", 3) == 0)
        {
            // target == host id
            char *target = strtok(clnt_info.text + 3, " ");
            // host socket
            int romm_host_socket = 0;

            // 방장의 소켓 찾기
            for (int i = 0; i < clnt_cnt; i++)
            {
                if (strcmp(target, clnt_ids[i]) == 0)
                {
                    romm_host_socket = clnt_socks[i];
                    break;
                }
            }

            int room_idx = -1;
            // private_chat_sock 에서 host 가 있는 방 idx 찾기
            for (int i = 0; i < room_cnt; i++)
            {
                // private_chat_sock[i][1] == host socket
                if (private_chat_sock[i][1] == romm_host_socket)
                {
                    // i 는 room index
                    room_idx = i;
                    break;
                }
                
            }
            // 유저 private_chat_sock 안에 인원 증가
            int num_user = private_chat_sock[room_idx][0];
            private_chat_sock[room_idx][0] = num_user + 1;
            
            // private_chat_sock 안에 유저 저장하기
            private_chat_sock[room_idx][num_user + 1] = clnt_sock;

            // 방 인원에게 메시지 보내기
            for (int i = 0; i < private_chat_sock[room_idx][0]; i++)
            {
                // 소켓 번호 찾기
                for (int j = 0; j < clnt_cnt; j++)
                {
                    // user는 idx 1번부터 있음
                    int user_sock = private_chat_sock[room_idx][i+1];

                    if (user_sock == clnt_socks[j])
                    {
                        snprintf(msg_packet.data, sizeof(msg_packet.data), "[%s]님이 참여하셨습니다.", clnt_info.id);
                        write(clnt_socks[j], &msg_packet, sizeof(msg_packet));
                    }
                }
            }
        }
        // 방 목록보기
        else if (strncmp(clnt_info.text, "/r", 2) == 0)
        {
            // 개설된 방이 없을 시
            if (room_cnt == 0)
            {
                strcpy(msg_packet.data, "개설된 방이 없습니다.");
                write(clnt_sock, &msg_packet, sizeof(msg_packet));
            }
            // 방 있을 시
            else
            {
                /**
                 * 나중에는 디비로 끌어오기 >> is_using 판별해서
                 */

                // 호스트 아이디 모음
                char str[BUF_SIZE] = "host id list : ";
                // 개설된 방의 개설자 소캣
                for (int i = 0; i < room_cnt; i++)
                {
                    // 소캣으로 아이디 찾기
                    for (int j = 0; j < clnt_cnt; j++)
                    {
                        if (private_chat_sock[i][1] == clnt_socks[j])
                        {
                            strcat(str, clnt_ids[j]);
                            strcat(str, " ");
                            break;
                        }
                    }
                }
                strcpy(msg_packet.data, str);
                write(clnt_sock, &msg_packet, sizeof(msg_packet));
            }
        }
        // 귓속말
        else if (strncmp(clnt_info.text, "/w ", 3) == 0)
        {
            char *target = strtok(clnt_info.text + 3, " ");
            char *message = strtok(NULL, "");
            
            if (target && message)
            {
                // 디비 저장 위함
                strcpy(clnt_info.whisper, "Y");
                strcpy(clnt_info.whisper_target, target);
                snprintf(msg_packet.data, sizeof(msg_packet.data), "[%s][%s가 %s에게..] %s", clnt_info.time, clnt_info.id, target, message);
            }

            int target_search = 0;
            for (int i = 0; i < clnt_cnt; i++)
            {
                if (strcmp(clnt_ids[i], clnt_info.whisper_target) == 0)
                {
                    write(clnt_socks[i], &msg_packet, sizeof(msg_packet)); // 대상자에게 전송
                    target_search = 1;
                    break;
                }
            }

            if (!target_search)
            {
                strcpy(msg_packet.data, "해당 아이디가 존재하지 않습니다.");
            }
            else
            {
                char query[500];
                sprintf(query, "INSERT INTO group_chat_tbl(from_user, msg, sent_at, whisper, whisper_target)" "VALUES ('%s', '%s', '%s', '%s', '%s');", clnt_info.id, clnt_info.text, clnt_info.time, clnt_info.whisper, clnt_info.whisper_target);
                if (mysql_query(conn, query))
                {
                    fprintf(stderr, "Whisper Chat Log Save Error: %s\n", mysql_error(conn));
                }
            }
            
            write(clnt_sock, &msg_packet, sizeof(msg_packet));
        }
        // 접속자 id 리스트
        else if (strncmp(clnt_info.text, "/l", 2) == 0)
        {
            char str[BUF_SIZE] = "id list : ";
            for (int i = 0; i < clnt_cnt; i++)
            {
                // 채팅방 인원 참여자만 있을 시
                if (clnt_ids[i][0] == '\0')
                {
                    break;
                }
                // 여러명 있을 시
                else
                {
                    // 참여인원 아이디 모음
                    strcat(str, clnt_ids[i]);
                    strcat(str, " ");
                }
            }
            strcpy(msg_packet.data, str);
            write(clnt_sock, &msg_packet, sizeof(msg_packet));
        }
        // 채팅방 탈출
        else if (strncmp(clnt_info.text, "/exit", 5) == 0)
        {
            // 소수채팅방에서 나갈 때
            // 채팅방 유저인지 판별하기 위함
            int in_groupchat = 0;
            // 룸 갯수
            for (int i = 0; i < room_cnt; i++)
            {
                for (int j = 0; j < private_chat_sock[i][0]; j++)
                {
                    if (clnt_sock == private_chat_sock[i][j + 1])
                    {
                        // 지우기: j+1 위치부터 앞으로 한 칸씩 이동
                        for (int k = j + 1; k < private_chat_sock[i][0]; k++)
                        {
                            private_chat_sock[i][k] = private_chat_sock[i][k + 1];
                        }
                        // 유저 수 감소
                        private_chat_sock[i][0]--;

                        in_groupchat = 1;
                        break;
                    }
                }
            }

            // clnt_ids 안의 유저 아이디 지워야 함
            // 단체방에서 나갈 시
            if (in_groupchat == 0)
            {
                for (int i = 0; i < clnt_cnt; i++)
                {
                    if (strcmp(clnt_info.id, clnt_ids[i]) == 0)
                    {
                        clnt_ids[i][0] = '\0';
                    }
                }
            }
            
        }
        // 일반 채팅
        else
        {
            // welcome msg
            if (strcmp(clnt_info.chat_alarm, "W") == 0)
            {
                snprintf(clnt_info.text, sizeof(clnt_info.text), "[%s]님이 [%s]으로 참가하셨습니다.", clnt_info.id, clnt_info.nickname);
                strcpy(msg_packet.data, clnt_info.text);
            }
            else
            {
                snprintf(msg_packet.data, sizeof(msg_packet.data), "[%s][%s] %s", clnt_info.time, clnt_info.nickname, clnt_info.text);
            }
                        
            // 채팅방 인원인지 찾기
            int room_idx = -1; // 채팅방 인덱스
            for (int i = 0; i < room_cnt; i++)
            {
                // private_chat_sock[i][0] 는 방 인원수
                int num = private_chat_sock[i][0];
                for (int j = 0; j < num; j++)
                {
                    if (private_chat_sock[i][j+1] == clnt_sock)
                    {
                        room_idx = i;
                        break;
                    }
                }
            }
            
            // 클라이언트 전송
            for (int i = 0; i < clnt_cnt; i++)
            {
                // 룸
                if (room_idx != -1)
                {
                    int num = private_chat_sock[room_idx][0]; // 채팅방 인원

                    for (int j = 0; j < private_chat_sock[room_idx][0]; j++)
                    {
                        // user는 idx 1번부터 있음
                        int user_sock = private_chat_sock[room_idx][j+1];

                        // 방에 있는 인원에게 메세지 전송
                        if (user_sock == clnt_socks[i])
                        {
                            write(clnt_socks[i], &msg_packet, sizeof(msg_packet));
                        }                        
                    }
                    if (private_chat_sock[room_idx][1] == clnt_socks[i])
                    {
                        char host_id[20];
                        strcpy(host_id, clnt_ids[i]);

                        
                        char query[500];
                        sprintf(query, "INSERT INTO private_chat_log_tbl(from_user, msg, sent_at, host_user)" "VALUES ('%s', '%s', '%s', '%s');", clnt_info.id, clnt_info.text, clnt_info.time, host_id);
                        if (mysql_query(conn, query))
                        {
                            fprintf(stderr, "Private Chat Log SAVE Error: %s\n", mysql_error(conn));
                        }
                    }
                }
                // 단체방
                else
                {
                    // 채팅방 유저인지 판별하기 위함
                    int in_groupchat = 0;
                    // 룸 갯수
                    for (int j = 0; j < room_cnt; j++)
                    {
                        // 룸 내 소켓
                        for (int k = 0; k < private_chat_sock[j][0]; k++)
                        {
                            if (clnt_socks[i] == private_chat_sock[j][k+1])
                            {
                                in_groupchat = 1;
                                break;
                            }
                        }
                        if (in_groupchat == 1)
                        {
                            break;
                        }
                    }
                    // 단체방에 있는 유저
                    if (in_groupchat != 1 && clnt_ids[i][0] != '\0')
                    {
                        write(clnt_socks[i], &msg_packet, sizeof(msg_packet));

                        // 보낸사람 일 때만 쿼리문 보내게,, 안그러면 인원수만큼 쿼리실행됨
                        if (clnt_socks[i] == clnt_sock)
                        {
                            char query[500];
                            sprintf(query, "INSERT INTO group_chat_tbl(from_user, msg, sent_at)" "VALUES ('%s', '%s', '%s');", clnt_info.id, clnt_info.text, clnt_info.time);
                            if (mysql_query(conn, query))
                            {
                                fprintf(stderr, "Chat Log Save Error: %s\n", mysql_error(conn));
                            }
                        }
                        
                    }

                }
                
            }
        }
            
        
    }

    pthread_mutex_unlock(&mutx);
    return;
}

void error_handling(char *msg)
{
    fputs(msg, stderr);
    fputc('\n', stderr);
    exit(1);
}