#include "csapp.h"
#include <stdio.h>

/* 프록시 서버의 캐시 관련 상수 정의 */
#define MAX_CACHE_SIZE 1049000  /* 최대 캐시 크기 (약 1MB) */
#define MAX_OBJECT_SIZE 102400  /* 캐시 가능한 최대 객체 크기 (약 100KB) */

/* User-Agent 헤더 문자열 상수 */
static const char *user_agent_hdr = 
    "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 "
    "Firefox/10.0.3\r\n";

/* 함수 프로토타입 */
void handle_transaction(int fd);
void send_request(int server_fd, char *method, char *path, char *hostname);
void forward_response(int server_fd, int client_fd);
int parse_uri(char *uri, char *hostname, char *path, char *port);
void send_error(int fd, char *cause, char *err_num, char *short_msg, char *long_msg);

/* 
 * main - 프록시 서버의 시작점
 * 지정된 포트에서 클라이언트의 연결을 대기하고 처리
 */
int main(int argc, char *argv[]) {
    setbuf(stdout, NULL);  /* 디버깅을 위한 표준 출력 버퍼링 비활성화 */

    int listen_fd, conn_fd;
    char hostname[MAXLINE], port[MAXLINE];
    socklen_t client_len;
    struct sockaddr_storage client_addr;

    /* 명령행 인자 검사 */
    if (argc != 2) {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(1);
    }

    listen_fd = Open_listenfd(argv[1]);

    while (1) {
        client_len = sizeof(client_addr);
        conn_fd = Accept(listen_fd, (SA *)&client_addr, &client_len);
        Getnameinfo((SA *)&client_addr, client_len, hostname, MAXLINE, port, MAXLINE, 0);
        printf("클라이언트 연결 수락: (%s, %s)\n", hostname, port);
        handle_transaction(conn_fd);
        Close(conn_fd);
    }
}

/*
 * handle_transaction - 단일 HTTP 트랜잭션 처리
 * 클라이언트의 요청을 받아 서버로 전달하고 응답을 회신
 */
void handle_transaction(int client_fd) {
    int server_fd;
    char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
    char hostname[MAXLINE], path[MAXLINE], port[MAXLINE];
    rio_t client_rio, server_rio;

    printf("\n<<<< 새로운 클라이언트 요청 >>>>\n");

    /* 요청 라인 읽기 */
    Rio_readinitb(&client_rio, client_fd);
    if (!Rio_readlineb(&client_rio, buf, MAXLINE))
        return;

    printf("클라이언트 요청 라인: %s", buf);
    sscanf(buf, "%s %s %s", method, uri, version);
    printf("메소드: %s, URI: %s, 버전: %s\n", method, uri, version);

    /* 지원하는 메소드 검사 */
    if (strcasecmp(method, "GET") != 0 && strcasecmp(method, "HEAD") != 0) {
        send_error(client_fd, method, "501", "지원하지 않는 요청",
                   "프록시가 지원하지 않는 메소드입니다");
        return;
    }

    /* URI 파싱 */
    if (parse_uri(uri, hostname, path, port) < 0) {
        send_error(client_fd, uri, "400", "잘못된 요청",
                   "프록시가 URI를 파싱할 수 없습니다");
        return;
    }

    /* 서버 연결 */
    printf("서버 연결 시도: %s:%s\n", hostname, port);
    server_fd = Open_clientfd(hostname, port);
    if (server_fd < 0) {
        send_error(client_fd, hostname, "404", "찾을 수 없음",
                   "서버에 연결할 수 없습니다");
        return;
    }

    /* 서버와의 통신 처리 */
    Rio_readinitb(&server_rio, server_fd);
    send_request(server_fd, method, path, hostname);
    forward_response(server_fd, client_fd);

    Close(server_fd);
}

/*
 * parse_uri - URI를 호스트명, 경로, 포트로 파싱
 * 반환값: 성공시 0, 실패시 -1
 */
