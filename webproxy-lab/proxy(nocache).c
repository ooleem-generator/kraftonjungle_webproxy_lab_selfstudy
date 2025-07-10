#include <stdio.h>
#include "csapp.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr =
    "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 "
    "Firefox/10.0.3\r\n";

void *thread(void *vargp);
void doit_proxy(int conn_client_fd);
void parse_uri(char *uri, char *host, char *port, char *new_uri);
void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg);


int main(int argc, char **argv) // 명령줄 인자로 전달받은 포트(4500)에서 연결 요청 수신
{
  int listenfd, *conn_client_fdp;
  char hostname[MAXLINE], port[MAXLINE];
  socklen_t clientlen;
  struct sockaddr_storage clientaddr;
  pthread_t tid;

  if (argc != 2)
  {
    fprintf(stderr, "usage: %s <port>\n", argv[0]);
    exit(1);
  }

  listenfd = Open_listenfd(argv[1]); // 전달받은 4500번 포트에서 리스닝 소켓 열기

  while (1) // 일반적인 무한 서버 루프
  {
    clientlen = sizeof(clientaddr);
    conn_client_fdp = Malloc(sizeof(int));
    *conn_client_fdp = Accept(listenfd, (SA *)&clientaddr, &clientlen); // 연결 수락 후 connfd로 descriptor 반환
    Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE, 0); // 클라이언트 정보가 hostname과 port에 각각 담김
    printf("Accepted connection from (%s, %s)\n", hostname, port);
    Pthread_create(&tid, NULL, thread, conn_client_fdp);
    
  }

}

void *thread(void *vargp)
{
  int conn_client_fd = *((int *)vargp);
  Pthread_detach(pthread_self());
  Free(vargp);
  doit_proxy(conn_client_fd);
  Close(conn_client_fd); // 연결 종료
  return NULL;
}

