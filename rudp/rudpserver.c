// Date   : October 30, 2020
// Author : Eric Park

#include <errno.h>
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

#define LOOP_SIZE 1000
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
#define TIMEOUT_LIMIT 5

enum PacketType
{
    TYPE_RRQ = 1, TYPE_DATA, TYPE_ACK, TYPE_ERROR
};

unsigned getPortNumber(int argc, char **argv);
void *makePacket(char type, char seqno, void *data, size_t datasize);
unsigned handleRRQ(void *pkt, char *filename);
unsigned handleACK(void *pkt);

int
main(int argc, char **argv)
{
    int sockfd;
    int optval;
    int clientlen;
    int ack;
    int lastpkt;
    int winstart;
    int winend;
    int i;
    long filesize;
    char *hostaddrp;
    char *filename;
    void *data;
    void *databuf;
    void *readbuf;
    void *pkt;
    unsigned portNum;
    unsigned winsize;
    unsigned loop_counter;
    unsigned num_timeouts;
    size_t datasize;
    ssize_t nbytes; // retval from recvfrom()/sendto()
    struct hostent *hostp;
    struct sockaddr_in serveraddr, clientaddr;
    struct timeval timeout;
    FILE *fp;

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
        readbuf = malloc(RRQ_PKT_SIZE);
        bzero(readbuf, RRQ_PKT_SIZE);
        nbytes = recvfrom(sockfd, readbuf, RRQ_PKT_SIZE, 0,
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
        winsize = handleRRQ(readbuf, filename);
        free(readbuf);
        printf("[rudpserver]         Type: RRQ\n");
        printf("[rudpserver]         Window size: %u\n", winsize);
        printf("[rudpserver]         File: %s\n", filename);

        // If the file doesn't exist, send ERROR packet
        if (access(filename, F_OK) == -1)
        {
            pkt = makePacket(TYPE_ERROR, IGNORE, NULL, IGNORE);
            nbytes = sendto(sockfd, pkt, ERROR_PKT_SIZE, 0,
                            (struct sockaddr *)&clientaddr, clientlen);
            if (nbytes < 0) 
            {
                fprintf(stderr, "[rudpserver] Error on sendto()\n");
                exit(EXIT_FAILURE);
            }
            printf("[rudpclient] Could not locate file %s\n", filename);
            printf("[rudpclient] Sent ERROR packet to the client\n");
            free(filename);
            free(pkt);

            continue;
        }

        // Calculate filesize
        fp = fopen(filename, "r");
        fseek(fp, 0, SEEK_END);
        filesize = ftell(fp);
        fseek(fp, 0, SEEK_SET);

        // Load file into the buffer
        databuf = malloc(filesize);
        if (fread(databuf, filesize, 1, fp) != 1)
        {
            fprintf(stderr, "[rudpserver] Error on fread()\n");
            exit(EXIT_FAILURE);
        }
        printf("[rudpserver] Read %ld bytes of file %s\n", filesize, filename);
        fclose(fp);

        // Set timeout 
        timeout.tv_sec = 3;
        timeout.tv_usec = 0;
        if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (char *)&timeout,
            sizeof(timeout)) < 0)
        {
            fprintf(stderr, "[rudpserver] setsockopt() failed\n");
            exit(EXIT_FAILURE);
        }

        // Send file to the client
        ack = -1;
        winstart = 0;
        num_timeouts = 0;
        lastpkt = filesize / DATA_SIZE;
        readbuf = malloc(ACK_PKT_SIZE);
        do
        {
            // Configure window end
            winend = winstart + winsize - 1;
            winend = winend > lastpkt ? lastpkt : winend;

            // Send packets within the window
            for (i = winstart; i <= winend; ++i)
            {
                datasize = i == lastpkt ? filesize % DATA_SIZE : DATA_SIZE;
                data = malloc(datasize);
                memcpy(data, databuf + (i * DATA_SIZE), datasize);
                pkt = makePacket(TYPE_DATA, (char)i, data, datasize);
                nbytes = sendto(sockfd, pkt, datasize + 2, 0,
                                (struct sockaddr *)&clientaddr, clientlen);
                if (nbytes < 0) 
                {
                    fprintf(stderr, "[rudpserver] Error on sendto()\n");
                    exit(EXIT_FAILURE);
                }
                printf("[rudpserver] Sent packet %d of size %zu\n", i,
                       datasize + 2);
                free(data);
                free(pkt);
            }

            // Read ACK from the client
            bzero(readbuf, ACK_PKT_SIZE);
            nbytes = recvfrom(sockfd, readbuf, ACK_PKT_SIZE, 0,
                              (struct sockaddr *)&clientaddr, &clientlen);
            if (nbytes < 0)
            {
                // timeout
                if (errno == EAGAIN || errno == EWOULDBLOCK)
                {
                    printf("[rudpserver] Timedout waiting for ACK %d\n",
                           winstart);
                    ++num_timeouts;

                    if (num_timeouts == TIMEOUT_LIMIT)
                    {
                        printf("[rudpserver] Timeout limit reached\n");
                        break;
                    }
                    else
                        continue;
                }
                else
                {
                    fprintf(stderr, "[rudpserver] Error on recvfrom()\n");
                    exit(EXIT_FAILURE);
                }
            }

            // Handle ACK packet
            if ((int)handleACK(readbuf) > ack)
            {
                ack = handleACK(readbuf);
                num_timeouts = 0;
                printf("[rudpserver] Received ACK %d\n", ack);
            }
            
            // Move the window
            winstart = ack + 1;
        } while (ack != lastpkt);

        printf("[rudpserver] Finished transferring file %s of size %ld\n",
               filename, filesize);
        free(readbuf);
        free(databuf);
        free(filename);

        // Turn off timeout for receiving the next client
        timeout.tv_sec = 0;
        timeout.tv_usec = 0;
        if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (char *)&timeout,
            sizeof(timeout)) < 0)
        {
            fprintf(stderr, "[rudpserver] setsockopt() failed\n");
            exit(EXIT_FAILURE);
        }
    }

    close(sockfd);

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
// Arguments : char of type, char of sequence number, void * of data, and
//             size_t of datasize
// Does      : makes a packet of given type. Memory is allocated as needs to be
//             freed
// Returns   : void * of packet
void *
makePacket(char type, char seqno, void *data, size_t datasize)
{
    void *pkt;

    switch (type)
    {
        case TYPE_ERROR:
            pkt = malloc(ERROR_PKT_SIZE);
            bzero(pkt, ERROR_PKT_SIZE);
            memcpy(pkt + TYPE_IDX, &type, TYPE_SIZE);
            break;
        case TYPE_DATA:
            pkt = malloc(datasize + 2); // + 2 for type and seqno
            memcpy(pkt + TYPE_IDX, &type, TYPE_SIZE);
            memcpy(pkt + SEQNO_IDX, &seqno, SEQNO_SIZE);
            memcpy(pkt + DATA_IDX, data, datasize);
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
    char winsize;

    bzero(filename, FILENAME_SIZE);

    memcpy(&type, pkt + TYPE_IDX, TYPE_SIZE);
    memcpy(&winsize, pkt + WINDOWSIZE_IDX, WINDOWSIZE_SIZE);
    memcpy(filename, pkt + FILENAME_IDX, FILENAME_SIZE);

    if (type != TYPE_RRQ)
    {
        fprintf(stderr, "[rudpserver] Wrong packet that is not RRQ\n");
        exit(EXIT_FAILURE);
    }

    return (unsigned)winsize;
}

// Function  : handleACK
// Arguments : void * of ACK packet
// Does      : handles an ACK packet and validates.
// Returns   : unsigned of ACK number
unsigned
handleACK(void *pkt)
{
    char type;
    char ack;

    memcpy(&type, pkt + TYPE_IDX, TYPE_SIZE);
    memcpy(&ack, pkt + SEQNO_IDX, SEQNO_SIZE);

    if (type != TYPE_ACK)
    {
        fprintf(stderr, "[rudpserver] Wrong packet that is not ACK\n");
        exit(EXIT_FAILURE);
    }

    return (unsigned)ack;
}