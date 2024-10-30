/* $begin tinymain */
/*
 * tiny.c - A simple, iterative HTTP/1.0 Web server that uses the
 *     GET method to serve static and dynamic content.
 *
 * Updated 11/2019 droh
 *   - Fixed sprintf() aliasing issue in serve_static(), and clienterror().
 */
#include "csapp.h"

/* 함수 프로토타입 선언 */
void doit(int fd);                // HTTP 트랜잭션을 처리하는 함수
void read_requesthdrs(rio_t *rp); // HTTP 요청 헤더를 읽는 함수
int parse_uri(char *uri, char *filename,
              char *cgiargs); // URI를 파일이름과 CGI 인자로 파싱하는 함수
void serve_static(int fd, char *filename, int filesize,
                  char *method); // 정적 컨텐츠를 클라이언트에게 제공하는 함수
void get_filetype(char *filename, char *filetype); // 파일 타입을 결정하는 함수
void serve_dynamic(int fd, char *filename, char *cgiargs,
                   char *method); // 동적 컨텐츠를 클라이언트에게 제공하는 함수
void clienterror(int fd, char *cause, char *errnum, char *shortmsg,
                 char *longmsg); // 에러 메시지를 클라이언트에게 전송하는 함수

/*
 * main 함수 - 웹 서버의 시작점
 * 인자로 포트번호를 받아 해당 포트에서 클라이언트의 연결을 대기
 * 새로운 연결이 들어올 때마다 연결을 수락하고 트랜잭션을 처리
 */
int main(int argc, char **argv) {
  int listenfd, connfd;
  char hostname[MAXLINE], port[MAXLINE];
  socklen_t clientlen;
  struct sockaddr_storage clientaddr;

  // 명령행 인자 체크 (포트번호)
  if (argc != 2) {
    fprintf(stderr, "usage: %s <port>\n", argv[0]);
    exit(1);
  }

  // 지정된 포트에서 연결 요청 대기
  listenfd = Open_listenfd(argv[1]);

  // 무한 루프를 돌며 클라이언트의 연결 요청을 처리
  while (1) {
    clientlen = sizeof(clientaddr);
    connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);

    // 클라이언트의 호스트명과 포트번호 출력
    Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE,
                0);
    printf("Accepted connection from (%s, %s)\n", hostname, port);

    // HTTP 트랜잭션 처리
    doit(connfd);
    // 연결 종료
    Close(connfd);
  }
}

/*
 * doit 함수 - 한 개의 HTTP 트랜잭션을 처리
 * 1. 요청 라인과 헤더를 읽음
 * 2. 정적/동적 컨텐츠 판단
 * 3. 적절한 함수 호출하여 요청 처리
 */
void doit(int fd) {
  int is_static;
  struct stat sbuf;
  char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
  char filename[MAXLINE], cgiargs[MAXLINE];
  rio_t rio;

  // 요청 라인 읽고 분석
  Rio_readinitb(&rio, fd);
  Rio_readlineb(&rio, buf, MAXLINE);
  printf("Request headers: \n");
  printf("%s", buf);
  sscanf(buf, "%s %s %s", method, uri, version);

  // GET 또는 HEAD 메소드만 지원
  if (strcasecmp(method, "GET") != 0 && strcasecmp(method, "HEAD") != 0) {
    clienterror(fd, method, "501", "Not implemented",
                "Tiny does not implement this method");
    return;
  }

  // 나머지 요청 헤더 읽기
  read_requesthdrs(&rio);

  // URI를 파일이름과 CGI 인자로 파싱
  is_static = parse_uri(uri, filename, cgiargs);

  // 파일의 상태 확인
  if (stat(filename, &sbuf) < 0) {
    clienterror(fd, filename, "404", "Not found",
                "Tiny couldn't find this file");
    return;
  }

  if (is_static) { // 정적 컨텐츠 처리
    // 일반 파일이고 읽기 권한이 있는지 확인
    if (!(S_ISREG(sbuf.st_mode)) || !(S_IRUSR & sbuf.st_mode)) {
      clienterror(fd, filename, "403", "Forbidden",
                  "Tiny couldn't read the file");
      return;
    }
    serve_static(fd, filename, sbuf.st_size, method);
  } else { // 동적 컨텐츠 처리
    // 실행 가능한 파일인지 확인
    if (!(S_ISREG(sbuf.st_mode)) || !(S_IXUSR & sbuf.st_mode)) {
      clienterror(fd, filename, "403", "Forbidden",
                  "Tiny couldn't run the CGI program");
      return;
    }
    serve_dynamic(fd, filename, cgiargs, method);
  }
}

/*
 * read_requesthdrs 함수 - HTTP 요청의 헤더를 읽음
 * 빈 줄(CRLF)이 나올 때까지 요청 헤더를 모두 읽어서 출력
 */
void read_requesthdrs(rio_t *rp) {
  char buf[MAXLINE];
  Rio_readlineb(rp, buf, MAXLINE);
  while (strcmp(buf, "\r\n")) {
    Rio_readlineb(rp, buf, MAXLINE);
    printf("%s", buf);
  }
  return;
}