void doit_proxy(int conn_client_fd) // 요청 한 번당 다시 서버에 보내고, 받아서 클라이언트에 전송해야 함
{
  int n;
  int content_length;
  char *ptr; // 헤더 파싱할때 쓸 포인터
  int conn_server_fd; // 서버랑 통신할때쓰는 fd
  char original_buf[MAXLINE]; // 클라이언트로부터 받은 원본 헤더용 buffer
  char modify_buf[MAXLINE]; // 프록시에서 수정한 헤더 저장 및 발송용 buffer
  char response_buf[MAXLINE]; // 서버로부터 수신한 응답 저장 및 발송용 buffer
  char responsetoclient_buf[MAXLINE];
  char *responsebody_buf;
  char method[MAXLINE], uri[MAXLINE], version[MAXLINE]; // 헤더 맨 첫 줄에서 각각 저장 
  char host[MAXLINE], port[MAXLINE], new_uri[MAXLINE]; // parse_uri에서 받아올 예정
  rio_t rio_request; // request용 rio 구조체
  rio_t rio_response; // response용 rio 구조체

  /* Read request line and headers */
  Rio_readinitb(&rio_request, conn_client_fd); // request 수신용 rio 초기화
  //*original_buf = " ";

  // 일단 헤더 첫 줄 읽고 GET 이외의 메서드 필터링 // 
  Rio_readlineb(&rio_request, original_buf, MAXLINE); 
  printf("Request headers:\n");
  printf("%s", original_buf);
  
  sscanf(original_buf, "%s %s %s", method, uri, version);
  
  if (strcasecmp(method, "GET")) { // GET 메서드 이외의 다른 메서드가 오면, 
    clienterror(conn_client_fd, method, "501", "Not implemented", "Proxy does not implement this method"); // clienterror 함수를 통해 501 오류 응답을 보냄
    return;
  }
  
  parse_uri(uri, host, port, new_uri); // URI를 host, port, 뒤에 붙은 새로운 new_uri 문자열로 파싱

  conn_server_fd = Open_clientfd(host, port); // 파싱한 정보를 바탕으로 서버랑 연결하는 fd 새로 오픈
  Rio_readinitb(&rio_response, conn_server_fd); // response 수신용 rio 초기화


  /* 여기서부터 클라이언트 헤더 수정 */
  sprintf(modify_buf, "GET %s HTTP/1.0\r\n", new_uri); // 수정된 헤더 첫 줄 저장

  while(strcmp(original_buf, "\r\n")) {
    Rio_readlineb(&rio_request, original_buf, MAXLINE); // original_buf로 온 원본 헤더 한 줄씩 읽음
    if ((ptr = strstr(original_buf, "Host:")) == original_buf) {
      sprintf(modify_buf, "%sHost: %s\r\n", modify_buf, host);
    } 
    else if ((ptr = strstr(original_buf, "User-Agent:")) == original_buf) {
      sprintf(modify_buf, "%s%s", modify_buf, user_agent_hdr);
    }
    else if ((ptr = strstr(original_buf, "Connection:")) == original_buf) {
      sprintf(modify_buf, "%sConnection: close\r\n", modify_buf);
    }
    else if ((ptr = strstr(original_buf, "Proxy-Connection:")) == original_buf) {
      sprintf(modify_buf, "%sProxy-Connection: close\r\n", modify_buf);
    }
    else {
      sprintf(modify_buf, "%s%s", modify_buf, original_buf);
    }
  }
  //*original_buf = "";
  Rio_writen(conn_server_fd, modify_buf, strlen(modify_buf)); // 완성된 수정 헤더 발송
  printf("Modified Request headers:\n");
  printf("%s", modify_buf); // 어떻게 보냈는지 확인

  /* 여기서부터 서버 응답 헤더 수신, 클라이언트로 발신 */

  while (strcmp(response_buf, "\r\n")) {
    Rio_readlineb(&rio_response, response_buf, MAXLINE);
    if ((ptr = strstr(response_buf, "Content-length")) == response_buf) {
      sscanf(response_buf, "%*s %d", &content_length);
    }  

    sprintf(responsetoclient_buf, "%s%s", responsetoclient_buf, response_buf);
  }
  //*response_buf = "";
  Rio_writen(conn_client_fd, responsetoclient_buf, strlen(responsetoclient_buf));
  printf("Response headers:\n");
  printf("%s", responsetoclient_buf);

  // /* 여기서부터 서버 응답 본문 수신, 클라이언트로 발신 */

  responsebody_buf = malloc(content_length);
  Rio_readnb(&rio_response, responsebody_buf, content_length);
  Rio_writen(conn_client_fd, responsebody_buf, content_length);
  free(responsebody_buf);

  Close(conn_server_fd);

}


void parse_uri(char *uri, char *host, char *port, char *new_uri)
{
  char *ptr = uri;
  char *protocol = "://";

  if ((ptr = strstr(uri, protocol)) != NULL) { // '://' 이 있는가없는가 확인
    ptr = ptr+3;
  } else {
    ptr = uri;
  }

  char *slash_ptr = strchr(ptr, '/'); // 
  if (slash_ptr) {
    *slash_ptr = '\0';
    slash_ptr++;
    strcpy(new_uri, "/");
    strcat(new_uri, slash_ptr);
  } else {
    strcpy(new_uri, "/"); // uri 뒤에 /가 안붙어있는 경우
  }

  char *colon_ptr = strchr(ptr, ':');
  if (colon_ptr) {
    *colon_ptr = '\0';
    colon_ptr++;
    strcpy(port, colon_ptr);
  } else {
    port = NULL;
  }
  strcpy(host, ptr);

}


void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg)
{
  char buf[MAXLINE], body[MAXBUF];

  /* Build the HTTP response body */
  sprintf(body, "<html><title>Proxy Error</title>");
  sprintf(body, "%s<body bgcolor=""ffffff"">\r\n", body);
  sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
  sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
  sprintf(body, "%s<hr><em>The Proxy Web Server</em>\r\n", body);

  /* Print the HTTP response - 응답은 rio_writen*/
  sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Content-type: text/html\r\n");
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));
  Rio_writen(fd, buf, strlen(buf));
  Rio_writen(fd, body, strlen(body));

}














// ---------------- tiny.c --------------------------------------

// int main(int argc, char **argv) // 명령줄 인자로 전달받은 포트에서 연결 요청 수신
// {
//   int listenfd, connfd;
//   char hostname[MAXLINE], port[MAXLINE];
//   socklen_t clientlen;
//   struct sockaddr_storage clientaddr;

