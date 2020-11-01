// Date   : October 30, 2020
// Author : Eric Park

#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <sys/types.h>
#include <sys/socket.h>

#define LOOP_SIZE 1
#define RRQ_PKT_SIZE 22
#define DATA_PKT_SIZE 514
#define ACK_PKT_SIZE 2
#define ERROR_PKT_SIZE 1
#define TYPE_SIZE 1
#define WINDOWSIZE_SIZE 1
#define SEQNO_SIZE 1
#define FILENAME_SIZE 20
#define DATA_SIZE 512
#define TYPE_IDX 0
#define WINDOWSIZE_IDX 1
#define SEQNO_IDX 1
#define FILENAME_IDX 2
#define DATA_IDX 2
#define IGNORE 0

enum PacketType
{
    TYPE_RRQ = 1, TYPE_DATA, TYPE_ACK, TYPE_ERROR
};

unsigned getPortNumber(int argc, char **argv);
void *makePacket(char type, char seqno, char *data);
unsigned handleRRQ(void *pkt, char *filename);

int
main(int argc, char **argv)
{
    int sockfd;
    int optval;
    int clientlen;
    int ack;
    char *hostaddrp;
    char *filename;
    void *buf;
    void *pkt;
    unsigned portNum;
    unsigned windowsize;
    unsigned loop_counter;
    ssize_t nbytes; // retval from recvfrom()/sendto()
    struct hostent *hostp;
    struct sockaddr_in serveraddr, clientaddr;

    // Handle input and get port number
    portNum = getPortNumber(argc, argv);

    // Create a UDP socket
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0)
    {
        fprintf(stderr, "[rudpserver] Failed to create socket in main()\n");
        exit(EXIT_FAILURE);
    }
    printf("[rudpserver] Created a UDP socket\n");

    // Eliminate "ERROR on binding: Address already in use" error 
    optval = 1;
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, (const void *)&optval,
               sizeof(int));

    // Build the server's Internet address
    bzero((char *) &serveraddr, sizeof(serveraddr));
    serveraddr.sin_family = AF_INET;
    serveraddr.sin_addr.s_addr = htonl(INADDR_ANY);
    serveraddr.sin_port = htons((unsigned short)portNum);

    // Associate the parent socket with a port 
    if (bind(sockfd, (struct sockaddr *)&serveraddr, sizeof(serveraddr)) < 0)
    {
        fprintf(stderr, "[rudpserver] Failed to bind to socket %d\n", sockfd);
        exit(EXIT_FAILURE);
    }

    // Main loop: waits for an RRQ packet and handles appropriately
    loop_counter = 0;
    clientlen = sizeof(clientaddr);
    while (loop_counter < LOOP_SIZE)
    {
        printf("[rudpserver]\n[rudpserver] Loop %u\n", loop_counter++);
        printf("[rudpserver] Waiting for packets to arrive...\n");

        // Receive a UDP datagram from a client
        buf = malloc(RRQ_PKT_SIZE);
        bzero(buf, RRQ_PKT_SIZE);
        nbytes = recvfrom(sockfd, buf, RRQ_PKT_SIZE, 0,
                          (struct sockaddr *)&clientaddr, &clientlen);
        if (nbytes < 0)
        {
            fprintf(stderr, "[rudpserver] Error on recvfrom()\n");
            exit(EXIT_FAILURE);
        }

        // Determine who sent the datagram
        hostp = gethostbyaddr((const char *)&clientaddr.sin_addr.s_addr,
                              sizeof(clientaddr.sin_addr.s_addr), AF_INET);
        if (hostp == NULL)
        {
            fprintf(stderr, "[rudpserver] Error on gethostbyaddr()\n");
            exit(EXIT_FAILURE);
        }
        hostaddrp = inet_ntoa(clientaddr.sin_addr);
        if (hostaddrp == NULL)
        {
            fprintf(stderr, "[rudpserver] Error on inet_ntoa()\n");
            exit(EXIT_FAILURE);
        }
        printf("[rudpserver] Received datagram from %s (%s)\n", hostp->h_name,
               hostaddrp);
        printf("[rudpserver]         Size: %d bytes\n", nbytes);

        // Handle the RRQ packet
        filename = malloc(FILENAME_SIZE);
        windowsize = handleRRQ(buf, filename);
        printf("[rudpserver]         Type: RRQ\n");
        printf("[rudpserver]         Window size: %u\n", windowsize);
        printf("[rudpserver]         File: %s\n", filename);

        // Load file into the buffer. If fails, send ERROR packet


        // Echo the input back to the client 
        pkt = makePacket(TYPE_ERROR, IGNORE, NULL);
        nbytes = sendto(sockfd, pkt, ERROR_PKT_SIZE, 0,
                        (struct sockaddr *)&clientaddr, clientlen);
        if (nbytes < 0) 
        {
            fprintf(stderr, "[rudpserver] Error on sendto()\n");
            exit(EXIT_FAILURE);
        }
    }

    close(sockfd);

    free(buf);
    free(pkt);
    free(filename);

    return 0;
}

// Function  : getPortNumber
// Arguments : int of argc and char ** of argv 
// Does      : 1) checks for the singular argument, port number
//             3) returns the port number
// Returns   : unsigned int of port number
unsigned
getPortNumber(int argc, char **argv)
{
    char *rest;
    long portNum;

    // Checks for the singular argument
    if (argc != 2)
    {
        fprintf(stderr, "[httpproxy] Usage: %s <port number>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    // Gets port number
    portNum = strtol(argv[1], &rest, 10);

    return (unsigned)portNum;
}

// Function  : makePacket
// Arguments : char of type, char of sequence number, char * of data
// Does      : makes a packet of given type. Memory is allocated as needs to be
//             freed
// Returns   : void * of packet
void *
makePacket(char type, char seqno, char *data)
{
    void *pkt;

    switch (type)
    {
        case TYPE_ERROR:
            pkt = malloc(ERROR_PKT_SIZE);
            bzero(pkt, ERROR_PKT_SIZE);
            memcpy(pkt + TYPE_IDX, &type, TYPE_SIZE);
            break;
        default:
            break;
    }

    return pkt;
}

// Function  : handleRRQ
// Arguments : void * of RRQ packet
// Does      : handles an RRQ packet and validates.
// Returns   : unsigned of window size, and char * of filename
unsigned
handleRRQ(void *pkt, char *filename)
{
    char type;
    char windowsize;

    bzero(filename, FILENAME_SIZE);

    memcpy(&type, pkt + TYPE_IDX, TYPE_SIZE);
    memcpy(&windowsize, pkt + WINDOWSIZE_IDX, WINDOWSIZE_SIZE);
    memcpy(filename, pkt + FILENAME_IDX, FILENAME_SIZE);

    if (type != TYPE_RRQ)
    {
        fprintf(stderr, "[rudpserver] Wrong packet that is not RRQ\n");
        exit(EXIT_FAILURE);
    }

    return (unsigned)windowsize;
}