/* $begin tinymain */
/*
 * tiny.c - GET 메소드를 사용하여 정적 및 동적 컨텐츠를 제공하는
 *          간단한 반복형 HTTP/1.0 웹 서버
 *
 * 2019년 11월 업데이트 droh
 *   - serve_static()과 clienterror()의 sprintf() 별칭 문제 수정
 */
#include "csapp.h"

/* 함수 프로토타입 */
void handle_request(int fd);                /* HTTP 요청 처리 */
void read_request_headers(rio_t *rp);       /* HTTP 요청 헤더 읽기 */
int parse_uri(char *uri, char *filename, 
              char *cgi_args);              /* URI 파싱 */
void serve_static(int fd, char *filename, 
                  int filesize, char *method); /* 정적 컨텐츠 제공 */
void get_filetype(char *filename, 
                  char *filetype);          /* 파일 타입 확인 */
void serve_dynamic(int fd, char *filename, 
                   char *cgi_args, char *method); /* 동적 컨텐츠 제공 */
void client_error(int fd, char *cause, char *err_num, 
                  char *short_msg, char *long_msg); /* 에러 응답 전송 */

/*
 * main - 웹 서버의 시작점
 * 지정된 포트에서 연결을 수신하고 HTTP 요청을 처리
 */
int main(int argc, char **argv) {
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
        
        /* 클라이언트 연결 정보 출력 */
        Getnameinfo((SA *)&client_addr, client_len, hostname, 
                    MAXLINE, port, MAXLINE, 0);
        printf("Accepted connection from (%s, %s)\n", hostname, port);
        
        handle_request(conn_fd);
        Close(conn_fd);
    }
}

/*
 * handle_request - HTTP 요청 처리
 * 요청을 분석하고 적절한 응답(정적/동적 컨텐츠)을 생성
 */
void handle_request(int fd) {
    int is_static;
    struct stat sbuf;
    char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
    char filename[MAXLINE], cgi_args[MAXLINE];
    rio_t rio;

    /* 요청 라인 읽기 및 분석 */
    Rio_readinitb(&rio, fd);
    Rio_readlineb(&rio, buf, MAXLINE);
    printf("요청 헤더:\n");
    printf("%s", buf);
    sscanf(buf, "%s %s %s", method, uri, version);

    /* GET과 HEAD 메소드만 지원 */
    if (strcasecmp(method, "GET") != 0 && strcasecmp(method, "HEAD") != 0) {
        client_error(fd, method, "501", "Not implemented",
                     "지원하지 않는 요청 메소드입니다");
        return;
    }

    read_request_headers(&rio);

    /* URI 파싱 */
    is_static = parse_uri(uri, filename, cgi_args);

    /* 파일 상태 확인 */
    if (stat(filename, &sbuf) < 0) {
        client_error(fd, filename, "404", "Not found",
                     "요청한 파일을 찾을 수 없습니다");
        return;
    }

    if (is_static) { /* 정적 컨텐츠 처리 */
        if (!(S_ISREG(sbuf.st_mode)) || !(S_IRUSR & sbuf.st_mode)) {
            client_error(fd, filename, "403", "Forbidden",
                         "파일 읽기 권한이 없습니다");
            return;
        }
        serve_static(fd, filename, sbuf.st_size, method);
    }
    else { /* 동적 컨텐츠 처리 */
        if (!(S_ISREG(sbuf.st_mode)) || !(S_IXUSR & sbuf.st_mode)) {
            client_error(fd, filename, "403", "Forbidden",
                         "CGI 프로그램을 실행할 수 없습니다");
            return;
        }
        serve_dynamic(fd, filename, cgi_args, method);
    }
}

/*
 * read_request_headers - HTTP 요청 헤더 읽기
 * 빈 줄(CRLF)이 나올 때까지 모든 헤더를 읽음
 */
void read_request_headers(rio_t *rp) {
    char buf[MAXLINE];

    Rio_readlineb(rp, buf, MAXLINE);
    while (strcmp(buf, "\r\n")) {
        Rio_readlineb(rp, buf, MAXLINE);
        printf("%s", buf);
    }
    return;
}

/*
 * parse_uri - URI를 파일명과 CGI 인자로 파싱
 * 반환값: 1(정적 컨텐츠), 0(동적 컨텐츠)
 */