//   /* Check command line args */
//   if (argc != 2)
//   {
//     fprintf(stderr, "usage: %s <port>\n", argv[0]);
//     exit(1);
//   }

//   listenfd = Open_listenfd(argv[1]); // 리스닝 소켓 열기

//   while (1) // 일반적인 무한 서버 루프
//   {
//     clientlen = sizeof(clientaddr);
//     connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen); // line:netp:tiny:accept, 연결 수락 후 connfd로 descriptor 반환
//     Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE, 0); // 
//     printf("Accepted connection from (%s, %s)\n", hostname, port);
//     doit(connfd);  // line:netp:tiny:doit, HTTP 트랜젝션 수행
//     Close(connfd); // line:netp:tiny:close, 연결 종료
//   }
// }

// void doit(int fd)
// {
//   int is_static;
//   struct stat sbuf;
//   char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
//   char filename[MAXLINE], cgiargs[MAXLINE];
//   rio_t rio;

//   /* Read request line and headers */
//   Rio_readinitb(&rio, fd);
//   Rio_readlineb(&rio, buf, MAXLINE);
//   printf("Request headers:\n");
//   printf("%s", buf);
//   sscanf(buf, "%s %s %s", method, uri, version);
//   if (strcasecmp(method, "GET")) { // GET 메서드 이외의 다른 메서드가 오면, 
//     clienterror(fd, method, "501", "Not implemented", "Tiny does not implement this method"); // clienterror 함수를 통해 501 오류 응답을 보냄
//     return;
//   }
//   read_requesthdrs(&rio); // 요청 라인 이외의 HTTP 헤더는 무시

//   /* Parse URI from GET request */
//   is_static = parse_uri(uri, filename, cgiargs); // URI를 파일 이름과 CGI 인자 문자열로 파싱, 정적/동적 콘텐츠 여부 플래그 설정
//   if (stat(filename, &sbuf) < 0) { // 요청된 파일이 존재하는지 체크
//     clienterror(fd, filename, "404", "Not found", "Tiny couldn't find this file"); // 존재하지 않으면 즉시 404 오류 메세지 출력
//     return;
//   }

//   if (is_static) { // 정적 콘텐츠 요청일 경우
//     if (!(S_ISREG(sbuf.st_mode)) || !(S_IRUSR & sbuf.st_mode)) { // 파일이 일반 파일인지, 읽기 권한이 있는지 확인
//       clienterror(fd, filename, "403", "Forbidden", "Tiny couldn't read this file"); // 권한이 없는 경우 403 오류 메세지 출력
//       return;
//     }
//     serve_static(fd, filename, sbuf.st_size); // 조건이 충족되면 serve_static 호출
//   }
//   else {
//     if (!(S_ISREG(sbuf.st_mode)) || !(S_IXUSR & sbuf.st_mode)) { // 파일이 일반 파일인지, 실행 권한이 있는지 확인
//       clienterror(fd, filename, "403", "Forbidden", "Tiny couldn't run the CGI program"); // 권한이 없는 경우 403 오류 메세지 출력
//       return;
//     }
//     serve_dynamic(fd, filename, cgiargs); // 조건이 충족되면 serve_dynamic 호출
//   }
// }

// void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg)
// {
//   char buf[MAXLINE], body[MAXBUF];

//   /* Build the HTTP response body */
//   sprintf(body, "<html><title>Tiny Error</title>");
//   sprintf(body, "%s<body bgcolor=""ffffff"">\r\n", body);
//   sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
//   sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
//   sprintf(body, "%s<hr><em>The Tiny Web Server</em>\r\n", body);

//   /* Print the HTTP response - 응답은 rio_writen*/
//   sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
//   Rio_writen(fd, buf, strlen(buf));
//   sprintf(buf, "Content-type: text/html\r\n");
//   Rio_writen(fd, buf, strlen(buf));
//   sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));
//   Rio_writen(fd, buf, strlen(buf));
//   Rio_writen(fd, body, strlen(body));

// }

// void read_requesthdrs(rio_t *rp) 
// {
//   char buf[MAXLINE];

