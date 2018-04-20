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

#define RBUFLEN		128 /* buffer for reading messages */
#define GET         0
#define QUIT        1
#define ERROR       -1
#define FBUFLEN     1024 /* file buffer */

/* FUNCTION PROTOTYPES */
void showAddr(char *str, struct sockaddr_storage *from);
char* getfilename(char *buf);
int parserequest(char *buf);
int Tcp_listen(const char *host, const char *serv, socklen_t *addrlenp);
void proto(int s);
void proto_error(int s);
int proto_get(char *buf, int s);

/* GLOBAL VARIABLES */
char *prog_name;

int main(int argc, char *argv[]) {
    //setvbuf(stdout, NULL, _IONBF, 0);
    int	conn_request_skt;	/* passive socket */
    int	s;                  /* connected socket */
    
    socklen_t addrlen;
    struct sockaddr_storage caddr;	/* server and client addresses */

    prog_name = argv[0];
    
    if(argc < 2) {
        err_quit ("usage: %s <port>", prog_name);
    }

    conn_request_skt = Tcp_listen(NULL, argv[1], NULL);

    /* main server loop */
    while(1) {
        /* accept next connection */
        addrlen = sizeof(struct sockaddr_storage);
        s = Accept(conn_request_skt, (struct sockaddr *) &caddr, &addrlen);
        showAddr("Accepted connection from", &caddr);
        printf("New socket: %u\n", s);

        /* serve the client on socket s */
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

void showAddr(char *str, struct sockaddr_storage *from) {
    char *p;
    
    char buf[100];
    getnameinfo((struct sockaddr *)from, sizeof(struct sockaddr_storage), buf, sizeof(buf), NULL, 0, NI_NUMERICHOST);
    
    printf("%s %s\n",str,buf);
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

int tcp_listen(const char *host, const char *serv, socklen_t *addrlenp) {
    int				listenfd, n;
    const int		on = 1;
    struct addrinfo	hints, *res, *ressave;
    
    bzero(&hints, sizeof(struct addrinfo));
    hints.ai_flags = AI_PASSIVE;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    
    if ( (n = getaddrinfo(host, serv, &hints, &res)) != 0)
        err_quit("tcp_listen error for %s, %s: %s",
                 host, serv, gai_strerror(n));
    ressave = res;
    
    do {
        listenfd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
        if (listenfd < 0)
            continue;		/* error, try next one */
        
        Setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
        if (bind(listenfd, res->ai_addr, res->ai_addrlen) == 0)
            break;			/* success */
        
        Close(listenfd);	/* bind error, close and try next one */
    } while ( (res = res->ai_next) != NULL);
    
    if (res == NULL)	/* errno from final socket() or bind() */
        err_sys("tcp_listen error for %s, %s", host, serv);
    
    Listen(listenfd, 2);
    
    if (addrlenp)
        *addrlenp = res->ai_addrlen;	/* return size of protocol address */
    
    freeaddrinfo(ressave);
    
    return(listenfd);
}


int Tcp_listen(const char *host, const char *serv, socklen_t *addrlenp) {
    return(tcp_listen(host, serv, addrlenp));
}


