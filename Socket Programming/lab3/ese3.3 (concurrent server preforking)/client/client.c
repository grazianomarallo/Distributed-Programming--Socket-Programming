#include     <stdlib.h>
#include     <string.h>
#include     <stdio.h>
#include     <inttypes.h>
#include     "errlib.h"
#include     "sockwrap.h"


#define BUFLEN	128 /* BUFFER LENGTH */
#define FBUFLEN 1024
#define OK      1
#define ERROR   -1

/* FUNCTION PROTOTYPES */
int mygetline(char * line, size_t maxline, char *prompt);
void showAddr(char *str, struct sockaddr_in *a);
int parsereply(char *);
void proto(int);
void proto_quit(int);
int proto_get(int, char *);

/* GLOBAL VARIABLES */
char *prog_name;

int main(int argc, char *argv[]) {
    uint16_t tport_n, tport_h;	/* server port number (net/host ord) */
    
    int s, result;
    struct sockaddr_in	saddr;		/* server address structure */
    struct in_addr	sIPaddr; 	/* server IP addr. structure */
    
    prog_name = argv[0];
    
    if(argc < 3) {
        err_quit ("usage: %s <IP address> <port>", prog_name);
    }
    
    /* input IP address and port of server */
    result = inet_aton(argv[1], &sIPaddr);
    if(!result)
        err_quit("Invalid address");
    
    if(sscanf(argv[2], "%" SCNu16, &tport_h) != 1)
        err_quit("Invalid port number");
    tport_n = htons(tport_h);
    
    /* create the socket */
    s = Socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    printf("Socket created. fd number: %d\n", s);
    
    /* prepare address structure */
    bzero(&saddr, sizeof(saddr));
    saddr.sin_family = AF_INET;
    saddr.sin_port   = tport_n;
    saddr.sin_addr   = sIPaddr;
    
    /* connect */
    showAddr("Connecting to target address", &saddr);
    Connect(s, (struct sockaddr *) &saddr, sizeof(saddr));
    
    /* main client body */
    proto(s);
    
    close(s);
    exit(0);
}


void proto(int s) {
    char buf[BUFLEN];		/* transmission buffer */
    
    while(1) {
        if(mygetline(buf, BUFLEN, "Insert filename (no GET), one per line (end with EOF - press CTRL+D on keyboard to enter EOF):\n") == EOF) {
            printf("Closing socket\n");
            proto_quit(s);
            break;
        }
        
        if(!proto_get(s, buf))
            break;
    }
}

void proto_quit(int s) {
    if(writen(s, "QUIT\r\n", sizeof(char)*6) != sizeof(char)*6) {
        printf("Write error\n");
    }
}

int proto_get(int s, char *buf) {
    size_t len;
    int n, result, tot, ret;
    char *sbuf, rbuf[FBUFLEN];
    uint32_t nlength, length, ntime, time;
    FILE *f;
    
    //send get request
    len = strlen(buf);
    sbuf = (char *) malloc(sizeof(char)*(6+len)); // 6 characters: 'GET \r\n' plus filename
    strncpy(sbuf, "GET ", 4);
    strncpy(sbuf+4, buf, len);
    strncpy(sbuf+4+len,"\r\n", 2);
    if(writen(s, sbuf, sizeof(char)*(6+len)) != sizeof(char)*(6+len)) {
        printf("Write error\n");
        return 0;
    }
    
    //wait for server response
    
    //protocol send +OK followed by file infos
    //let's read first 13 characters
    n = recv(s, rbuf, sizeof(char)*13, 0);
    if (n < 0) {
        printf("Read error\n");
        return 0;
    }
    else if (n == 0) {
        printf("Connection closed by party on socket %d\n",s);
        return 0;
    }
    else {
        if( (ret = parsereply(rbuf)) ==  OK) {
            tot = 0;
            
            memcpy(&nlength, rbuf+5*sizeof(char), 4*sizeof(char));
            length = ntohl(nlength);
            printf("Length: %u\n", length);
            memcpy(&ntime, rbuf+9*sizeof(char), 4*sizeof(char));
            time = ntohl(ntime);
            printf("Time: %u\n", time);
            
            
            f = fopen(buf, "wb");
            while(length > 0) {
                n = recv(s, rbuf, FBUFLEN, 0);
                if(fwrite(rbuf,sizeof(char),n, f) != n) {
                    printf("Error during file write\n");
                    return 0;
                }
                tot += n;
                if(tot >= length)
                    break;
            }
            
            fclose(f);
        }
    }
    
    return 1;
}


/* Gets a line of text from standard input after having printed a prompt string
 Substitutes end of line with '\0'
 Empties standard input buffer but stores at most maxline-1 characters in the
 passed buffer
 */
int mygetline(char *line, size_t maxline, char *prompt) {
    char ch;
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

int parsereply(char *rbuf) {
    char *subbuff = malloc(sizeof(char)*5);
    
    //compare first 5 chars with OK string
    strncpy(subbuff, rbuf, 5);
    if(!strcmp(subbuff, "+OK\r\n")) {
        printf("OK - file received\n");
        free(subbuff);
        return OK;
    }
    //compare first 6 chars with ERROR string
    subbuff = realloc(subbuff, sizeof(char)*6);
    strncpy(subbuff, rbuf, 6);
    if(!strcmp(subbuff, "-ERR\r\n")) {
        printf("Error occurred in file transmission\n");
        free(subbuff);
        return ERROR;
    }
    return 0;
}
