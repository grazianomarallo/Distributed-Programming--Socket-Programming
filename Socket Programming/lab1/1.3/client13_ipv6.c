#include     <stdlib.h>
#include     <string.h>
#include     <inttypes.h>
#include     "errlib.h"
#include     "sockwrap.h"

#define BUFLEN	128 /* BUFFER LENGTH */

/* FUNCTION PROTOTYPES */
int tcp_connect(const char *host, const char *serv);

/* GLOBAL VARIABLES */
char *prog_name;


int main(int argc, char *argv[]) {
    char buf[BUFLEN];		/* transmission buffer */
    char rbuf[BUFLEN];      /* reception buffer */

    int s, op1, op2;
    int	result, res;

    prog_name = argv[0];

    s = tcp_connect(argv[1], argv[2]);

    /* main client body */
    printf("Please insert the first operand to send to the server: ");
    scanf("%d", &op1);
    printf("Please insert the second operand to send to the server: ");
    scanf("%d", &op2);
    sprintf(buf, "%d %d\r\n", op1, op2);
    
    Write(s, buf, strlen(buf));
    
    result = Readline(s, rbuf, BUFLEN);
    if (result <= 0) {
        printf("Read error/Connection closed\n");
        close(s);
        exit(1);
    }
    else if(sscanf(rbuf, "%d", &res) == 1) {
        printf("Sum: %d\n", res);
    } else {
        printf("Message: %s\n", rbuf);
    }
    
	close(s);
	exit(0);
}


int tcp_connect(const char *host, const char *serv) {
    int sockfd, n;
    struct addrinfo hints, *res, *ressave;
    
    bzero(&hints, sizeof(struct addrinfo));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    
    if( (n = getaddrinfo(host, serv, &hints, &res)) != 0 ) {
        err_quit("tcp_connect error for %s, %s: %s", host, serv, gai_strerror(n));
    }
    ressave = res;
    
    do {
        // s = Socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        sockfd = Socket(res->ai_family, res->ai_socktype, res->ai_protocol);
        if(sockfd < 0)
            continue;
        
        if(connect(sockfd, res->ai_addr, res->ai_addrlen) == 0)
            break; //success!
        
        Close(sockfd);
    } while( (res = res->ai_next) != NULL);
    
    if(res == NULL)
        err_sys("tcp_connect error for %s, %s", host, serv);
    
    freeaddrinfo(ressave);
    return sockfd;
}