int parse_uri(char *uri, char *filename, char *cgi_args) {
    char *ptr;

    /* 정적 컨텐츠 */
    if (!strstr(uri, "cgi-bin")) {
        strcpy(cgi_args, "");
        strcpy(filename, ".");
        strcat(filename, uri);
        if (uri[strlen(uri) - 1] == '/')
            strcat(filename, "home.html");
        return 1;
    }
    /* 동적 컨텐츠 */
    else {
        ptr = index(uri, '?');
        if (ptr) {
            strcpy(cgi_args, ptr + 1);
            *ptr = '\0';
        }
        else
            strcpy(cgi_args, "");
        strcpy(filename, ".");
        strcat(filename, uri);
        return 0;
    }
}

/*
 * serve_static - 정적 컨텐츠 제공
 * 파일을 읽어서 클라이언트에게 전송
 */
void serve_static(int fd, char *filename, int filesize, char *method) {
    int src_fd;
    char *src_p, filetype[MAXLINE], buf[MAXBUF];
    int idx = 0;

    /* 파일 타입 결정 */
    get_filetype(filename, filetype);

    /* HTTP 응답 헤더 생성 */
    idx += sprintf(buf + idx, "HTTP/1.1 200 OK\r\n");
    idx += sprintf(buf + idx, "Server: Tiny Web Server\r\n");
    idx += sprintf(buf + idx, "Connection: close\r\n");
    idx += sprintf(buf + idx, "Content-length: %d\r\n", filesize);
    idx += sprintf(buf + idx, "Content-type: %s\r\n\r\n", filetype);

    /* 응답 헤더 전송 */
    Rio_writen(fd, buf, strlen(buf));
    printf("응답 헤더:\n");
    printf("%s", buf);

    /* HEAD 요청인 경우 응답 본문 생략 */
    if (strcasecmp(method, "HEAD") == 0)
        return;

    /* 요청 파일의 내용을 응답 본문으로 전송 */
    src_fd = Open(filename, O_RDONLY, 0);
    src_p = (char *)malloc(filesize);
    rio_readn(src_fd, src_p, filesize);
    Close(src_fd);
    Rio_writen(fd, src_p, filesize);
    free(src_p);
}

/*
 * get_filetype - 파일 확장자를 통해 Content-type 결정
 */
void get_filetype(char *filename, char *filetype) {
    if (strstr(filename, ".html"))
        strcpy(filetype, "text/html");
    else if (strstr(filename, ".gif"))
        strcpy(filetype, "image/gif");
    else if (strstr(filename, ".png"))
        strcpy(filetype, "image/png");
    else if (strstr(filename, ".jpg"))
        strcpy(filetype, "image/jpeg");
    else if (strstr(filename, ".mp4"))
        strcpy(filetype, "video/mp4");
    else
        strcpy(filetype, "text/plain");
}

/*
 * serve_dynamic - 동적 컨텐츠 제공
 * CGI 프로그램을 실행하여 결과를 클라이언트에게 전송
 */
void serve_dynamic(int fd, char *filename, char *cgi_args, char *method) {
    char buf[MAXLINE], *empty_list[] = { NULL };

    /* HTTP 응답 헤더 전송 */
    sprintf(buf, "HTTP/1.0 200 OK\r\n");
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Server: Tiny Web Server\r\n");
    Rio_writen(fd, buf, strlen(buf));

    if (Fork() == 0) {  /* 자식 프로세스 */
        /* CGI 환경 변수 설정 */
        setenv("QUERY_STRING", cgi_args, 1);
        /* 표준 출력을 클라이언트 소켓으로 재지정 */
        Dup2(fd, STDOUT_FILENO);
        /* CGI 프로그램 실행 */
        Execve(filename, empty_list, environ);
    }
    Wait(NULL); /* 자식 프로세스 종료 대기 */
}

/*
 * client_error - HTTP 오류 응답 전송
 * HTML 형식의 에러 페이지 생성
 */
void client_error(int fd, char *cause, char *err_num, 
                  char *short_msg, char *long_msg) {
    char buf[MAXLINE], body[MAXBUF];

    /* HTTP 응답 본문 생성 */
    sprintf(body, "<html><title>Tiny 오류</title>");
    sprintf(body, "%s<body bgcolor=""ffffff"">\r\n", body);
    sprintf(body, "%s%s: %s\r\n", body, err_num, short_msg);
    sprintf(body, "%s<p>%s: %s\r\n", body, long_msg, cause);
    sprintf(body, "%s<hr><em>Tiny 웹 서버</em>\r\n", body);

    /* HTTP 응답 헤더 전송 */
    sprintf(buf, "HTTP/1.0 %s %s\r\n", err_num, short_msg);
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-type: text/html\r\n");
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));
    Rio_writen(fd, buf, strlen(buf));

    /* HTTP 응답 본문 전송 */
    Rio_writen(fd, body, strlen(body));
}