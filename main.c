// Date   : September 16, 2020
// Author : Eric Park
// Status : This work is not complete. Memory management is very bad.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>

typedef struct CacheBlock
{
    char *key;
    char *value;
    time_t expiration;
    struct CacheBlock *llPrev, *llNext; // For doubly linked list
    struct CacheBlock *hmPrev, *hmNext; // For hashmap chaining
} CacheBlock;

// Doubly linked list, used for keeping track of LRU. MRU is the head, LRU is
// the tail.
typedef struct CacheDLL
{
    CacheBlock *head, *tail; // head is most recently used
} CacheDLL;

typedef struct CacheHashMap
{
    CacheBlock **map;
} CacheHashMap;

typedef struct Cache
{
    CacheDLL *cacheDll;
    CacheHashMap *cacheHashMap;
    unsigned numBlocks;
    unsigned capacity;
} Cache;

unsigned getPortNumber(int argc, char **argv);
void serveClient(unsigned portNum);

#define MAX_VALUE_SIZE 1000000

int
main(int argc, char **argv)
{
    unsigned portNum;

    // Handle input and get port number
    portNum = getPortNumber(argc, argv);

    while (1)
        serveClient(portNum);

    return 0;
}

// Function  : getPortNumber
// Arguments : int of argc and char ** of argv 
// Does      : 1) checks for the singular argument, port number
//             2) checks the validity of the port number
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
        fprintf(stderr, "Usage: %s <port number>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    // Gets port number and checks validity
    portNum = strtol(argv[1], &rest, 10);
    if (portNum <= 0) // If invalid
    {
        fprintf(stderr, "Port number, %s, is not valid\n", argv[1]);
        exit(EXIT_FAILURE);
    }

    fprintf(stdout, "Checked in valid port number\n");

    return (unsigned)portNum;
}

// Function  : serveClient
// Arguments : unsigned of port number
// Does      : 1) creates 
//             2) checks the validity of the port number
//             3) returns the port number
// Returns   : unsigned int of port number
void
serveClient(unsigned portNum)
{
    int sockfd;
    struct sockaddr_in addr;

    // Create a TCP socket
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0)
    {
        fprintf(stderr, "Failed to create socket\n");
        exit(EXIT_FAILURE);
    }

    // Bind socket to the port number
    bzero((char *) &addr, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(portNum);
    if (bind(sockfd, (struct sockaddr *) &addr, sizeof(addr)) < 0) 
    {
        fprintf(stderr, "Failed to bind socket\n");
        exit(EXIT_FAILURE);
    }

    // Close socket
    close(sockfd);
}