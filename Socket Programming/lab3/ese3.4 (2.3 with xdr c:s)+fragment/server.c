/*
 * FTP server
 * XDR and standard message format
 * XDR mode send the length of the file in a separated message to fragment the file
 * standard mode fragments the file
 */


#include    <stdlib.h>
#include    <string.h>
#include    <stdio.h>
#include    <inttypes.h>
#include    <fcntl.h>
#include    <sys/types.h>
#include    <sys/stat.h>
#include    <unistd.h>
#include    <rpc/xdr.h>
#include    "types.h"
#include    "errlib.h"
#include    "sockwrap.h"

#define RBUFLEN		128 /* Buffer length */
#define OK      1
#define ERROR   3
#define GET     0
#define QUIT    2
#define FBUFLEN     1024

/* FUNCTION PROTOTYPES */
void showAddr(char *str, struct sockaddr_in *a);
char* getfilename(char *buf);
int parserequest(char *buf);
void service(int);
void service_xdr(int);
int proto_get(char *buf, int s);
int proto_get_xdr(char *filename, int s);

/* GLOBAL VARIABLES */
char *prog_name;
int xdr_flag;       // useful to verify if the xdr flag (-x in command line) is online

int main(int argc, char *argv[]) {
    int	conn_request_skt;	/* passive socket */
    uint16_t lport_n, lport_h;	/* port used by server (net/host ord.) */
    int	bklog = 2;		/* listen backlog */
    int	s;			/* connected socket */
    socklen_t addrlen;
    struct sockaddr_in saddr, caddr;	/* server and client addresses */
    
    if(argc != 3) {
        printf("Command line format: <format flag: -a | -x> <port>\n");
        exit(1);
    }
    if(strcmp(argv[1], "-x") != 0 && strcmp(argv[1], "-a") != 0) {
        printf("Command line format: <format flag: -a | -x> <port>\n");
        exit(1);
    }
    
    prog_name = argv[0];
    
    // verify command line first argument, if -x activate xdr
    xdr_flag = 0;
    if(!strcmp(argv[1], "-x")) {
        xdr_flag = 1;
    }
    
    /* input server port number */
    if (sscanf(argv[2], "%" SCNu16, &lport_h) != 1)
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
    printf ("Listening at socket %d with backlog = %d \n",s,bklog);
    Listen(s, bklog);
    
    conn_request_skt = s;
    
    /* main server loop */
    while(1) {
        /* accept next connection */
        addrlen = sizeof(struct sockaddr_in);
        s = Accept(conn_request_skt, (struct sockaddr *) &caddr, &addrlen);
        showAddr("Accepted connection from", &caddr);
        printf("New socket: %u\n",s);
        
        /* serve the client on socket s */
        if(xdr_flag)
            service_xdr(s);
        else
            service(s);
    }
}



/*
 * The server firsts receives a message, which can be a GET (to request a file) or QUIT to close the connection
 * different messages will be considered as an ERROR
 *
 * Client message formats:
 * GET filename\r\n
 * QUIT\r\n
 *
 * In case of GET, the server responds with:
 * +OK\r\LLLLTTTTXXXXXXXXXXXXX... (4 byte of L (length), 4 byte of T (last mod time), X is the file content)
 * In case of error, it responds with:
 * -ERR\r\n
 */
// NB: THE FILE IS TRANSMITTED IN CHUNKS OF LENGTH FBUFLEN
void service(int s) {
    // buf is used to read the first message from client
    char buf[RBUFLEN];
    int ret, n;
    
    do {
        n = recv(s, buf, RBUFLEN-1, 0); // receive first client message (GET-QUIT-error)
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
            //parse the message (GET? QUIT? other?)
            switch( (ret = parserequest(buf)) ) {
                case GET: {
                    if(proto_get(buf, s))
                        printf("File sent\n");
                    break;
                }
                    
                case QUIT: {
                    //in case of quit, just close the socket
                    printf("Quit received, closing socket\n");
                    Close(s);
                    break;
                }
                    
                case ERROR: {
                    //error, sending -ERR\r\n
                    printf("Error received, sending notification\n");
                    writen(s, "-ERR\r\n", sizeof(char)*6);
                    Close(s);
                    break;
                }
            }
        }
    } while(ret == GET);
}


