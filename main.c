// Date   : September 16, 2020
// Author : Eric Park
// Status : This work is not complete. Memory management is very bad.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <errno.h>
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
void serveClient(int sockfd);

#define MAX_URL_LENGTH 100
#define MAX_HTTP_GET_LENGTH MAXURL_LENGTH + 4
#define MAX_CONTENT_SIZE 1000000 // 10MB
#define CACHE_SIZE 10
#define BACKLOG_SIZE 10

int
main(int argc, char **argv)
{
    int sockfd;
    unsigned portNum;
    struct sockaddr_in addr;

    // Handle input and get port number
    portNum = getPortNumber(argc, argv);

    // Create a TCP socket
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0)
    {
        fprintf(stderr, "[httpproxy] Failed to create socket\n");
        exit(EXIT_FAILURE);
    }
    printf("[httpproxy] Created TCP socket\n");

    // Bind socket to the port number
    bzero((char *) &addr, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(portNum);
    if (bind(sockfd, (struct sockaddr *) &addr, sizeof(addr)) < 0) 
    {
        fprintf(stderr, "[httpproxy] Failed to bind socket to port %d\n",
                portNum);
        exit(EXIT_FAILURE);
    }
    printf("[httpproxy] Binded socket to port %d\n", portNum);

    // Serve client
    while (1) serveClient(sockfd);

    // Close socket
    close(sockfd);
    printf("[httpproxy] Closed TCP socket\n");

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
        fprintf(stderr, "[httpproxy] Usage: %s <port number>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    // Gets port number and checks validity
    portNum = strtol(argv[1], &rest, 10);
    if (portNum < 1 || portNum > 32767)
    {
        fprintf(stderr, "[httpproxy] Port number, %s, is not valid\n", argv[1]);
        exit(EXIT_FAILURE);
    }
    printf("[httpproxy] Checked in valid port number: %ld\n", portNum);

    return (unsigned)portNum;
}

// Function  : serveClient
// Arguments : int of socket file descriptor
// Does      : 1) listens on the socket
//             2) accepts the first connection request
//             3) returns the port number
// Returns   : unsigned int of port number
void
serveClient(int sockfd)
{
    int client_sockfd;
    char buf[MAX_CONTENT_SIZE];
    struct sockaddr_in client_addr;
    socklen_t client_addrlen;

    // Listen
    if (listen(sockfd, BACKLOG_SIZE) != 0)
    {
        fprintf(stderr, "[httpproxy] Failed listening to port\n");
        fprintf(stderr, "[httpproxy] errno: %d\n", errno);
        exit(EXIT_FAILURE);
    }
    printf("[httpproxy] Listening...\n");

    // Accept the first connection request
    client_addrlen = sizeof(client_addr);
    client_sockfd = accept(sockfd, (struct sockaddr *)&client_addr,
                           &client_addrlen);
    printf("[httpproxy] Accepted connection request\n");

    // Read from the connection
    bzero(buf, sizeof(buf));
    if (read(client_sockfd, buf, MAX_CONTENT_SIZE) < 0)
    {
        fprintf(stderr, "[httpproxy] Failed reading from the connection\n");
        exit(EXIT_FAILURE);
    }
    printf("[httpproxy] Read:\n");
    printf("%s", buf);

    // Write response to the connection
    if (write(client_sockfd, buf, MAX_CONTENT_SIZE) < 0)
    {
        fprintf(stderr, "[httpproxy] Failed writing to the connection\n");
        exit(EXIT_FAILURE);
    }

    // Close client socket
    close(client_sockfd);
    printf("[httpproxy] Closed connection\n");
}