//   Rio_readlineb(rp, buf, MAXLINE);
//   while(strcmp(buf, "\r\n")) {
//     Rio_readlineb(rp, buf, MAXLINE);
//     printf("%s", buf);
//   }
//   return;

// }

// int parse_uri(char *uri, char *filename, char *cgiargs)
// {
//   char *ptr;

//   if(!strstr(uri, "cgi-bin")) {  /* Static content */
//     strcpy(cgiargs, "");
//     strcpy(filename, ".");
//     strcat(filename, uri);
//     if (uri[strlen(uri)-1] == '/')
//       strcat(filename, "home.html");
//     return 1;
//   }
//   else {  /* Dynamic content */
//     ptr = index(uri, '?');
//     if (ptr) {
//       strcpy(cgiargs, ptr+1);
//       *ptr = '\0';
//     }
//     else {
//       strcpy(cgiargs, "");
//     }
//     strcpy(filename, ".");
//     strcat(filename, uri);
//     return 0;
//   }
// }

// void serve_static(int fd, char *filename, int filesize)
// {
//   int srcfd;
//   char *srcp, filetype[MAXLINE], buf[MAXBUF];

//   /* Send response headers to client */
//   get_filetype(filename, filetype);
//   sprintf(buf, "HTTP/1.0 200 OK\r\n");
//   sprintf(buf, "%sServer: Tiny Web Server\r\n", buf);
//   sprintf(buf, "%sConnection: Close\r\n", buf);
//   sprintf(buf, "%sContent-length: %d\r\n", buf, filesize);
//   sprintf(buf, "%sContent-type: %s\r\n\r\n", buf, filetype);
//   Rio_writen(fd, buf, strlen(buf));
//   printf("Response headers:\n");
//   printf("%s", buf);

//   /* Send response body to client */
//   // srcfd = Open(filename, O_RDONLY, 0); // Open()으로 파일 열기
//   // srcp = Mmap(0, filesize, PROT_READ, MAP_PRIVATE, srcfd, 0); // 파일을 메모리에 매핑 (가상 메모리의 접근)
//   // Close(srcfd); // 파일 디스크립터 닫기
//   // Rio_writen(fd, srcp, filesize); // 매핑된 메모리에서 fd로 직접 전송
//   // Munmap(srcp, filesize); // 매핑 해제 (메모리 누수 방지)

//   // Homework Problem 11.10
//   srcfd = Open(filename, O_RDONLY, 0);
//   srcp = malloc(filesize);
//   Rio_readn(srcfd, srcp, filesize);
//   Close(srcfd);
//   Rio_writen(fd, srcp, filesize);
//   free(srcp);

// }

// /* get_filetype - Derive file type from filename */
// void get_filetype(char *filename, char *filetype)
// {
//   if (strstr(filename, ".html"))
//     strcpy(filetype, "text/html");
//   else if (strstr(filename, ".gif"))
//     strcpy(filetype, "image/gif");
//   else if (strstr(filename, ".png"))
//     strcpy(filetype, "image/png");
//   else if (strstr(filename, ".jpg"))
//     strcpy(filetype, "image/jpg");
//   else if (strstr(filename, ".mp4"))
//     strcpy(filetype, "application/mp4");
//   else
//     strcpy(filetype, "text/plain");
// }

// void serve_dynamic(int fd, char *filename, char *cgiargs)
// {
//   char buf[MAXLINE], *emptylist[] = { NULL };

//   /* Return first part of HTTP response */
//   sprintf(buf, "HTTP/1.0 200 OK\r\n");
//   Rio_writen(fd, buf, strlen(buf));
//   sprintf(buf, "Server: Tiny Web Server\r\n");
//   Rio_writen(fd, buf, strlen(buf));

//   if (Fork() == 0) {  /* Child */
//     /* Real server would set all CGI vars here */
//     setenv("QUERY_STRING", cgiargs, 1);
//     Dup2(fd, STDOUT_FILENO);                /* Redirect stdout to client */
//     Execve(filename, emptylist, environ);   /* Run CGI program */
//   }
//   Wait(NULL);   /* Parent waits for and reaps child */
  
// }    


