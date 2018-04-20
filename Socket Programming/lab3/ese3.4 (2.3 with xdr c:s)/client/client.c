/*
 * FTP client
 * XDR and standard message format
 * XDR mode does not fragment the file (long file transfer not working)
 * standard mode fragments the file (long file transfer working)
 */

#include     <stdlib.h>
#include     <string.h>
#include     <stdio.h>
#include     <inttypes.h>
#include    <rpc/xdr.h>
#include     "types.h"
#include     "errlib.h"
#include     "sockwrap.h"


#define BUFLEN	128 /* BUFFER LENGTH */
#define FBUFLEN 1024
#define OK      1
#define ERROR   3
#define GET     0
#define QUIT    2

/* FUNCTION PROTOTYPES */
int mygetline(char * line, size_t maxline, char *prompt);
void showAddr(char *str, struct sockaddr_in *a);
int parsereply(char *);
int parserequest(char *);
void xdr_quit(int);
void service_xdr(int);
void service(int);

/* GLOBAL VARIABLES */
char *prog_name;
int xdr_flag;

int main(int argc, char *argv[]) {
    uint16_t tport_n, tport_h;	/* server port number (net/host ord) */
    
    int s;
    int result;
    struct sockaddr_in	saddr;		/* server address structure */
    struct in_addr	sIPaddr; 	/* server IP addr. structure */
    
    prog_name = argv[0];
    
    if(argc != 4) {
        printf("Command line format: <format flag: -a | -x> <IP address> <port>\n");
        exit(1);
    }
    
    xdr_flag = 0;
    if(!strcmp(argv[1], "-x")) {
        xdr_flag = 1;
    }
    
    /* input IP address and port of server */
    result = inet_aton(argv[2], &sIPaddr);
    if(!result)
        err_quit("Invalid address");
    
    if(sscanf(argv[3], "%" SCNu16, &tport_h) != 1)
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
    if(xdr_flag)
        service_xdr(s);
    else
        service(s);
    
    close(s);
    exit(0);
}

void service(int s) {
    char buf[BUFLEN];		/* transmission buffer */
    char *sbuf, rbuf[FBUFLEN];
    
    int n, tot;
    
    while(1) {
        if(mygetline(buf, BUFLEN, "Insert filename (no GET), one per line (end with EOF - press CTRL+D on keyboard to enter EOF):\n") == EOF) {
            printf("Closing socket\n");
            sbuf = (char *) malloc(sizeof(char)*6);
            strncpy(sbuf, "QUIT\r\n", 6);
            if(writen(s, sbuf, sizeof(char)*6) != sizeof(char)*6)
                printf("Write error\n");
            
            free(sbuf);
            break;
        }
        
        size_t len = strlen(buf);
        sbuf = (char *) malloc(sizeof(char)*(6+len)); // 6 characters: 'GET \r\n' plus filename
        strncpy(sbuf, "GET ", 4);
        strncpy(sbuf+4, buf, len);
        strncpy(sbuf+4+len,"\r\n", 2);
        
        if(writen(s, sbuf, sizeof(char)*(6+len)) != sizeof(char)*(6+len))
            printf("Write error\n");
        
        //let's read first 13 characters
        n = recv(s, rbuf, sizeof(char)*13, 0);
        
        if (n < 0) {
            printf("Read error\n");
            close(s);
            printf("Socket %d closed\n", s);
            break;
        }
        else if (n == 0) {
            printf("Connection closed by party on socket %d\n",s);
            close(s);
            break;
        }
        else {
            int ret;
            
            if( (ret = parsereply(rbuf)) ==  OK) {
                uint32_t nlength, length, ntime, time;
                tot = 0;
                
                memcpy(&nlength, rbuf+5*sizeof(char), 4*sizeof(char));
                length = ntohl(nlength);
                printf("Length: %u\n", length);
                memcpy(&ntime, rbuf+9*sizeof(char), 4*sizeof(char));
                time = ntohl(ntime);
                printf("Time: %u\n", time);
                
                
                FILE *f = fopen(buf, "wb");
                while(length > 0) {
                    n = recv(s, rbuf, FBUFLEN, 0);
                    if(fwrite(rbuf,sizeof(char),n, f) != n) {
                        printf("Error during file write\n");
                    }
                    tot += n;
                    if(tot >= length)
                        break;
                    
                }
                
                fclose(f);
            }
            
        }
    }
}


void service_xdr(int s) {
    char buf[BUFLEN];
    
    FILE *f;
    XDR xdrs_in;
    
    int n, result, quit_set = 0;
    int reading_file = 0, tot;
    uint32_t length, time;
    
    char *filename;
    
    printf("Enter: - GET <filename> to request the transfer of a file.\n       - Q to close the connection.\n");
    while(1) {
        //read user input
        if(mygetline(buf, BUFLEN, "") == EOF) {
            printf("Closing socket\n");
            xdr_quit(s);
            break;
        }
        
        //manage different inputs: GET - Q
        result = parserequest(buf);
        filename = buf+4;
        
        if(result == GET) {
            XDR xdrs_out;
            
            message m = {GET, .message_u.filename = filename};
            
            FILE *fstream = fdopen(s, "w");
            xdrstdio_create(&xdrs_out, fstream, XDR_ENCODE);
            xdr_message(&xdrs_out, &m);
            xdr_destroy(&xdrs_out);
            fflush(fstream);
        }
        else if(result == QUIT) {
            printf("Closing connection\n");
            xdr_quit(s);
            break;
        }
        else {
            printf("Incorrect string\nEnter: - GET <filename> to request the transfer of a file.\n       - Q to close the connection.\n");
            continue;
        }

        message m = {ERROR, NULL};
        FILE *fstream;
        fstream = fdopen(s, "r");
        xdrstdio_create(&xdrs_in, fstream, XDR_DECODE);
        xdr_message(&xdrs_in, &m);
        
        if(m.tag == OK) {
            printf("Receiving file...\n");
            reading_file = 1;
            tot = 0;
            
            f = fopen(filename, "wb");
            
            length = m.message_u.fdata.contents.contents_len;
            tot += length;
            time = m.message_u.fdata.last_mod_time;
            printf("Last modify: %u\n", time);
            
            if(fwrite(m.message_u.fdata.contents.contents_val, sizeof(char), length, f) != length) {
                printf("Error during file write\n");
                break;
            }
            
            //read all together
            fclose(f);
            xdr_destroy(&xdrs_in);
            printf("Total length: %u\n", tot);
            printf("File transfer completed\n");
            
        } else {
            printf("Error received from server\n");
            break;
        }
    }
    
}

void xdr_quit(int s) {
    FILE *fstream;
    XDR xdrs_out;
    
    message m = {QUIT, NULL};
    
    fstream = fdopen(s, "w");
    xdrstdio_create(&xdrs_out, fstream, XDR_ENCODE);
    xdr_message(&xdrs_out, &m);
    xdr_destroy(&xdrs_out);
    fflush(fstream);
    Close(s);
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



int parserequest(char *buf) {
    if(strlen(buf) == 1 && !strcmp(buf, "Q")) {
        return QUIT;
    }
    char *subbuff = malloc(sizeof(char)*5);
    strncpy(subbuff, buf, 4);
    if(strlen(buf) > 4 && !strcmp(subbuff, "GET ")) {
        return GET;
    }
    return ERROR;
}
