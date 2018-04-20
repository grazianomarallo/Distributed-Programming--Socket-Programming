#include     <stdlib.h>
#include     <string.h>
#include     <inttypes.h>
#include     "errlib.h"
#include     "sockwrap.h"

#define BUFLEN	128 /* BUFFER LENGTH */

/* FUNCTION PROTOTYPES */
int mygetline(char * line, size_t maxline, char *prompt);
void showAddr(char *str, struct sockaddr_in *a);
int iscloseorstop(char *buf);

/* GLOBAL VARIABLES */
char *prog_name;


int main(int argc, char *argv[]) {
    char buf[BUFLEN];		/* transmission buffer */
    char rbuf[BUFLEN];      /* reception buffer */

    uint16_t tport_n, tport_h;	/* server port number (net/host ord) */

    int		   s;
    int		   result, res;
    struct sockaddr_in	saddr;		/* server address structure */
    struct in_addr	sIPaddr; 	/* server IP addr. structure */


    prog_name = argv[0];

    /* input IP address and port of server */
    //ip
    result = inet_aton(argv[1], &sIPaddr);      // convert it to internet standard
    if(!result)
        err_quit("Invalid address");
    
    //port
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
    size_t	len;
    mygetline(buf, BUFLEN, "Enter two decimal integers: ");
    strcat(buf,"\r\n");
    len = strlen(buf);
    Write(s, buf, len);
    
    printf("waiting for response...\n");
    result = Readline(s, rbuf, BUFLEN);
    if (result <= 0) {
        printf("Read error/Connection closed\n");
        close(s);
        exit(1);
    }
    else if(sscanf(buf, "%d", &res) == 1) {
        printf("Sum: %d", res);
    } else {
        printf("Message: %s", rbuf);
    }
        
	printf("===========================================================\n");
    
	close(s);
	exit(0);
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
void showAddr(char *str, struct sockaddr_in *a)
{
    char *p;
    
    p = inet_ntoa(a->sin_addr);
	printf("%s %s",str,p);
	printf("!%u\n", ntohs(a->sin_port));
}

/* Checks if the content of buffer buf equals the "close" o "stop" line */
int iscloseorstop(char *buf)
{
	return (!strcmp(buf, "close\n") || !strcmp(buf, "stop\n"));
}
