/* $begin tinymain */
/*
 * tiny.c - A simple, iterative HTTP/1.0 Web server that uses the
 *     GET method to serve static and dynamic content.
 *
 * Updated 11/2019 droh
 *   - Fixed sprintf() aliasing issue in serve_static(), and clienterror().
 */
#include "csapp.h"

void doit(int fd);
void read_requesthdrs(rio_t *rp);
int parse_uri(char *uri, char *filename, char *cgiargs);
void serve_static(int fd, char *filename, int filesize, char *method);
void get_filetype(char *filename, char *filetype);
void serve_dynamic(int fd, char *filename, char *cgiargs, char *method);
void clienterror(int fd, char *cause, char *errnum, char *shortmsg,
                 char *longmsg);

int main(int argc, char **argv) {
  int listenfd, connfd;
  char hostname[MAXLINE], port[MAXLINE];
  socklen_t clientlen;
  struct sockaddr_storage clientaddr;

  /* Check command line args */
  if (argc != 2) {
    fprintf(stderr, "usage: %s <port>\n", argv[0]);
    exit(1);
  }

  listenfd = Open_listenfd(argv[1]); // 포트번호를 인자로 받기
  while (1) {
    clientlen = sizeof(clientaddr);
    connfd = Accept(listenfd, (SA *)&clientaddr,
                    &clientlen); // line:netp:tiny:accept
    Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE,
                0);
    printf("Accepted connection from (%s, %s)\n", hostname, port);
    doit(connfd);  // line:netp:tiny:doit
    Close(connfd); // line:netp:tiny:close
  }
}

void doit(int fd) {
  int is_static;
  struct stat sbuf;
  char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
  char filename[MAXLINE], cgiargs[MAXLINE];
  rio_t rio;

  Rio_readinitb(&rio, fd);
  Rio_readlineb(&rio, buf, MAXLINE);
  printf("Request headers: \n");
  printf("%s", buf);
  sscanf(buf, "%s %s %s", method, uri, version);
  /* 숙제 11.11: HEAD 메서드 인식 기능 추가 */
  if (strcasecmp(method, "GET") != 0 && strcasecmp(method, "HEAD") != 0) {
    clienterror(fd, method, "501", "Not implemented",
                "Tiny does not implement this method");
    return;
  }
  read_requesthdrs(&rio);

  /* URI 파싱 */
  is_static = parse_uri(uri, filename, cgiargs);
  if (stat(filename, &sbuf) < 0) {
    clienterror(fd, filename, "404", "Not found",
                "Tiny couldn't find this file");
    return;
  }
  if (is_static) { // 정적 컨텐츠
    if (!(S_ISREG(sbuf.st_mode)) || !(S_IRUSR & sbuf.st_mode)) {
      clienterror(fd, filename, "403", "Forbidden",
                  "Tiny couldn't read the file");
      return;
    }
    serve_static(fd, filename, sbuf.st_size, method);
  } else { // 동적 컨텐츠
    if (!(S_ISREG(sbuf.st_mode)) || !(S_IXUSR & sbuf.st_mode)) {
      clienterror(fd, filename, "403", "Forbidden",
                  "Tiny couldn't run the CGI program");
      return;
    }
    serve_dynamic(fd, filename, cgiargs, method);
  }
}

void read_requesthdrs(rio_t *rp) {
  char buf[MAXLINE];
  Rio_readlineb(rp, buf, MAXLINE); // 요청 헤더를 한 줄씩 읽어서 출력
  while (strcmp(buf, "\r\n")) {    // 빈줄이 나올 때까지 읽음
    Rio_readlineb(rp, buf, MAXLINE);
    printf("%s", buf);
  }
  return;
}

int parse_uri(char *uri, char *filename, char *cgiargs) {
  char *ptr;
  if (!strstr(uri, "cgi-bin")) { // 정적컨텐츠
    strcpy(cgiargs, "");         // CGI 인자 string 삭제
    strcpy(filename, "."); // URI를 상대 리눅스 경로 이름으로 변환
    strcat(filename, uri);
    if (uri[strlen(uri) - 1] == '/')
      strcat(filename, "home.html"); // 기본 파일이름 추가
    return 1;
  } else { // 동적컨텐츠
    ptr = index(uri, '?');
    if (ptr) { // 모든 CGI 인자 추출
      strcpy(cgiargs, ptr + 1);
      *ptr = '\0';
    } else
      strcpy(cgiargs, "");
    strcpy(filename, "."); // 나머지 URI 부분 상대 리눅스 파일 이름으로 변환
    strcat(filename, uri);
    return 0;
  }
}

