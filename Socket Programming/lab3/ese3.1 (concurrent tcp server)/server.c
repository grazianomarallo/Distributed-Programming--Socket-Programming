/*
 * Concurrent TCP server
 * Processes created on demand
 */

#include    <stdlib.h>
#include    <signal.h>
#include    <string.h>
#include    <stdio.h>
#include    <ctype.h>
#include    <inttypes.h>
#include    <fcntl.h>
#include    <sys/types.h>
#include    <sys/wait.h>
#include    <sys/stat.h>
#include    <unistd.h>
#include    "errlib.h"
#include    "sockwrap.h"

#define RBUFLEN		128 /* Buffer length */
#define GET         0
#define QUIT        1
#define ERROR       -1
#define FBUFLEN     1024
#define MAX_CHILDS  3

/* FUNCTION PROTOTYPES */
void showAddr(char *str, struct sockaddr_in *a);
char* getfilename(char *buf);
int parserequest(char *buf);
void service(int);

/* GLOBAL VARIABLES */
char *prog_name;
int childnum;

static void childhandler(int signo) {
    //WNOHANG specifies waitpid should return immediately instead of waiting if there is no child process ready to be noticed
    //It is necessary to perform a loop, and not a single waitpid(), because it is not guaranteed that a single signal will be sent for every terminated children (for example, if the father execution stream is in the signal handler, it has already performed the waitpid() loop, and in the meantime two children terminate, the handler will be newly called, but just once).
    while( waitpid(-1, NULL, WNOHANG) > 0) {
        childnum--;
    }
    return;
}


//perform a blocking wait, max children number reached
//if one is zombie, this means that at least a new connection can be accepted
//NB: wait() eliminates the zombies. in this case, call non blocking wait until all zombies have been eliminated
//this leaves some zombies around because children may terminate while the process is waiting for the accept
//this is avoided using SIGCHILD signal
int main(int argc, char *argv[]) {
    int	conn_request_skt;	/* passive socket */
    uint16_t lport_n, lport_h;	/* port used by server (net/host ord.) */
    int	bklog = 2;		/* listen backlog */
    int	s;			/* connected socket */
    int childpid;
    
    socklen_t addrlen;
    struct sockaddr_in saddr, caddr;	/* server and client addresses */
    
    struct sigaction sa;

    
    prog_name = argv[0];

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
    printf ("Listening at socket %d with backlog = %d \n",s,bklog);
    Listen(s, bklog);

    conn_request_skt = s;
    

    ////////////////////////////////////////////////////////////////
    ////////////////////////////////////////////////////////////////
    ////////////////////////////////////////////////////////////////
    /* main server body */
    for(childnum = 0;;) {
        if(childnum == MAX_CHILDS) {
            //wait for a change of state on any children (child termintes)
            //NB: here it is possible to experience a race condition if the signal
            //has been sent after the proc entered this section
            //but has not already performed the wait call
            //because the wait() would be blocking but the number of children waiting for the wait() would have been already reduced by the signal handler.
            wait(NULL);
            childnum--;
        }
        

        //on Accept it may happen that 2 childs close
        //and the wait above will decrement the counter only once
        //so here waitpid with WNOHANG to remove zombies
        //but I can also register an handler for SIGCHLD signals to solve the problem
        //moreover, in order to solve the race condition on the wait above
        //I've to Install the SIGCHLD signal handler just before calling the Accept() and uninstall it soon after. In this case it is necessary, after the if that tests the children number, to perform a while loop with a non-blocking wait(), as in the signal handler, in order to call the wait() for the children that have terminated when the signal handler was not installed.
        
        //do the non blocking wait call
        while( waitpid(-1, NULL, WNOHANG) > 0) {
            childnum--;
        }
        
        //register the signal
        memset(&sa, 0, sizeof(sa));
        sa.sa_handler = childhandler;
        sigaction(SIGCHLD, &sa, NULL);
        
        addrlen = sizeof(struct sockaddr_in);
        s = Accept(conn_request_skt, (struct sockaddr *) &caddr, &addrlen);
        showAddr("Accepted connection from", &caddr);
        printf("New socket: %u\n", s);
        
        //unistall the handler
        memset(&sa, 0, sizeof(sa));
        sa.sa_handler = SIG_DFL;
        sigaction(SIGCHLD, &sa, NULL);
        
        if( (childpid=fork()) < 0 ) {
            err_msg("fork() failed");
            close(s);
        }
        else if (childpid > 0) {
            /* parent process: count number of active "servicing" processes */
            childnum++;
            close(s); //parent process does not need service socket
        } else {
            /* child process: service process */
            close(conn_request_skt); //child process does not need parent socket on which new connections are accepted
            service(s);
            exit(0);
        }
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



