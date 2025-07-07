#include "csapp.h"

void echo(int connfd)
{
    size_t n;
    char buf[MAXLINE];
    rio_t rio;

    Rio_readinitb(&rio, connfd); // rio read buffer 초기화, 해당하는 descriptor는 connfd로 지정
    while((n = Rio_readlineb(&rio, buf, MAXLINE)) != 0) { // rio read buffer에서 buf로 한 줄씩 읽음
        printf("server received %d bytes\n", (int)n); // 매 줄마다 읽은 바이트 수 출력 (다 읽을 때까지)
        Rio_writen(connfd, buf, n); // buf에서 rio로 매 줄마다 복사 (읽어들인 대로 다시 보냄 -> 그러니까 메아리. echo)
    }

}