/*
 * parse_uri 함수 - URI를 파일이름과 CGI 인자로 파싱
 * URI에 "cgi-bin"이 포함되어 있으면 동적 컨텐츠로 판단
 * 반환값: 1(정적 컨텐츠), 0(동적 컨텐츠)
 */
int parse_uri(char *uri, char *filename, char *cgiargs) {
  char *ptr;

  // 정적 컨텐츠인 경우
  if (!strstr(uri, "cgi-bin")) {
    strcpy(cgiargs, "");   // CGI 인자 문자열 초기화
    strcpy(filename, "."); // 현재 디렉토리에서 시작
    strcat(filename, uri); // URI를 상대 리눅스 경로이름으로 변환
    if (uri[strlen(uri) - 1] == '/') // URI가 '/'로 끝나면
      strcat(filename, "home.html"); // 기본 파일이름 추가
    return 1;
  }
  // 동적 컨텐츠인 경우
  else {
    ptr = index(uri, '?'); // CGI 인자 찾기
    if (ptr) {
      strcpy(cgiargs, ptr + 1); // CGI 인자 저장
      *ptr = '\0';              // URI에서 CGI 인자 분리
    } else
      strcpy(cgiargs, ""); // CGI 인자가 없는 경우
    strcpy(filename, "."); // 현재 디렉토리에서 시작
    strcat(filename, uri); // URI를 상대 리눅스 경로이름으로 변환
    return 0;
  }
}

/*
 * serve_static 함수 - 정적 컨텐츠를 클라이언트에게 제공
 * 응답 헤더를 보내고, 요청한 파일의 내용을 응답 본체로 보냄
 */
void serve_static(int fd, char *filename, int filesize, char *method) {
  int srcfd;
  char *srcp, filetype[MAXLINE], buf[MAXBUF];

  // 파일 타입 결정
  get_filetype(filename, filetype);
  int idx = 0;

  // HTTP 응답 헤더 생성
  idx += sprintf(buf + idx, "HTTP/1.1 200 OK\r\n");
  idx += sprintf(buf + idx, "Server: Tiny Web Server\r\n");
  idx += sprintf(buf + idx, "Connection: close\r\n");
  idx += sprintf(buf + idx, "Content-length: %d\r\n", filesize);
  idx += sprintf(buf + idx, "Content-type: %s\r\n\r\n", filetype);

  // 응답 헤더 전송
  Rio_writen(fd, buf, strlen(buf));
  printf("Response headers:\n");
  printf("%s", buf);

  // HEAD 메소드인 경우 응답 본체를 보내지 않음
  if (strcasecmp(method, "HEAD") == 0)
    return;

  // 요청한 파일을 읽어서 응답 본체로 전송
  srcfd = Open(filename, O_RDONLY, 0);
  srcp = (char *)malloc(filesize);
  rio_readn(srcfd, srcp, filesize);
  close(srcfd);
  rio_writen(fd, srcp, filesize);
  free(srcp);
}

/*
 * get_filetype 함수 - 파일 이름의 확장자를 검사해서 파일 타입을 결정
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
 * serve_dynamic 함수 - 동적 컨텐츠를 클라이언트에게 제공
 * 자식 프로세스를 fork하고 CGI 프로그램을 실행
 */
void serve_dynamic(int fd, char *filename, char *cgiargs, char *method) {
  char buf[MAXLINE], *emptylist[] = {NULL};

  // HTTP 응답 헤더 전송
  sprintf(buf, "HTTP/1.0 200 OK\r\n");
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Server: Tiny Web Server\r\n");
  Rio_writen(fd, buf, strlen(buf));

  if (Fork() == 0) { // 자식 프로세스 생성
    // CGI 인자를 환경변수로 설정
    setenv("QUERY_STRING", cgiargs, 1);
    // 자식의 표준 출력을 클라이언트 소켓으로 리다이렉션
    Dup2(fd, STDOUT_FILENO);
    // CGI 프로그램 실행
    Execve(filename, emptylist, environ);
  }
  Wait(NULL); // 자식 프로세스가 종료될 때까지 대기
}

/*
 * clienterror 함수 - HTTP 오류 응답을 클라이언트에게 전송
 * 오류 상황에 맞는 상태 코드와 메시지를 포함한 HTML 문서 전송
 */
void clienterror(int fd, char *cause, char *errnum, char *shortmsg,
                 char *longmsg) {
  char buf[MAXLINE], body[MAXBUF];

  // HTTP 응답 본체 생성
  sprintf(body, "<html><title>Tiny Error</title>");
  sprintf(body,
          "%s<body bgcolor="
          "ffffff"
          ">\r\n",
          body);
  sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
  sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
  sprintf(body, "%s<hr><em>The Tiny Web server</em>\r\n", body);

  // HTTP 응답 헤더 전송
  sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Content-type: text/html\r\n");
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));
  Rio_writen(fd, buf, strlen(buf));

  // HTTP 응답 본체 전송
  Rio_writen(fd, body, strlen(body));
}