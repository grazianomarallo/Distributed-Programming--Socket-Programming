#include     <stdlib.h>
#include     <string.h>
#include     <inttypes.h>
#include     "errlib.h"
#include     "sockwrap.h"

#define BUFLEN	128 /* BUFFER LENGTH */
#define TIMEOUT 5

/* FUNCTION PROTOTYPES */
void showAddr(char *str, struct sockaddr_storage *from);
int Udp_client(const char *host, const char *serv, struct sockaddr **saptr, socklen_t *lenptr);

/* GLOBAL VARIABLES */
char *prog_name;


int main(int argc, char *argv[]) {
    char buf[BUFLEN];		/* transmission buffer */
    char rbuf[BUFLEN];      /* reception buffer */

    int n, s, op1, op2;
    int	result;
    
    struct sockaddr_storage *sa;
    socklen_t salen;
    
    prog_name = argv[0];
  
    s = Udp_client(argv[1], argv[2], (struct sockaddr **) &sa, &salen);
    
	printf("Socket connected\n");
	
    /* main client body */
    int counter = 0;
    struct sockaddr_storage from;
    socklen_t fromlen;
    fd_set cset;
    struct timeval tval;
    
    
    // try sending the message up to 5 times
    do {
        n = sendto(s, argv[3], strlen(argv[3]), 0, (struct sockaddr *) sa, salen);
        if (n != strlen(argv[3]))
            printf("Write error\n");
        
        printf("waiting for response...\n");
        FD_ZERO(&cset);
        FD_SET(s, &cset);
        tval.tv_sec = TIMEOUT;
        tval.tv_usec = 0;
        n = Select(FD_SETSIZE, &cset, NULL, NULL, &tval);
        if (n > 0) {
            /* receive datagram */
            fromlen = sizeof(struct sockaddr_storage);
            n = recvfrom(s, rbuf, BUFLEN-1, 0, (struct sockaddr *)&from, &fromlen);
            if (n != -1) {
                rbuf[n] = '\0';
                showAddr("Received response from", &from);
                printf(": [%s]\n", rbuf);
            }
            else
                printf("Error in receiving response\n");
        }
        else counter++;
    } while(n == 0 && counter < 5);
    
    if(n == 0)
        printf("No response received after %d try\n", counter);
    
    printf("=======================================================\n");
    

	close(s);
	exit(0);
}


/* 
   Utility function to display a string str
   followed by an internet address a, written
   in decimal notation
*/
void showAddr(char *str, struct sockaddr_storage *from) {
    char *p;
    
    char buf[100];
    getnameinfo((struct sockaddr *)from, sizeof(struct sockaddr_storage), buf, sizeof(buf), NULL, 0, NI_NUMERICHOST);

	printf("%s %s",str,buf);
}




int
udp_client(const char *host, const char *serv, struct sockaddr **saptr, socklen_t *lenp) {
    int	sockfd, n;
    struct addrinfo	hints, *res, *ressave;
    
    bzero(&hints, sizeof(struct addrinfo));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;
    
    if ( (n = getaddrinfo(host, serv, &hints, &res)) != 0)
        err_quit("udp_client error for %s, %s: %s", host, serv, gai_strerror(n));
    ressave = res;
    
    do {
        sockfd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
        if (sockfd >= 0)
            break;		/* success */
    } while ( (res = res->ai_next) != NULL);
    
    if (res == NULL)	/* errno set from final socket() */
        err_sys("udp_client error for %s, %s", host, serv);
    
    *saptr = (struct sockaddr *) malloc(res->ai_addrlen);
    memcpy(*saptr, res->ai_addr, res->ai_addrlen);
    *lenp = res->ai_addrlen;
    
    freeaddrinfo(ressave);
    
    return(sockfd);
}
/* end udp_client */

int
Udp_client(const char *host, const char *serv, struct sockaddr **saptr, socklen_t *lenptr) {
    return(udp_client(host, serv, saptr, lenptr));
}
