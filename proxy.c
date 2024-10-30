#include "csapp.h"
#include <stdio.h>

/* 프록시 서버의 캐시 관련 상수 정의 */
#define MAX_CACHE_SIZE 1049000 // 최대 캐시 크기 (약 1MB)
#define MAX_OBJECT_SIZE 102400 // 캐시 가능한 최대 객체 크기 (약 100KB)

/* User-Agent 헤더 문자열 상수 정의 */
static const char *user_agent_hdr =
    "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 "
    "Firefox/10.0.3\r\n";

/* 함수 프로토타입 선언 */
void doit(int fd); // HTTP 트랜잭션을 처리하는 함수
void doRequest(int serverfd, char *method, char *path,
               char *hostname); // 서버에 HTTP 요청을 보내는 함수
void doResponse(
    int serverfd,
    int clientfd); // 서버로부터 받은 응답을 클라이언트에게 전달하는 함수
int parse_uri(char *uri, char *hostname, char *path,
              char *port); // URI를 파싱하는 함수
void clientError(int fd, char *cause, char *errnum, char *shortmsg,
                 char *longmsg); // 에러 응답을 보내는 함수

/*
 * main 함수 - 프록시 서버의 시작점
 * 주어진 포트에서 클라이언트의 연결을 대기하고 요청을 처리
 */
int main(int argc, char *argv[]) {
  // 표준 출력 버퍼링 비활성화 (디버깅 용이)
  setbuf(stdout, NULL);

  int listenfd, connfd;
  char hostname[MAXLINE], port[MAXLINE];
  socklen_t clientlen;
  struct sockaddr_storage clientaddr;

  // 명령행 인자 확인 (포트번호)
  if (argc != 2) {
    fprintf(stderr, "usage: %s <port>\n", argv[0]);
    exit(1);
  }

  // 지정된 포트에서 연결 요청 대기
  listenfd = Open_listenfd(argv[1]);

  // 무한 루프를 돌며 클라이언트 요청 처리
  while (1) {
    clientlen = sizeof(clientaddr);
    connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);
    Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE,
                0);
    printf("Accepted connection from (%s, %s)\n", hostname, port);
    doit(connfd);  // 클라이언트 요청 처리
    Close(connfd); // 연결 종료
  }
}

/*
 * doit 함수 - 하나의 HTTP 트랜잭션을 처리
 * 1. 클라이언트로부터 요청을 받음
 * 2. 요청을 파싱하고 검증
 * 3. 서버에 요청을 전달
 * 4. 서버로부터 응답을 받아 클라이언트에게 전달
 */
void doit(int clientfd) {
  int serverfd;
  char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
  char hostname[MAXLINE], path[MAXLINE], port[MAXLINE];
  rio_t clientRio, serverRio;

  printf("\n<<<< New Client Request >>>>\n");

  // 클라이언트의 요청 라인 읽기
  Rio_readinitb(&clientRio, clientfd);
  if (!Rio_readlineb(&clientRio, buf, MAXLINE))
    return;

  // 요청 라인 파싱 및 출력
  printf("Client Request Line: %s", buf);
  sscanf(buf, "%s %s %s", method, uri, version);
  printf("Method: %s, URI: %s, Version: %s\n", method, uri, version);

  // GET과 HEAD 메소드만 지원
  if (strcasecmp(method, "GET") != 0 && strcasecmp(method, "HEAD") != 0) {
    clientError(clientfd, method, "501", "Not implemented",
                "Proxy does not implement this method");
    return;
  }

  // URI 파싱
  if (parse_uri(uri, hostname, path, port) < 0) {
    clientError(clientfd, uri, "400", "Bad request",
                "Proxy could not parse URI");
    return;
  }

  // 서버와 연결 시도
  printf("Connecting to server: %s:%s\n", hostname, port);
  serverfd = Open_clientfd(hostname, port);
  if (serverfd < 0) {
    clientError(clientfd, hostname, "404", "Not found",
                "Could not connect to server");
    return;
  }

  // 서버와의 통신 처리
  Rio_readinitb(&serverRio, serverfd);
  doRequest(serverfd, method, path, hostname); // 서버에 요청 전송
  doResponse(serverfd, clientfd); // 서버 응답을 클라이언트에게 전달

  Close(serverfd);
}

