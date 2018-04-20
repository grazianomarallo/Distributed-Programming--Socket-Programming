#include     <stdlib.h>
#include     <string.h>
#include     <inttypes.h>
#include     "errlib.h"
#include     "sockwrap.h"

#define MAXLENGTH	32 /* BUFFER LENGTH */
#define TIMEOUT 3  /* TIMEOUT (seconds) */

/* FUNCTION PROTOTYPES */
void showAddr(char *str, struct sockaddr_in *a);

/* GLOBAL VARIABLES */
char *prog_name;


int main(int argc, char *argv[]) {
    char rbuf[MAXLENGTH];      /* reception buffer */

    uint16_t tport_n, tport_h;	/* server port number (net/host ord) */

    int s;  // socket
    struct sockaddr_in	saddr;		/* server address structure */
    struct in_addr sIPaddr; 	/* server IP addr. structure */
    size_t len;

    prog_name = argv[0];

    // IP ADDRESS
    if(!inet_aton(argv[1], &sIPaddr))
        err_quit("Invalid address");

    // PORT NUMBER
    if(sscanf(argv[2], "%" SCNu16, &tport_h) != 1)  //SCNu16 = decimal int format for uint16_t
        err_quit("Invalid port number");
    tport_n = htons(tport_h);
    
    // ARGUMENT
    len = strlen(argv[3]);
    if(len > MAXLENGTH)
        err_quit("Invalid argument: maxlength exceeded");
    
    /* create the socket */
    printf("Creating udp socket\n");
    s = Socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    printf("done. Socket fd number: %d\n", s);

    /* prepare address structure */
    bzero(&saddr, sizeof(saddr));
    saddr.sin_family = AF_INET;
    saddr.sin_port = tport_n;
    saddr.sin_addr = sIPaddr;

    /* main client body */
    size_t n;
    int counter = 0;
    struct sockaddr_in from;
    socklen_t fromlen;
    fd_set cset;
    struct timeval tval;
    // try sending the message up to 5 times
    do {
        n = sendto(s, argv[3], len, 0, (struct sockaddr *) &saddr, sizeof(saddr));
        if (n != len)
            printf("Write error\n");
    
        printf("waiting for response...\n");
        FD_ZERO(&cset);
        FD_SET(s, &cset);
        tval.tv_sec = TIMEOUT;
        tval.tv_usec = 0;
        n = Select(FD_SETSIZE, &cset, NULL, NULL, &tval);
        if (n > 0) {
            /* receive datagram */
            fromlen = sizeof(struct sockaddr_in);
            n = recvfrom(s, rbuf, MAXLENGTH-1, 0, (struct sockaddr *)&from, &fromlen);
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


/* Utility function to display a string str
   followed by an internet address a, written
   in decimal notation
*/
void showAddr(char *str, struct sockaddr_in *a) {
    char *p;
    
    p = inet_ntoa(a->sin_addr);
	printf("%s %s",str,p);
	printf("!%u\n", ntohs(a->sin_port));
}