int proto_get(char *buf, int s) {
    // sbuf is used to send the answer
    char sbuf[FBUFLEN], *filename;
    struct stat stat_buf; //stat to get file information (eg. length, last mod)
    uint32_t tot, length, time;
    int n, fd;
    uint32_t nlength, ntime;
    
    // GET received, get filename
    filename = getfilename(buf);
    if ( (fd = open(filename, O_RDONLY)) < 0) {
        printf("Error received, sending notification\n");
        writen(s, "-ERR\r\n", sizeof(char)*6);
        return 0;
    }
    fstat(fd, &stat_buf);
    length = stat_buf.st_size;
    time = stat_buf.st_mtime;
    //length and time in network-endian format
    nlength = htonl(length);
    ntime = htonl(time);
    
    printf("Sending reply\n+OK\r\n%u %u + data\n", length, time);
    
    //construct the response message
    memcpy(sbuf, "+OK\r\n", 5);
    memcpy(sbuf+5, &nlength, 4);
    memcpy(sbuf+9, &ntime, 4);
    
    //start sending the file chunks
    //send first packet [+OK\r\nLLLLTTTTxxxx...x]
    //and then loop until file transfer completes [xxxxx...x]
    //NB: two different message formats: the first with the header, and all the other with the content
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
    
    return 1;
}


/*
 * The server firsts receives a message, which can be a GET (to request a file) or QUIT to close the connection
 * different messages will be considered as an ERROR
 *
 * Send length before, and then fragment the file
 *
 * XDR format (look at types.xdr and .h)
 */
void service_xdr(int s) {
    char *filename;
    int ret;
    
    do {
        //XDR stream on socket s is used to transfer messages
        //let's receive first message
        XDR xdrs_in;
        FILE *fstream_in;
        message m = {ERROR, NULL}; //init message m to ERROR and NULL, it will be overwritten when receiving
        
        fstream_in = fdopen(s, "r"); //read mode
        xdrstdio_create(&xdrs_in, fstream_in, XDR_DECODE); //create the stream
        xdr_message(&xdrs_in, &m); //receive message
        xdr_destroy(&xdrs_in);
        fflush(fstream_in);
    
        switch( (ret=m.tag) ) {
            case GET: {
                //the message contains the filename
                filename = m.message_u.filename;
                printf("Received: GET %s\n", filename);
                
                if(proto_get_xdr(filename, s))
                    printf("File sent\n");
                
                break;
            }
            case QUIT: {
                printf("Quit received, closing socket\n");
                break;
            }
            case ERROR: {
                printf("Error, sending notification\n");
                FILE *fstream_out = fdopen(s, "w");
                XDR xdrs_out;
                message m_err = {ERROR, NULL};
                xdr_message(&xdrs_out, &m_err);
                xdr_destroy(&xdrs_out);
                fflush(fstream_out);
                fclose(fstream_out);
                break;
            }
        }
    } while(ret == GET);
    close(s);
}



int proto_get_xdr(char *filename, int s) {
    // create xdr output stream ("w" mode)
    FILE *fstream_out = fdopen(s, "w");
    XDR xdrs_out;
    char sbuf[FBUFLEN];
    int fd, n;
    struct stat stat_buf;
    uint32_t tot, length, time;
    
    
    xdrstdio_create(&xdrs_out, fstream_out, XDR_ENCODE);
    if ( (fd = open(filename, O_RDONLY) ) < 0) {
        printf("Error while opening the file, sending notification\n");
        message m_err = {ERROR, NULL};
        xdr_message(&xdrs_out, &m_err);
        xdr_destroy(&xdrs_out);
        fflush(fstream_out);
        return 0;
    }
    
    //take file information
    fstat(fd, &stat_buf);
    length = stat_buf.st_size;
    time = stat_buf.st_mtime;
    
    printf("Sending length: %u\n", length);
    if(!xdr_int(&xdrs_out, &length)) {
        printf("Error while appending message\n");
        xdr_destroy(&xdrs_out);
        return 0;
    }
    
    printf("Sending file: +OK/%u/%u\n", length, time);
    
    //send file chunks
    for(tot = 0, n = 0; tot < length;) {
        n = read(fd, sbuf, FBUFLEN);
        
        //build message
        file fdata = {{n, sbuf}, time};
        message m_out = {.tag = OK, .message_u.fdata = fdata};
        
        //append message on the stream
        if(!xdr_message(&xdrs_out, &m_out)) {
            printf("Error while appending message\n");
            return 0;
        }
        tot += n;
    }
    
    xdr_destroy(&xdrs_out);
    fflush(fstream_out);
    close(fd);
    
    return 1;
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


