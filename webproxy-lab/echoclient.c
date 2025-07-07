#include "csapp.h"

int main(int argc, char **argv) { // echoclient <host 주소> <post 주소>
    int clientfd;
    char *host, *port, buf[MAXLINE];
    rio_t rio;

    if (argc != 3) {
        fprintf(stderr, "usage: %s <host> <port>\n", argv[0]);
        exit(0);
    }
    host = argv[1];
    port = argv[2];

    clientfd = Open_clientfd(host, port); // 서버 연결
    Rio_readinitb(&rio, clientfd); // rio buffer 초기화

    while (Fgets(buf, MAXLINE, stdin) != NULL) { // 
        Rio_writen(clientfd, buf, strlen(buf)); // 서버로 전송
        Rio_readlineb(&rio, buf, MAXLINE); // 서버의 응답을 읽음
        Fputs(buf, stdout); // 다시 출력
    }
    Close(clientfd); 
    exit(0);

}