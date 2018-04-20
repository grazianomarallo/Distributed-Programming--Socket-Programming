#include     <stdlib.h>
#include     <string.h>
#include     <inttypes.h>
#include     "errlib.h"
#include     "sockwrap.h"
#include     <rpc/xdr.h>

#define BUFLEN	128 /* BUFFER LENGTH */

/* FUNCTION PROTOTYPES */
int mygetline(char * line, size_t maxline, char *prompt);
void showAddr(char *str, struct sockaddr_in *a);
int iscloseorstop(char *buf);
int proto(int);

/* GLOBAL VARIABLES */
char *prog_name;

int main(int argc, char *argv[]) {
    uint16_t tport_n, tport_h;	/* server port number (net/host ord) */

    int s, result;
    struct sockaddr_in	saddr;		/* server address structure */
    struct in_addr	sIPaddr; 	/* server IP addr. structure */

    prog_name = argv[0];
    
    if(argc < 3) {
        err_quit ("usage: %s <IPv4 address> <port>", prog_name);
    }

    /* input IP address and port of server */
    result = inet_aton(argv[1], &sIPaddr);      // convert it to internet standard
    if(!result)
        err_quit("Invalid address");

    if(sscanf(argv[2], "%" SCNu16, &tport_h) != 1)  //SCNu16 = decimal int format for uint16_t
        err_quit("Invalid port number");
    tport_n = htons(tport_h);

    /* create the socket */
    printf("Creating socket\n");
    s = Socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    printf("done. Socket fd number: %d\n", s);

    /* prepare address structure */
    bzero(&saddr, sizeof(saddr));
    saddr.sin_family = AF_INET;
    saddr.sin_port   = tport_n;
    saddr.sin_addr   = sIPaddr;

    /* connect */
    showAddr("Connecting to target address", &saddr);
    Connect(s, (struct sockaddr *) &saddr, sizeof(saddr));
    printf("done.\n");

    /* main client body */
    if(!proto(s))
        exit(1);
    
	close(s);
	exit(0);
}

int proto(int s) {
    char buf[BUFLEN];		/* transmission buffer */
    char rbuf[BUFLEN];      	/* reception buffer */
    
    XDR	xdrs_in, xdrs_out;
    size_t	len;
    int i, input, output, result;
    
    //xdr
    xdrmem_create(&xdrs_out, buf, BUFLEN, XDR_ENCODE);
    
    //read two integers and append them to the xdr packet
    for(i = 0; i < 2; i++) {
        printf("Enter integer: ");
        scanf("%d", &output);
        if(!xdr_int(&xdrs_out, &output)) {
            xdr_destroy(&xdrs_out);
            close(s);
            return 0;
        }
    }
    
    //get xdr packet length and send it to the socket
    len = xdr_getpos(&xdrs_out);
    if(writen(s, buf, len) != len) {
        printf("Write error\n");
        xdr_destroy(&xdrs_out);
        close(s);
        return 0;
    }
    xdr_destroy(&xdrs_out);
    
    
    //packet has been sent, now wait for the response
    
    printf("waiting for response...\n");
    result = Readline(s, rbuf, BUFLEN);
    if (result <= 0) {
        printf("Read error/Connection closed\n");
        close(s);
        return 0;
    } else {
        xdrmem_create(&xdrs_in, rbuf, BUFLEN, XDR_DECODE);
        if(!xdr_int(&xdrs_in, &input)) {
            xdr_destroy(&xdrs_in);
            close(s);
            return 0;
        }
        xdr_destroy(&xdrs_in);
        
        printf("Result: %d\n", input);
    }
    
    return 1;
}

/* Gets a line of text from standard input after having printed a prompt string 
   Substitutes end of line with '\0'
   Empties standard input buffer but stores at most maxline-1 characters in the
   passed buffer
*/
int mygetline(char *line, size_t maxline, char *prompt) {
	char	ch;
	size_t 	i;

	printf("%s", prompt);
	for (i = 0; i < maxline-1 && (ch = getchar()) != '\n' && ch != EOF; i++)
		*line++ = ch;
	*line = '\0';
    // let's empty the standard input
	while (ch != '\n' && ch != EOF)
		ch = getchar();
	if (ch == EOF)
		return(EOF);
	else
        return(1);
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

/* Checks if the content of buffer buf equals the "close" o "stop" line */
int iscloseorstop(char *buf) {
	return (!strcmp(buf, "close\n") || !strcmp(buf, "stop\n"));
}
