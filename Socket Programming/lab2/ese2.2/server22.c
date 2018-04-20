#include    <stdlib.h>
#include    <string.h>
#include    <inttypes.h>
#include    "errlib.h"
#include    "sockwrap.h"

#define BUFLEN	65536 /* Maximum UDP datagram length */
#define CLIENTS 10

struct clcounter {
    struct sockaddr_storage *from;
    int counter;
};

/* FUNCTION PROTOTYPES */
void showAddr(char *str, struct sockaddr_storage *a);
int clientCount(struct clcounter *clients, struct sockaddr_storage *from);
int udp_server(const char *host, const char *serv, socklen_t *addrlenp);

/* GLOBAL VARIABLES */
char *prog_name;
int older_pointer;    // index of the older client into clients array

int main(int argc, char *argv[]) {
    int s;  // socket
    char buf[BUFLEN];
    int i, n, counter;
    
    struct clcounter clients[CLIENTS];
    struct sockaddr_storage from;
    socklen_t fromlen;
    bzero(&clients, sizeof(struct clcounter)*CLIENTS);

    prog_name = argv[0];
    older_pointer = 0;
    
    s = udp_server(NULL, argv[1], NULL);
    printf("Socket created: %d\n", s);

    while(1) {
        printf("Waiting for data from client\n");
        fromlen = sizeof(struct sockaddr);
        n = recvfrom(s, buf, BUFLEN-1, 0, (struct sockaddr *) &from, &fromlen);
		printf("Received %d bytes\n", n);
        counter = clientCount(clients, &from);
        printf("Request number %d\n", counter);
        //showAddr(" received from client ", &from);
        if(n != -1 && counter <= 3) {
            printf("Sending data\n");
            buf[n] = '\0';
            sendto(s, buf, n, 0, (struct sockaddr *) &from, sizeof(struct sockaddr));
        }
    }
    
}


/* Utility function to display a string str
   followed by an internet address a, written
   in decimal notation
*/
void showAddr(char *str, struct sockaddr_storage *a) {
    char *p;
    
    //p = inet_ntoa(a->sin_addr);
	//printf("%s %s",str,p);
	//printf("!%u\n", ntohs(a->sin_port));
}


// if client has already registered increment its counter and return it
// else insert new client (overwriting older ones) and return new count
int clientCount(struct clcounter *clients, struct sockaddr_storage *from) {
    int i;
	char buf[512];
	char buf2[512];
    getnameinfo((struct sockaddr *)from, sizeof(struct sockaddr_storage), buf, sizeof(buf), NULL, 0, 0);


    for(i = 0; i < CLIENTS; i++) {
        if(clients[i].counter == 0) {
            // if null it means that no clients has been found until now
            // so insert new one
            clients[older_pointer].counter = 1;
			clients[older_pointer].from = (struct sockaddr_storage *) malloc(sizeof(struct sockaddr_storage)); 
            *(clients[older_pointer].from) = *(from);
            older_pointer = (older_pointer == CLIENTS-1 ? 0 : older_pointer + 1);
            return 1;
        }
 		getnameinfo((struct sockaddr *)clients[i].from, sizeof(struct sockaddr_storage), buf2, sizeof(buf2), NULL, 0, 0);
        if(!strcmp(buf, buf2)) {
            return ++clients[i].counter;
        }
    }
    // if here it means that more than CLIENTS have done requests and a new one wants to enter
    clients[older_pointer].counter = 1;
    *(clients[older_pointer].from) = *from;
    older_pointer = older_pointer == CLIENTS-1 ? 0 : older_pointer + 1;
    return 1;
}



int udp_server(const char *host, const char *serv, socklen_t *addrlenp) {
    int sockfd, n;
    struct addrinfo hints, *res, *ressave;
    
    bzero(&hints, sizeof(struct addrinfo));
	hints.ai_flags = AI_PASSIVE;
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;
    
    if( (n = getaddrinfo(host, serv, &hints, &res)) != 0 ) {
        err_quit("udp server error for %s, %s: %s", host, serv, gai_strerror(n));
    }
    ressave = res;
    
    do {
        // s = Socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        sockfd = Socket(res->ai_family, res->ai_socktype, res->ai_protocol);
        if(sockfd < 0)
            continue;
        
        if (bind (sockfd, res->ai_addr, res->ai_addrlen) == 0)
            break; //success!
        
        Close(sockfd);
    } while( (res = res->ai_next) != NULL);
    
    if(res == NULL)
        err_sys("udp server error for %s, %s", host, serv);

	if (addrlenp)
        *addrlenp = res->ai_addrlen;
    
    freeaddrinfo(ressave);
    return sockfd;
}
