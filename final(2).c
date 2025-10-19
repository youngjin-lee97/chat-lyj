#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <string.h>
#include <time.h>
#include <ctype.h>

#define BUF_SIZE 1024
#define NAME_SIZE 20
#define BUF_SIZE 1024

typedef struct {
    char type[16];        // 메시지 종류
    char data[BUF_SIZE];  // 메시지 내용
} MsgPacket;

MsgPacket shared_msg;
int msg_updated = 0;
pthread_mutex_t shared_mutex = PTHREAD_MUTEX_INITIALIZER;

char msg[BUF_SIZE];
ssize_t str_len;

void *send_msg(void *arg);
void *recv_msg(void *arg);
void error_handling(char *msg);
int check_and_handle_msg();

typedef struct {
    char page_id[20];         // 요청 구분: "login", "join", "pwd_change", "groupchat" 등
    char id[10];              // 사용자 ID
    char pwd[10];             // 비밀번호 (회원가입, 로그인용)
    char change[10];          // 새 비밀번호 (비밀번호 변경용)
    char nickname[10];        // 채팅용 닉네임
    char text[300];           // 채팅 내용
    char time[20];            // 채팅 시간
    char whisper[2];          // 귓속말 여부
    char whisper_target[20];  // 귓속말 보낼 상대
    char chat_alarm[2];
    char private_room[2];
} ClntInfo;

int main(int argc, char *argv[])
{
    int sock;
    struct sockaddr_in serv_addr;
    pthread_t snd_thread;
    pthread_t rcv_thread;
    void *thread_return;

    if (argc != 3)
    {
        printf("Usage : %s <IP> <port>\n", argv[0]);
        exit(1);
    }
    
    sock = socket(PF_INET, SOCK_STREAM, 0);
    if (sock == -1)
    {
        error_handling("socket error");
    }

    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = inet_addr(argv[1]);
    serv_addr.sin_port = htons(atoi(argv[2]));

    if (connect(sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) == -1)
    {
        error_handling("connecting error");
    }
    
    pthread_create(&snd_thread, NULL, send_msg, (void*)&sock);
    pthread_create(&rcv_thread, NULL, recv_msg, (void*)&sock);
    pthread_join(snd_thread, &thread_return);
    pthread_join(rcv_thread, &thread_return);
    close(sock);
    return 0;
}

void clear_client_screen() {
    system("clear"); 
}

