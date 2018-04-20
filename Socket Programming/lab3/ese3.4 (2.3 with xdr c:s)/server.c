/*
 * FTP server
 * XDR and standard message format
 * XDR mode does not fragment the file (long file transfer not working)
 * standard mode fragments the file (long file transfer working)
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
    // sbuf is used to send the answer
    char buf[RBUFLEN], sbuf[FBUFLEN], *filename;
    struct stat stat_buf; //stat to get file information (eg. length, last mod)
    uint32_t tot, length, time;
    int n, fd;
    
    while(1) {
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
            int ret;
            switch( (ret = parserequest(buf)) ) {
                    
                case GET: {
                    // GET received, get filename
                    filename = getfilename(buf);
                    if ( (fd = open(filename, O_RDONLY)) < 0) {
                        printf("Error received, sending notification\n");
                        writen(s, "-ERR\r\n", sizeof(char)*6);
                        break;
                    }
                    fstat(fd, &stat_buf);
                    length = stat_buf.st_size;
                    time = stat_buf.st_mtime;
                    //length and time in network-endian format
                    uint32_t nlength = htonl(length);
                    uint32_t ntime = htonl(time);
                    
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
                    if(writen(s, sbuf, 13+n) != 13+n)
                        printf("Write error while replying\n");
                    tot += n;
                    while(tot < length) {
                        n = read(fd, sbuf, FBUFLEN);
                        if(writen(s, sbuf, n) != n) {
                            printf("Write error while replying\n");
                            break;
                        }
                        tot += n;
                    }
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
            
            //both in case of QUIT and ERROR, stop loop, connection is closed
            if(ret != GET)
                break;
        }
    }
}



/*
 * The server firsts receives a message, which can be a GET (to request a file) or QUIT to close the connection
 * different messages will be considered as an ERROR
 *
 * XDR format (look at types.xdr and .h)
 */
void service_xdr(int s) {
    char *filename;
    
    struct stat stat_buf;
    uint32_t tot, length, time;
    int n, fd;
    
    while(1) {
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
        
        int ret;
        switch( (ret=m.tag) ) {
            case GET: {
                //the message contains the filename
                filename = m.message_u.filename;
                printf("Received: GET %s\n", filename);
                
                // create xdr output stream ("w" mode)
                FILE *fstream_out = fdopen(s, "w");
                XDR xdrs_out;
                xdrstdio_create(&xdrs_out, fstream_out, XDR_ENCODE);
                
                if ( (fd = open(filename, O_RDONLY) ) < 0) {
                    printf("Error while opening the file, sending notification\n");
                    message m_err = {ERROR, NULL};
                    xdr_message(&xdrs_out, &m_err);
                    xdr_destroy(&xdrs_out);
                    fflush(fstream_out);
                    break;
                }
                
                //take file information
                fstat(fd, &stat_buf);
                length = stat_buf.st_size;
                time = stat_buf.st_mtime;
                
                printf("Sending file: +OK/%u/%u\n", length, time);
                
                //allocate an array to send the whole file (only one message)
                char *array = malloc(length);
                read(fd, array, length); //read the whole file
                
                //build message
                file fdata = {{length, array}, time};
                message m_out = {.tag = OK, .message_u.fdata = fdata};
                
                //append message on the stream
                if(!xdr_message(&xdrs_out, &m_out)) {
                    printf("Error while appending message\n");
                    xdr_destroy(&xdrs_out);
                }
                
                xdr_destroy(&xdrs_out);
                fflush(fstream_out);
                close(fd);
                
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
        
        if(ret != GET) break;
    }
    close(s);
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
    char *filename = NULL;
    int i;
    
    for(i = 0; buf[i+4] != '\r' && buf[i+5] != '\n'; i++) {
        filename = realloc(filename, (i+1)*sizeof(char));
        filename[i] = buf[i+4];
    }
    return filename;
}


int parserequest(char *buf) {
    char subbuff[6];
    
    printf("Received: %s", buf);
    // copy first 3 char
    strncpy(subbuff, buf, 3);
    
    if(!strcmp(subbuff, "GET")) {
        return GET;
    }
    strncpy(subbuff, buf, 6);
    if(!strcmp(subbuff, "QUIT\r\n")) {
        return QUIT;
    }
    return ERROR;
}


