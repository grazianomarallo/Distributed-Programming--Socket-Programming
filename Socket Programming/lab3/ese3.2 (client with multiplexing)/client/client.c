#include     <stdlib.h>
#include     <string.h>
#include     <stdio.h>
#include     <inttypes.h>
#include     "errlib.h"
#include     "sockwrap.h"


#define BUFLEN	128 /* BUFFER LENGTH */
#define FBUFLEN 1024
#define OK      1
#define ERROR   -2
#define GET     2
#define QUIT    0
#define ABORT   -1

/* FUNCTION PROTOTYPES */
int mygetline(char * line, size_t maxline, char *prompt);
void showAddr(char *str, struct sockaddr_in *a);
int parsereply(char *);
int parserequest(char *);
int proto(int);

/* GLOBAL VARIABLES */
char *prog_name;


int main(int argc, char *argv[]) {
    uint16_t tport_n, tport_h;	/* server port number (net/host ord) */
    
    int s, result;
    struct sockaddr_in	saddr;		/* server address structure */
    struct in_addr	sIPaddr; 	/* server IP addr. structure */

    
    prog_name = argv[0];
    
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
    
    Close(s);
    exit(0);
}

int proto(int s) {
    fd_set cset;
    FILE *f;
    uint32_t nlength, length, ntime, time;
    char buf[BUFLEN];		/* transmission buffer */
    char sbuf[BUFLEN], rbuf[FBUFLEN];
    int n, ret, result, tot, reading_file = 0, quit_set = 0;
    
    printf("Enter: - GET <filename> to request the transfer of a file.\n       - Q to close the connection.\n       - A to force the closing.\n");
    while(1) {
        //use Select function to manage multiplexing between the socket and the user input
        //adding both to the fd_set, so that the select gets the used one
        FD_ZERO(&cset);
        FD_SET(s, &cset);
        if(!quit_set)
            FD_SET(fileno(stdin), &cset);
        n = Select(FD_SETSIZE, &cset, NULL, NULL, NULL);
        if(n > 0) {
            if(FD_ISSET(s, &cset)) {
                if(!reading_file) {
                    //let's read first 13 characters
                    n = recv(s, rbuf, sizeof(char)*13, 0);
                    if (n < 0) {
                        printf("Read error, closing connection\n");
                        return 0;
                    }
                    else if (n == 0) {
                        if(quit_set)
                            break;
                        printf("Connection closed by party on socket %d\n",s);
                        return 0;
                    }
                    else {
                        ret = parsereply(rbuf);
                        if(ret ==  OK) {
                            reading_file = 1;
                            tot = 0;
                            
                            memcpy(&nlength, rbuf+5*sizeof(char), 4*sizeof(char));
                            length = ntohl(nlength);
                            printf("Length: %u\n", length);
                            memcpy(&ntime, rbuf+9*sizeof(char), 4*sizeof(char));
                            time = ntohl(ntime);
                            printf("Time: %u\n", time);
                            
                            f = fopen(buf+4, "wb");
                            if(length <= 0) {
                                reading_file = 0;
                                fclose(f);
                            }
                        }
                        else if(ret ==  ERROR) {
                            printf("Received error, closing connection\n");
                            return 0;
                        }
                    }
                } else {
                    n = recv(s, rbuf, FBUFLEN, 0);
                    if(fwrite(rbuf,sizeof(char),n, f) != n) {
                        printf("Error during file write\n");
                    }
                    tot += n;
                    if(tot >= length) {
                        printf("File transfer completed\n");
                        reading_file = 0;
                        fclose(f);
                    }
                }
            }
            if(FD_ISSET(fileno(stdin), &cset)) {
                //read user input
                if(mygetline(buf, BUFLEN, "") == EOF) {
                    printf("Closing socket\n");
                    if(writen(s, "QUIT\r\n", sizeof(char)*6) != sizeof(char)*6)
                        printf("Write error\n");
                    break;
                }
                
                //manage different inputs: GET - A - Q
                result = parserequest(buf);
                if(result == GET) {
                    size_t len = strlen(buf);
                    bzero(sbuf, sizeof(sbuf));
                    strncpy(sbuf, buf, len);
                    strncpy(sbuf+len,"\r\n", 2);
                    
                    if(writen(s, sbuf, sizeof(char)*(2+len)) != sizeof(char)*(2+len)) {
                        printf("Write error\n");
                        return 0;
                    }
                }
                else if(result == QUIT) {
                    printf("Closing connection\n");
                    if(writen(s, "QUIT\r\n", sizeof(char)*6) != sizeof(char)*6) {
                        printf("Write error\n");
                        return 0;
                    }
                    quit_set = 1;
                }
                else if(result == ABORT) {
                    printf("Process aborted\n");
                    if(reading_file)
                        fclose(f);
                    break;
                }
                else {
                    printf("Input not valid\n");
                }
            }
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

int parserequest(char *buf) {
    if(strlen(buf) == 1 && !strcmp(buf, "Q")) {
        return QUIT;
    }
    if(strlen(buf) == 1 && !strcmp(buf, "A")) {
        return ABORT;
    }
    if(strlen(buf) > 4 && !strncmp(buf, "GET ", 4)) {
        return GET;
    }
    return ERROR;
}

int parsereply(char *rbuf) {
    if(!strncmp(rbuf, "+OK\r\n", 5)) {
        printf("OK - transfer starts\n");
        return OK;
    }
    if(!strncmp(rbuf, "-ERR\r\n", 6)) {
        printf("Error occurred in file transmission\n");
        return ERROR;
    }
    return -3;
}
