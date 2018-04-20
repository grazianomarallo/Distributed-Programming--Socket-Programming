#include    <stdlib.h>
#include    <string.h>
#include    <stdio.h>
#include    <inttypes.h>
#include    <fcntl.h>
#include    <sys/types.h>
#include    <sys/stat.h>
#include    <unistd.h>
#include    "errlib.h"
#include    "sockwrap.h"

#define RBUFLEN		128 /* Buffer length */
#define GET         0
#define QUIT        1
#define ERROR       -1
#define FBUFLEN     1024

/* FUNCTION PROTOTYPES */
void showAddr(char *str, struct sockaddr_in *a);
char* getfilename(char *buf);
int parserequest(char *buf);
void proto(int s);
void proto_error(int s);
int proto_get(char *buf, int s);

/* GLOBAL VARIABLES */
char *prog_name;

int main(int argc, char *argv[]) {
    //setvbuf(stdout, NULL, _IONBF, 0);
    int	conn_request_skt;	/* passive socket */
    uint16_t lport_n, lport_h;	/* port used by server (net/host ord.) */
    int	bklog = 2;		/* listen backlog */
    int	s;			/* connected socket */
    socklen_t addrlen;
    struct sockaddr_in saddr, caddr;	/* server and client addresses */

    prog_name = argv[0];
    
    if(argc < 2) {
        err_quit ("usage: %s <port>", prog_name);
    }

    /* input server port number */
    if (sscanf(argv[1], "%" SCNu16, &lport_h) != 1)
        err_sys("Invalid port number");
    lport_n = htons(lport_h);

    /* create the socket */
    s = Socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    printf("Socket created: %u\n",s);

    /* bind the socket to any local IP address */
    bzero(&saddr, sizeof(saddr));
    saddr.sin_family      = AF_INET;
    saddr.sin_port        = lport_n;
    saddr.sin_addr.s_addr = INADDR_ANY;
    showAddr("Binding to address", &saddr);
    Bind(s, (struct sockaddr *) &saddr, sizeof(saddr));

    /* listen */
    printf ("Listening at socket %d with backlog = %d \n", s, bklog);
    Listen(s, bklog);

    conn_request_skt = s;

    /* main server loop */
    while(1) {
        /* accept next connection */
        addrlen = sizeof(struct sockaddr_in);
        s = Accept(conn_request_skt, (struct sockaddr *) &caddr, &addrlen);
        showAddr("Accepted connection from", &caddr);
        printf("New socket: %u\n",s);

        proto(s);
    }
}


void proto(int s) {
    char buf[RBUFLEN], *filename;
    int n, fd, ret;
    
    //start waiting for client requests until ending condition
    do {
        n = recv(s, buf, RBUFLEN-1, 0);
        buf[n] = 0;
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
            //request received
            switch( (ret = parserequest(buf)) ) {
                case GET: {
                    filename = getfilename(buf);
                    if(!proto_get(filename, s)) {
                        //something went wrong, send error
                        printf("ERROR. sending notification\n");
                        proto_error(s);
                    }
                    break;
                }
                case QUIT: {
                    printf("Quit received, closing socket\n");
                    Close(s);
                    break;
                }
                case ERROR: {
                    printf("Error received, sending notification\n");
                    proto_error(s);
                    break;
                }
            }
        }
    } while(ret == GET);
}

int proto_get(char *filename, int s) {
    uint32_t tot, length, time;
    uint32_t nlength, ntime;
    int fd, n;
    char sbuf[FBUFLEN];
    struct stat stat_buf;
    
    if ( (fd = open(filename, O_RDONLY)) < 0) {
        return 0;
    }
    fstat(fd, &stat_buf);
    length = stat_buf.st_size;
    nlength = htonl(length);
    time = stat_buf.st_mtime;
    ntime = htonl(time);
    
    printf("Sending reply\n+OK\r\n%u %u + data\n", length, time);
    memcpy(sbuf, "+OK\r\n", 5);
    memcpy(sbuf+5, &nlength, 4);
    memcpy(sbuf+9, &ntime, 4);
    tot = 0;
    
    n = read(fd, sbuf+13, FBUFLEN - 13);
    if(writen(s, sbuf, 13+n) != 13+n) {
        printf("Write error while replying\n");
        return 0;
    }
    tot += n;
    while(tot < length) {
        n = read(fd, sbuf, FBUFLEN);
        if(writen(s, sbuf, n) != n) {
            printf("Write error while replying\n");
            return 0;
        }
        tot += n;
    }
    printf("File sent\n");
    return 1;
}

void proto_error(int s) {
    writen(s, "-ERR\r\n", sizeof(char)*6);
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

char* getfilename(char *buf) {
    char *filename;
    int i;
    
    for(i = 1; buf[i+4] != '\r' ; i++);
    
    filename = (char *) malloc(i+1); //+1 for terminator
    strncpy(filename, &buf[4], i);
    filename[++i] = 0;
    
    return filename;
}

int parserequest(char *buf) {
    printf("Received: %s", buf);
    
    if(!strncmp(buf, "GET", 3)) {
        return GET;
    }
    if(!strncmp(buf, "QUIT\r\n", 6)) {
        return QUIT;
    }
    return ERROR;
}

