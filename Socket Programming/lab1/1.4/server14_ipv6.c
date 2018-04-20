#include    <stdlib.h>
#include    <string.h>
#include    <inttypes.h>
#include    "errlib.h"
#include    "sockwrap.h"

#define BUFLEN	65536 /* Maximum UDP datagram length */

/* FUNCTION PROTOTYPES */
void showAddr(char *str, struct sockaddr_in *a);
int Udp_server(const char *host, const char *serv, socklen_t *addrlenp);

/* GLOBAL VARIABLES */
char *prog_name;

int main(int argc, char *argv[]) {
    int s;  // socket
    struct sockaddr_storage from;
    socklen_t fromlen;
    char buf[BUFLEN];
    int n;
    
    prog_name = argv[0];
    
    // PORT NUMBER
    s = Udp_server (NULL, argv[1], NULL);
    printf("Socket created: %d\n", s);
    
    while(1) {
        printf("Waiting for data from client\n");
        fromlen = sizeof(from);
        n = recvfrom(s, buf, BUFLEN-1, 0, (struct sockaddr *) &from, &fromlen);
        printf("Data received: length = %d\n", n);
        if(n != -1) {
            buf[n] = '\0';
            sendto(s, buf, n, 0, (struct sockaddr *) &from, fromlen);
        }
    }
    
}


/* Utility function to display a string str
   followed by an internet address a, written
   in decimal notation
*/
void showAddr(char *str, struct sockaddr_in *a)
{
    char *p;
    
    p = inet_ntoa(a->sin_addr);
	printf("%s %s",str,p);
	printf("!%u\n", ntohs(a->sin_port));
}




/*
 * portable udp server
 * loop until it is not found a valid socket to connect
 * by considering hints specified. NB: it will connect only to one among ipv4 - ipv6 addresses
 * force it by changing the ai_family hint
 * or you will not know to which address it will be binded
 */
int
udp_server(const char *host, const char *serv, socklen_t *addrlenp)
{
    int				sockfd, n;
    struct addrinfo	hints, *res, *ressave;
    
    bzero(&hints, sizeof(struct addrinfo));
    hints.ai_flags = AI_PASSIVE;
    hints.ai_family = AF_UNSPEC; // Use AF_INET for IPV4 and AF_INET6 for IPV6
    hints.ai_socktype = SOCK_DGRAM;
    
    if ( (n = getaddrinfo(host, serv, &hints, &res)) != 0)
        err_quit("udp_server error for %s, %s: %s",
                 host, serv, gai_strerror(n));
    ressave = res;
    
    do {
        sockfd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
        if (sockfd < 0)
            continue;		/* error - try next one */
        
        if (bind(sockfd, res->ai_addr, res->ai_addrlen) == 0)
            break;			/* success */
        
        Close(sockfd);		/* bind error - close and try next one */
    } while ( (res = res->ai_next) != NULL);
    
    if (res == NULL)	/* errno from final socket() or bind() */
        err_sys("udp_server error for %s, %s", host, serv);
    
    if (addrlenp)
        *addrlenp = res->ai_addrlen;	/* return size of protocol address */
    
    freeaddrinfo(ressave);
    
    return(sockfd);
}
/* end udp_server */

int
Udp_server(const char *host, const char *serv, socklen_t *addrlenp)
{
    return(udp_server(host, serv, addrlenp));
}