void *send_msg(void *arg)
{
    int sock = *((int*)arg);
    ClntInfo clnt_info;
    char id_buf[10]; // 로그인 후 유지용
    int login_flag = 0; // 로그인 플레그

    while (1)
    {
        memset(&clnt_info, 0, sizeof(clnt_info));
        // 비 로그인 시
        if (login_flag == 0)
        {  
            clear_client_screen();
            printf("1.로그인 2.회원가입 3.종료 ");
            int page;
            scanf("%d", &page);

            if (page == 3)
            {
                strcpy(clnt_info.page_id, "exit");
                write(sock, &clnt_info, sizeof(clnt_info));
                close(sock);
                exit(0);
            }

            // 회원가입
            if (page == 2)
            {
                while (1)
                {
                    memset(&clnt_info, 0, sizeof(clnt_info));
                    strcpy(clnt_info.page_id, "join_id");

                    printf("id: ");
                    scanf("%s", clnt_info.id);
                    write(sock, &clnt_info, sizeof(clnt_info));

                    sleep(1);
                    if (check_and_handle_msg())
                    {
                        break;
                    }
                }

                strcpy(clnt_info.page_id, "join");
                printf("password: ");
                scanf("%s", clnt_info.pwd);
                write(sock, &clnt_info, sizeof(clnt_info));

                sleep(1);
                if (check_and_handle_msg()) // 회원가입 성공 시 화면 지우기
                {
                    sleep(1);
                    clear_client_screen();
                }
            }
            else if (page == 1) // 로그인
            {
                memset(&clnt_info, 0, sizeof(clnt_info));
                strcpy(clnt_info.page_id, "login");

                printf("id: ");
                scanf("%s", clnt_info.id);
                printf("password: ");
                scanf("%s", clnt_info.pwd);
                write(sock, &clnt_info, sizeof(clnt_info));

                sleep(1);
                if (check_and_handle_msg())
                {
                    strcpy(id_buf, clnt_info.id);  // 로그인한 id 저장
                    login_flag = 1;
                    // sleep(1);
                    // clear_client_screen(); // 로그인 성공시 화면 지우기
                }
            }            
        }
        // 로그인 상태
        else 
        {    
            clear_client_screen();
            printf("1.채팅 시작 2.비밀번호 변경 3.로그아웃 ");
            int page;
            scanf("%d", &page);

            if (page == 1) // 채팅
            {
                memset(&clnt_info, 0, sizeof(clnt_info));
                strcpy(clnt_info.page_id, "groupchat");
                strcpy(clnt_info.id, id_buf);
                clear_client_screen();
                printf("nickname: ");
                scanf("%s", clnt_info.nickname);
                getchar(); 

                clear_client_screen();


                printf("=======================================\n");
                printf("    방 개설하기       /m\n");
                printf("---------------------------------------\n");
                printf(" 개설한 방 들어가기   /j 방개설자id\n");
                printf("---------------------------------------\n");
                printf("    방 목록보기       /r\n");
                printf("---------------------------------------\n");
                printf("     귓속말하기       /w 귓속말상대id\n");
                printf("---------------------------------------\n");
                printf("   접속자 id 리스트   /l\n");   
                printf("---------------------------------------\n");
                printf("     채팅 종료        /exit\n");
                printf("=======================================\n");
                printf("\n\n");

                // 웰컴 메세지
                strcpy(clnt_info.chat_alarm, "W");
                // 현재 시간 구하기
                time_t now = time(NULL);
                struct tm *t = localtime(&now);
                strftime(clnt_info.time, sizeof(clnt_info.time), "%Y-%m-%d %H:%M:%S", t);
                write(sock, &clnt_info, sizeof(clnt_info));
                
                // 웰컴 외 메세지
                while (1)
                {
                    strcpy(clnt_info.chat_alarm, "N");
                    
                    fgets(clnt_info.text, sizeof(clnt_info.text), stdin);
                    clnt_info.text[strcspn(clnt_info.text, "\n")] = '\0';  // 개행 문자 제거

                    if (strcmp(clnt_info.text, "/m") == 0 || strncmp(clnt_info.text, "/j ", 3) == 0)
                    {
                        strcpy(clnt_info.private_room,"Y");
                    }
                    // 종료 명령
                    else if (strcmp(clnt_info.text, "/exit") == 0)
                    {
                        write(sock, &clnt_info, sizeof(clnt_info));

                        if (strcmp(clnt_info.private_room, "Y") == 0)
                        {
                            strcpy(clnt_info.private_room,"N");
                        }
                        else
                        {
                            break;
                        }
                    }

                    // 현재 시간 구하기
                    time_t now = time(NULL);
                    struct tm *t = localtime(&now);
                    strftime(clnt_info.time, sizeof(clnt_info.time), "%Y-%m-%d %H:%M:%S", t);

                    write(sock, &clnt_info, sizeof(clnt_info));
                }
            }
            else if (page == 2) // 비밀번호 변경
            {
                memset(&clnt_info, 0, sizeof(clnt_info));
                strcpy(clnt_info.page_id, "pwd_change");
                strcpy(clnt_info.id, id_buf);

                printf("new password: ");
                scanf("%s", clnt_info.change);

                write(sock, &clnt_info, sizeof(clnt_info));
                sleep(1);
                check_and_handle_msg();
            }
            else if (page == 3) // 로그아웃
            {
                login_flag = 0;
                memset(id_buf, 0, sizeof(id_buf));
                printf("로그아웃 되었습니다.\n");
            }
        }
    }
    return NULL;
}

