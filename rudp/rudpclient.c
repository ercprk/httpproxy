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

void *makePacket(char type, char second_field, char *filename);

enum PacketType
{
    TYPE_RRQ = 1, TYPE_DATA, TYPE_ACK, TYPE_ERROR
};

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
#define DST_DIR "./DST/"
#define MAX_FILE_SIZE 26112

int
main(int argc, char **argv)
{
    char type;
    char seqno;
    int ack;
    int sockfd;
    int portno;
    int windowsize;
    int nbytes;
    int serverlen;
    size_t filesize;
    struct in_addr hostaddr;
    struct sockaddr_in serveraddr;
    struct hostent *server;
    char *filename;
    char *filepath;
    char *hostIP;
    void *pkt;
    void *readbuf;
    void *databuf;
    FILE *fp;

    // Check command line arguments
    if (argc != 5)
    {
       fprintf(stderr, "[rudpclient] Usage: %s <host IP> <port> ", argv[0]);
       fprintf(stderr, "<windowsize> <filename>\n");
       exit(EXIT_FAILURE);
    }
    hostIP = argv[1];
    portno = atoi(argv[2]);
    windowsize = atoi(argv[3]);
    filename = argv[4];

    // Create the UDP socket
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0)
    {
        fprintf(stderr, "[rudpclient] Error creating the UDP socket\n");
        exit(EXIT_FAILURE);
    }
    printf("[rudpclient] Created a UDP socket\n");

    // Get the server's DNS entry
    hostaddr.s_addr = inet_addr(hostIP);
    server = gethostbyaddr(&hostaddr, sizeof(hostaddr), AF_INET);
    if (server == NULL)
    {
        fprintf(stderr, "[rudpclient] Error: no host at %s\n", hostIP);
        exit(EXIT_FAILURE);
    }
    printf("[rudpclient] Found host at %s (%s)\n", server->h_name, hostIP);

    // Build the server's Internet address
    bzero((char *) &serveraddr, sizeof(serveraddr));
    serveraddr.sin_family = AF_INET;
    bcopy((char *)server->h_addr, (char *)&serveraddr.sin_addr.s_addr,
          server->h_length);
    serveraddr.sin_port = htons(portno);

    // Makes an RRQ packet
    pkt = makePacket(TYPE_RRQ, windowsize, filename);

    // Send the message to the server
    serverlen = sizeof(serveraddr);
    nbytes = sendto(sockfd, pkt, RRQ_PKT_SIZE, 0, 
                    (struct sockaddr *)&serveraddr, serverlen);
    if (nbytes < 0)
    {
        fprintf(stderr, "[rudpclient] Error in sendto()\n");
        exit(EXIT_FAILURE);
    }
    free(pkt);

    // Copies the file over from the server to the data buffer
    ack = -1;
    filesize = 0;
    readbuf = malloc(DATA_PKT_SIZE);
    databuf = malloc(MAX_FILE_SIZE);
    do
    {
        // Read in
        bzero(readbuf, DATA_PKT_SIZE);
        nbytes = recvfrom(sockfd, readbuf, DATA_PKT_SIZE, 0,
                          (struct sockaddr *)&serveraddr, &serverlen);
        if (nbytes < 0)
        {
            fprintf(stderr, "[rudpclient] Error in recvfrom()\n");
            exit(EXIT_FAILURE);
        }

        // Check type
        memcpy(&type, readbuf + TYPE_IDX, TYPE_SIZE);
        if (type == TYPE_ERROR)
        {
            printf("[rudpclient] Could not transfer file %s from the server\n",
                   filename);
            free(readbuf);
            free(databuf);
            close(sockfd);
            return 0;
        }
        else if (type == TYPE_DATA)
        {
            memcpy(&seqno, readbuf + SEQNO_IDX, SEQNO_SIZE);
            if (seqno == ack + 1) // If the packet is the right one
            {
                // Store and send ACK
                ++ack;
                memcpy(databuf + (seqno * DATA_SIZE), readbuf + DATA_IDX,
                       nbytes - 2);
                filesize += nbytes - 2; // - 2 for type and seqno
                pkt = makePacket(TYPE_ACK, ack, NULL);
                if (sendto(sockfd, pkt, ACK_PKT_SIZE, 0,
                           (struct sockaddr *)&serveraddr, serverlen) < 0)
                {
                    fprintf(stderr, "[rudpclient] Error in sendto()\n");
                    exit(EXIT_FAILURE);
                }
                printf("[rudpclient] Received and ACK'd packet %d of size %d\n",
                       ack, nbytes);
                free(pkt);
            }
        }
    } while (nbytes == DATA_PKT_SIZE);
    free(readbuf);

    // Write file
    filepath = malloc(strlen(filename) + strlen(DST_DIR) + 1);
    strcpy(filepath, DST_DIR);
    strcat(filepath, filename);
    fp = fopen(filepath, "w+");
    if (fwrite(databuf, filesize, 1, fp) != 1)
    {
        fprintf(stderr, "[rudpclient] Error on writing file %s\n", filepath);
        exit(EXIT_FAILURE);
    }
    printf("[rudpclient] Successfully wrote to file in path %s\n", filepath);
    fclose(fp);
    free(databuf);
    free(filepath);

    close(sockfd);

    return 0;
}

// Function  : makePacket
// Arguments : char of type, char of window size / sequence number, char * of
//             filename
// Does      : makes a packet of given type. Memory is allocated as needs to be
//             freed
// Returns   : void * of packet
void *
makePacket(char type, char second_field, char *filename)
{
    void *pkt;

    switch (type)
    {
        case TYPE_RRQ:
            pkt = malloc(RRQ_PKT_SIZE);
            bzero(pkt, RRQ_PKT_SIZE);
            memcpy(pkt + TYPE_IDX, &type, TYPE_SIZE);
            memcpy(pkt + WINDOWSIZE_IDX, &second_field, WINDOWSIZE_SIZE);
            memcpy(pkt + FILENAME_IDX, filename, strlen(filename) + 1);
            break;
        case TYPE_ACK:
            pkt = malloc(ACK_PKT_SIZE);
            bzero(pkt, ACK_PKT_SIZE);
            memcpy(pkt + TYPE_IDX, &type, TYPE_SIZE);
            memcpy(pkt + SEQNO_IDX, &second_field, SEQNO_SIZE);
            break;
        default:
            break;
    }

    return pkt;
}

// Function  : handleData
// Arguments : char of type, char of window size / sequence number, char * of
//             filename
// Does      : makes a packet of given type. Memory is allocated as needs to be
//             freed
// Returns   : void * of packet