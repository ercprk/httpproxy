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
    struct CacheBlock *ruPrev, *ruNext; // For recent usage doubly linked list
    struct CacheBlock *hmPrev, *hmNext; // For hashmap chaining
} CacheBlock;

typedef struct Cache
{
    CacheBlock *mru;
    CacheBlock *lru;
    CacheBlock **hashMap;
    unsigned numBlocks;
} Cache;

unsigned getPortNumber(int argc, char **argv);
Cache *createCache();
void deleteCache(Cache *cache);
void serveClient(int sockfd);
char *handleRequest(char *request);

#define MAX_URL_LENGTH 100
#define MAX_CONTENT_SIZE 1000000 // 10MB
#define CACHE_SIZE 10
#define HASH_SIZE 13
#define BACKLOG_SIZE 10
#define MAX_SERVING 5

int
main(int argc, char **argv)
{
    int sockfd;
    unsigned portNum;
    struct sockaddr_in addr;
    Cache *cache;

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
    if (bind(sockfd, (struct sockaddr *)&addr, sizeof(addr)) < 0) 
    {
        fprintf(stderr, "[httpproxy] Failed to bind socket to port %d\n",
                portNum);
        exit(EXIT_FAILURE);
    }
    printf("[httpproxy] Binded socket to port %d\n", portNum);

    // Create cache
    cache = createCache();

    // Serve client
    for (int i = 0; i < MAX_SERVING; i++) serveClient(sockfd);

    // Close socket
    close(sockfd);
    printf("[httpproxy] Closed TCP socket\n");

    // Delete cache
    deleteCache(cache);

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

// Function  : createCache
// Arguments : nothing
// Does      : 1) initializes and allocates a cache for the proxy
//             2) returns the pointer to the cache
// Returns   : Cache * of cache
Cache *
createCache()
{
    Cache *cache;

    cache = (Cache *)malloc(sizeof(Cache));
    cache->lru = (CacheBlock *)malloc(sizeof(CacheBlock));
    cache->mru = (CacheBlock *)malloc(sizeof(CacheBlock));
    cache->hashMap = (CacheBlock **)malloc(HASH_SIZE * sizeof(CacheBlock *));
    cache->numBlocks = 0;

    return cache;
}

// Function  : putIntoCache
// Arguments : Cache * of cache, char * of key, and char * of value
// Does      : 1) Cach
//             2) returns the pointer to the cache
// Returns   : Cache * of cache
//void
//putIntoCache(Cache *cache, )

// Function  : deleteCache
// Arguments : Cache * of cache
// Does      : 1) deallocates memory in cache
// Returns   : nothing
void
deleteCache(Cache *cache)
{
    free(cache->hashMap);
    free(cache->lru);
    free(cache->mru);
    free(cache);
}

// Function  : serveClient
// Arguments : int of socket file descriptor
// Does      : 1) listens on the socket
//             2) accepts the first connection request
//             3) reads an HTTP request
//             4) retrieve the corresponding HTTP response either from the cache
//                or directly from the server
//             5) writes back the HTTP request
// Returns   : nothing
void
serveClient(int sockfd)
{
    int client_sockfd;
    char *request;
    char *response;
    struct sockaddr *client_addr;
    ssize_t request_size;
    socklen_t *client_addrlen;

    client_addr = malloc(sizeof(struct sockaddr));
    client_addrlen = malloc(sizeof(socklen_t));
    request = malloc(sizeof(char) * MAX_CONTENT_SIZE);

    // Listen
    if (listen(sockfd, BACKLOG_SIZE) != 0)
    {
        fprintf(stderr, "[httpproxy] Failed listening to port\n");
        fprintf(stderr, "[httpproxy] errno: %d\n", errno);
        exit(EXIT_FAILURE);
    }
    printf("[httpproxy] Listening...\n");

    // Accept the first connection request
    *client_addrlen = sizeof(struct sockaddr_in);
    client_sockfd = accept(sockfd, client_addr, client_addrlen);
    if (client_sockfd < 0)
    {
        fprintf(stderr, "[httpproxy] Failed accepting connection request\n");
        exit(EXIT_FAILURE);
    }
    printf("[httpproxy] Accepted connection request\n");

    // Read from the connection
    bzero(request, sizeof(request));
    request_size = read(client_sockfd, request, MAX_CONTENT_SIZE);
    if (request_size < 0)
    {
        fprintf(stderr, "[httpproxy] Failed reading from the connection\n");
        exit(EXIT_FAILURE);
    }
    request[request_size] = 0; // null-termination for strtok_r
    printf("[httpproxy] Read from the connection\n");

    // Handle request
    handleRequest(request);

    // Write response to the connection
    if (0/*write(client_sockfd, response, MAX_CONTENT_SIZE) < 0*/)
    {
        fprintf(stderr, "[httpproxy] Failed writing to the connection\n");
        exit(EXIT_FAILURE);
    }
    printf("[httpproxy] Wrote response to the connection\n");

    // Close client socket
    close(client_sockfd);
    printf("[httpproxy] Closed connection\n");

    free(request);
    free(client_addr);
    free(client_addrlen);
}

// Function  : handleRequest
// Arguments : char * of request
// Does      : 1) parses the request for 
//             2) returns the pointer to the cache
// Returns   : char * of response
char *
handleRequest(char *request)
{
    char *line, *line_saveptr, *token_saveptr, *key, *str, *host;
    char line_delim[2] = "\n";
    char token_delim[2] = " ";

    printf("[httpproxy] Handling request\n");

    // Find key
    str = strdup(request); // strtok_r manipulates the string, so make a copy
    for (line = strtok_r(str, line_delim, &line_saveptr); line;
         line = strtok_r(NULL, line_delim, &line_saveptr))
    {
        if (strstr(line, "GET ") == line)
        {
            strtok_r(line, token_delim, &token_saveptr);
            key = strtok_r(NULL, token_delim, &token_saveptr);
        }
        
    }
    printf("request: %s\n", request);



    free(str); // for malloc() within strdup()

    return "";
}