void *recv_msg(void *arg)
{
    int sock = *((int *)arg);
    MsgPacket packet;

    while ((str_len = read(sock, &packet, sizeof(MsgPacket))) > 0)
    {
        // read(sock, &packet, sizeof(MsgPacket));
        pthread_mutex_lock(&shared_mutex);
        
        // 채팅일 시 바로 화면에 출력
        if (strcmp(packet.type, "groupchat") == 0)
        {
            printf("%s\n", packet.data);

        }
        // 채팅외는 판별하기 위해 전역으로 값 저장
        else
        {
            shared_msg = packet;       // 전역 구조체에 복사
            msg_updated = 1;           // 플래그 설정
        }
        pthread_mutex_unlock(&shared_mutex);
    }

    return NULL;
}

// 요청 성공/실패 판별
// 서버에서 가입, 로그인, 비밀번호 변경시에는 success, fail, error 로 통신
int check_and_handle_msg()
{
    int handled = 0;

    pthread_mutex_lock(&shared_mutex);

    if (msg_updated)
    {
        msg_updated = 0;

        // 아이디 중복 여부
        if (strcmp(shared_msg.type, "join_id") == 0)
        {
            if (strcmp(shared_msg.data, "success") == 0)
            {
                printf("사용가능한 아이디\n");
                handled = 1;
            }
            else if (strcmp(shared_msg.data, "fail") == 0)
            {
                printf("이미 사용중인 아이디입니다.\n");
                sleep(1); 
                clear_client_screen();  // 아이디 중복 시 화면 지우기
            }
            else if (strcmp(shared_msg.data, "error") == 0)
            {
                printf("db error\n");
            }
        }
        // 회원가입 성공 여부
        if (strcmp(shared_msg.type, "join") == 0)
        {
            if (strcmp(shared_msg.data, "success") == 0)
            {
                printf("회원가입 성공\n");
                handled = 1;
                sleep(1);
                clear_client_screen();
            }
            else if (strcmp(shared_msg.data, "fail") == 0)
            {
                printf("회원가입 실패\n");
                sleep(1); // 메시지를 볼 수 있도록 1초 대기
                clear_client_screen();  //  화면 지우기
            }
            else if (strcmp(shared_msg.data, "error") == 0)
            {
                printf("db error\n");
            }
        }
        // 로그인 성공 여부
        else if (strcmp(shared_msg.type, "login") == 0)
        {
            if (strcmp(shared_msg.data, "success") == 0)
            {   sleep(1);
                clear_client_screen();
                printf("로그인 성공\n");
                handled = 1;
            }
            else if (strcmp(shared_msg.data, "fail") == 0)
            {
                printf("다시 입력해주세요.\n");
                sleep(1); // 메시지를 볼 수 있도록 1초 대기
                clear_client_screen();  // 화면 지우기
            }
            else if (strcmp(shared_msg.data, "error") == 0)
            {
                printf("db error\n");
            }
        }
        // 비밀번호 변경 성공 여부
        else if (strcmp(shared_msg.type, "pwd_change") == 0)
        {
            if (strcmp(shared_msg.data, "success") == 0)
            {   sleep(1);
                clear_client_screen();
                printf("비밀번호 변경 성공\n");
                handled = 1;
            }
            else if (strcmp(shared_msg.data, "fail") == 0)
            {
                printf("다시 시도해주세요.\n");
                sleep(1); // 메시지를 볼 수 있도록 1초 대기
                clear_client_screen();  //화면 지우기
            }
            else if (strcmp(shared_msg.data, "error") == 0)
            {
                printf("db error\n");
            }
        }
        
    }

    pthread_mutex_unlock(&shared_mutex);

    return handled;
}

void error_handling(char *msg)
{
    fputs(msg, stderr);
    fputc('\n', stderr);
    exit(1);
}