// HTML, 무형식 텍스트파일, GIF, PNG, JPEG
void serve_static(int fd, char *filename, int filesize, char *method) {
  int srcfd;
  char *srcp, filetype[MAXLINE], buf[MAXBUF];

  /* 클라이언트에게 응답 헤더 전송 */
  get_filetype(filename, filetype); // 파일 타입 결정
  int idx = 0;

  // sprintf(buf, "HTTP/1.1 200 OK\r\n");
  // sprintf(buf, "%sServer: Tiny Web Server\r\n", buf);
  // sprintf(buf, "%sConnection: close\r\n", buf);
  // sprintf(buf, "%sContent-length: %d\r\n", buf, filesize);
  // sprintf(buf, "%sContent-type: %s\r\n\r\n", buf, filetype);
  idx += sprintf(buf + idx, "HTTP/1.1 200 OK\r\n");
  idx += sprintf(buf + idx, "Server: Tiny Web Server\r\n");
  idx += sprintf(buf + idx, "Connection: close\r\n");
  idx += sprintf(buf + idx, "Content-length: %d\r\n", filesize);
  idx += sprintf(buf + idx, "Content-type: %s\r\n\r\n", filetype);

  Rio_writen(fd, buf, strlen(buf));
  printf("Response headers:\n");
  printf("%s", buf);

  /* 숙제 11.11: HEAD 메서드일 경우 바디 전송할 필요 없음 */
  if (strcasecmp(method, "HEAD") == 0)
    return;

  /* 클라이언트에게 응답 바디 전송 */
  srcfd =
      Open(filename, O_RDONLY, 0); // 읽기 위해 filename 오픈하고 식별자 얻어옴
  // srcp = Mmap(0, filesize, PROT_READ, MAP_PRIVATE, srcfd, 0); // 요청 파일을
  // 가상메모리 영역으로 매핑 Close(srcfd); // 매핑됐으면 더 이상 식별자가
  // 필요없으므로 파일 닫기 Rio_writen(fd, srcp, filesize); // 클라이언트에게
  // 파일 전송 Munmap(srcp, filesize);                                     //
  // 매핑된 가상메모리주소 반환

  /* 숙제 11.9: Mmap 대신 malloc 사용하기 */
  srcp = (char *)malloc(filesize);
  rio_readn(srcfd, srcp, filesize);
  close(srcfd);
  rio_writen(fd, srcp, filesize);
  free(srcp);
}

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
    strcpy(filetype, "video/mp4"); // 숙제 11.7번 풀이 코드
  else
    strcpy(filetype, "text/plain");
}

void serve_dynamic(int fd, char *filename, char *cgiargs, char *method) {
  char buf[MAXLINE], *emptylist[] = {NULL};

  /* 클라이언트의 성공을 알려주는 응답 라인 보내기 */
  sprintf(buf, "HTTP/1.0 200 OK\r\n");
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Server: Tiny Web Server\r\n");
  Rio_writen(fd, buf, strlen(buf));

  if (Fork() == 0) { // 새로운 자식 프로세스 fork()
    setenv("QUERY_STRING", cgiargs,
           1); // QUERY_STRING 환경변수를 요청 URI의 CGI 인자들로 초기화
    Dup2(fd, STDOUT_FILENO); // 자식의 표준 출력을 연결 파일 식별자로 재지정
    Execve(filename, emptylist, environ); // CGI 프로그램 로드 및 실행
  }
  Wait(NULL); // 자식이 종료되어 정리되는 것을 기다리기 위해 wait함수에서 블록
}

void clienterror(int fd, char *cause, char *errnum, char *shortmsg,
                 char *longmsg) {
  char buf[MAXLINE], body[MAXBUF];

  /* http 응답 body 생성*/
  sprintf(body, "<html><title>Tiny Error</title>");
  sprintf(body,
          "%s<body bgcolor = "
          "ffffff"
          ">\r\n",
          body);
  sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
  sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
  sprintf(body, "%s<hr><em>The Tiny Web server</em>\r\n", body);

  /* http 응답 body 출력 */
  sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Content-type: text/html\r\n");
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));
  Rio_writen(fd, buf, strlen(buf));
  Rio_writen(fd, body, strlen(body));
}