int parse_uri(char *uri, char *hostname, char *path, char *port) {
    strcpy(port, "80");  /* 기본 포트 설정 */

    char *host_start = strstr(uri, "://") ? strstr(uri, "://") + 3 : uri;

    /* 경로 파싱 */
    char *path_start = strchr(host_start, '/');
    if (path_start) {
        strcpy(path, path_start);
        *path_start = '\0';
    } else {
        strcpy(path, "/");
    }

    /* 포트 번호 파싱 */
    char *port_start = strchr(host_start, ':');
    if (port_start) {
        *port_start = '\0';
        strcpy(port, port_start + 1);
    }

    /* 로컬 통신 최적화 */
    if (strcmp(hostname, "13.125.78.231") == 0) {
        strcpy(hostname, "127.0.0.1");
    } else {
        strcpy(hostname, host_start);
    }

    return 0;
}

/*
 * send_request - 서버에 HTTP 요청 전송
 * 필요한 모든 HTTP 헤더를 포함하여 요청
 */
void send_request(int server_fd, char *method, char *path, char *hostname) {
    char buf[MAXLINE];

    printf("\n<<<< 서버로 프록시 요청 전송 >>>>\n");

    /* 요청 라인 전송 */
    sprintf(buf, "%s %s HTTP/1.0\r\n", method, path);
    printf("요청 라인: %s", buf);
    Rio_writen(server_fd, buf, strlen(buf));

    /* 헤더 전송 */
    sprintf(buf, "Host: %s\r\n", hostname);
    printf("<헤더>\nHost: %s", buf);
    Rio_writen(server_fd, buf, strlen(buf));

    sprintf(buf, "%s", user_agent_hdr);
    printf("User-Agent: %s", buf);
    Rio_writen(server_fd, buf, strlen(buf));

    sprintf(buf, "Connection: close\r\n");
    printf("Connection: %s", buf);
    Rio_writen(server_fd, buf, strlen(buf));

    sprintf(buf, "Proxy-Connection: close\r\n\r\n");
    printf("Proxy-Connection: %s", buf);
    Rio_writen(server_fd, buf, strlen(buf));
}

/*
 * forward_response - 서버로부터 받은 응답을 클라이언트에게 전달
 * 헤더와 본문을 모두 전송하며 진행 상황을 출력
 */
void forward_response(int server_fd, int client_fd) {
    char buf[MAXLINE];
    rio_t rio;
    ssize_t n;
    int total_bytes = 0, header_end = 0;

    printf("\n<<<< 서버 응답 수신 >>>>\n");
    Rio_readinitb(&rio, server_fd);

    /* 헤더 전달 */
    while ((n = Rio_readlineb(&rio, buf, MAXLINE)) != 0) {
        printf("수신: %s", buf);
        Rio_writen(client_fd, buf, n);
        total_bytes += n;

        if (strcmp(buf, "\r\n") == 0) {
            header_end = 1;
            break;
        }
    }

    /* 본문 전달 */
    while ((n = Rio_readlineb(&rio, buf, MAXLINE)) != 0) {
        Rio_writen(client_fd, buf, n);
        total_bytes += n;
    }
    printf("<<<< 응답 전송 완료 >>>>\r\n");
}

/*
 * send_error - 클라이언트에게 에러 메시지 전송
 * HTML 형식의 에러 페이지 생성 및 전송
 */
void send_error(int fd, char *cause, char *err_num, char *short_msg, char *long_msg) {
    char buf[MAXLINE], body[MAXBUF];

    /* 응답 본문 생성 */
    sprintf(body, "<html><title>프록시 오류</title>");
    sprintf(body, "%s<body bgcolor=""ffffff"">\r\n", body);
    sprintf(body, "%s%s: %s\r\n", body, err_num, short_msg);
    sprintf(body, "%s<p>%s: %s\r\n", body, long_msg, cause);
    sprintf(body, "%s<hr><em>프록시 웹 서버</em>\r\n", body);

    /* 응답 헤더 전송 */
    sprintf(buf, "HTTP/1.0 %s %s\r\n", err_num, short_msg);
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-type: text/html\r\n");
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));
    Rio_writen(fd, buf, strlen(buf));

    /* 응답 본문 전송 */
    Rio_writen(fd, body, strlen(body));
}