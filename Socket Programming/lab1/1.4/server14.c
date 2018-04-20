#include    <stdlib.h>
#include    <string.h>
#include    <inttypes.h>
#include    "errlib.h"
#include    "sockwrap.h"

#define BUFLEN	65536 /* Maximum UDP datagram length */

/* FUNCTION PROTOTYPES */
void showAddr(char *str, struct sockaddr_in *a);

/* GLOBAL VARIABLES */
char *prog_name;

int main(int argc, char *argv[]) {
    int s;  // socket
    uint16_t portn, porth;
    struct sockaddr_in saddr, from;
    socklen_t fromlen;
    char buf[BUFLEN];
    int n;
    
    prog_name = argv[0];
    
    // PORT NUMBER
    if(sscanf(argv[1], "%" SCNu16, &porth) != 1)  //SCNu16 = decimal int format for uint16_t
        err_quit("Invalid port number");
    portn = htons(porth);
    
    
    bzero(&saddr, sizeof(saddr));
    saddr.sin_family = AF_INET;
    saddr.sin_port = portn;
    saddr.sin_addr.s_addr = INADDR_ANY;
    
    s = Socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    printf("Socket created: %d\n", s);
    
    Bind(s, (struct sockaddr *) &saddr, sizeof(saddr));
    
    while(1) {
        printf("Waiting for data from client\n");
        fromlen = sizeof(struct sockaddr_in);
        n = recvfrom(s, buf, BUFLEN-1, 0, (struct sockaddr *) &from, &fromlen);
        printf("Data received: length = %d\n", n);
        if(n != -1) {
            buf[n] = '\0';
            sendto(s, buf, n, 0, (struct sockaddr *) &from, sizeof(struct sockaddr));
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