/*
 * parse_uri 함수 - URI를 호스트명, 경로, 포트로 파싱
 * 예: http://www.example.com:8080/path/page.html
 * 반환값: 성공시 0, 실패시 -1
 */
int parse_uri(char *uri, char *hostname, char *path, char *port) {
  strcpy(port, "80"); // 기본 포트는 80

  // 프로토콜 부분("http://") 건너뛰기
  char *hoststart = strstr(uri, "://") ? strstr(uri, "://") + 3 : uri;

  // 경로 부분 파싱
  char *pathstart = strchr(hoststart, '/');
  if (pathstart) {
    strcpy(path, pathstart);
    *pathstart = '\0';
  } else
    strcpy(path, "/"); // 기본 경로는 "/"

  // 포트 번호 파싱
  char *portstart = strchr(hoststart, ':');
  if (portstart) {
    *portstart = '\0';
    strcpy(port, portstart + 1);
  }

  strcpy(hostname, hoststart);
  return 0;
}

/*
 * doRequest 함수 - 서버에 HTTP 요청을 전송
 * 필요한 HTTP 헤더들을 포함하여 요청 생성
 */
void doRequest(int serverfd, char *method, char *path, char *hostname) {
  char buf[MAXLINE];

  printf("\n<<<< Proxy Request to Server >>>>\n");

  // 요청 라인 전송
  sprintf(buf, "%s %s HTTP/1.0\r\n", method, path);
  printf("Request Line: %s", buf);
  Rio_writen(serverfd, buf, strlen(buf));

  // Host 헤더 전송
  sprintf(buf, "Host: %s\r\n", hostname);
  printf("<HEADER>\nHost: %s", buf);
  Rio_writen(serverfd, buf, strlen(buf));

  // User-Agent 헤더 전송
  sprintf(buf, "%s", user_agent_hdr);
  printf("User-Agent: %s", buf);
  Rio_writen(serverfd, buf, strlen(buf));

  // Connection 헤더 전송
  sprintf(buf, "Connection: close\r\n");
  printf("Connection: %s", buf);
  Rio_writen(serverfd, buf, strlen(buf));

  // Proxy-Connection 헤더 전송
  sprintf(buf, "Proxy-Connection: close\r\n\r\n");
  printf("Proxy-Connection: %s", buf);
  Rio_writen(serverfd, buf, strlen(buf));
}

/*
 * doResponse 함수 - 서버로부터 받은 응답을 클라이언트에게 전달
 * 헤더와 바디를 모두 전달하며 전송 현황을 출력
 */
void doResponse(int serverfd, int clientfd) {
  char buf[MAXLINE];
  rio_t rio;
  ssize_t n;
  int totalBytes = 0, headerEnd = 0;

  printf("\n<<<< Server Response >>>>\n");
  Rio_readinitb(&rio, serverfd);

  // 헤더 부분 전달
  while ((n = Rio_readlineb(&rio, buf, MAXLINE)) != 0) {
    printf("Received: %s", buf);
    Rio_writen(clientfd, buf, n);
    totalBytes += n;

    // 빈 줄(\r\n)을 만나면 헤더의 끝
    if (strcmp(buf, "\r\n") == 0) {
      headerEnd = 1;
      break;
    }
  }

  // 바디 부분 전달
  while ((n = Rio_readlineb(&rio, buf, MAXLINE)) != 0) {
    Rio_writen(clientfd, buf, n);
    totalBytes += n;
  }
  printf("<<<< Response Complete >>>>\r\n");
}

/*
 * clientError 함수 - 클라이언트에게 에러 메시지를 전송
 * HTML 형식의 에러 페이지를 생성하여 전송
 */
void clientError(int fd, char *cause, char *errnum, char *shortmsg,
                 char *longmsg) {
  char buf[MAXLINE], body[MAXBUF];

  // HTML 응답 본문 생성
  sprintf(body, "<html><title>Proxy Error</title>");
  sprintf(body, "%s<body bgcolor=\"ffffff\">\r\n", body);
  sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
  sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
  sprintf(body, "%s<hr><em>The Proxy Web server</em>\r\n", body);

  // 응답 헤더 전송
  sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Content-type: text/html\r\n");
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));
  Rio_writen(fd, buf, strlen(buf));

  // 응답 본문 전송
  Rio_writen(fd, body, strlen(body));
}