// Date   : September 16, 2020
// Author : Eric Park

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>

typedef struct CacheBlock
{
    char *key;
    char *value;
    ssize_t size;
    time_t production;
    time_t expiration;
    struct CacheBlock *moreRU, *lessRU; // For recent usage doubly linked list
    struct CacheBlock *hmPrev, *hmNext; // For hashmap chaining
} CacheBlock;

typedef struct
{
    CacheBlock *mru;
    CacheBlock *lru;
    CacheBlock **hashMap; // each hashMap[index] points to the head of chaining
    unsigned numBlocks;
} Cache;

unsigned getPortNumber(int argc, char **argv);
Cache *createCache();
void deleteCache(Cache *cache);
void serveClient(int sockfd, Cache *cache);
ssize_t handleRequest(char *request, char *response, Cache *cache);
ssize_t queryServer(char *host, char *request, char *response);
void putIntoCache(Cache *cache, char *key, char *response,
                  ssize_t response_size);
ssize_t getFromCache(Cache *cache, char *key, char *response);
void organizeCache(Cache *cache);
void removeCacheBlock(Cache *cache, CacheBlock* block);
void printCache(Cache *cache);
unsigned hashKey(char *key);
ssize_t addAgeField(char *reponse, ssize_t response_size, time_t age);

#define MAX_URL_LENGTH 100
#define MAX_CONTENT_SIZE 10000000 // 10MB
#define CACHE_SIZE 10
#define HASH_SIZE 13
#define BACKLOG_SIZE 10
#define CONNECTION_FAIL "Failed to connect to the host\n"
#define DEFAULT_PORT 80
#define MAX_SERVING_SIZE 1000000
#define DEFAULT_MAXAGE 3600

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
        fprintf(stderr, "[httpproxy] Failed to create socket in main()\n");
        exit(EXIT_FAILURE);
    }

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

    // Create cache
    cache = createCache();

    // Serve client
    for (int i = 0; i < MAX_SERVING_SIZE; i++) serveClient(sockfd, cache);

    // Close socket
    close(sockfd);

    // Delete cache
    deleteCache(cache);

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

// Function  : createCache
// Arguments : nothing
// Does      : 1) initializes and allocates a cache for the proxy
//             2) returns the pointer to the cache
// Returns   : Cache * of cache
Cache *
createCache()
{
    Cache *cache;
    int i;

    cache = (Cache *)malloc(sizeof(Cache));
    cache->lru = NULL;
    cache->mru = NULL;
    cache->hashMap = (CacheBlock **)malloc(HASH_SIZE * sizeof(CacheBlock *));
    cache->numBlocks = 0;

    for (i = 0; i < HASH_SIZE; i++) cache->hashMap[i] = NULL;

    return cache;
}

// Function  : deleteCache
// Arguments : Cache * of cache
// Does      : 1) deallocates memory in cache
// Returns   : nothing
void
deleteCache(Cache *cache)
{
    CacheBlock *curr, *prev;

    curr = cache->mru;
    while (curr)
    {
        prev = curr;
        curr = curr->lessRU;
        free(prev->key);
        free(prev->value);
        free(prev);
    }
    
    free(cache->hashMap);
    free(cache);
}

// Function  : serveClient
// Arguments : int of socket file descriptor, and Cache * of cache
// Does      : 1) listens on the socket
//             2) accepts the first connection request
//             3) reads an HTTP request
//             4) retrieve the corresponding HTTP response either from the cache
//                or directly from the server
//             5) writes back the HTTP request
// Returns   : nothing
void
serveClient(int sockfd, Cache *cache)
{
    int client_sockfd;
    char *request;
    char *response;
    struct sockaddr *client_addr;
    ssize_t request_size, response_size, write_size;
    socklen_t *client_addrlen;

    client_addr = malloc(sizeof(struct sockaddr));
    client_addrlen = malloc(sizeof(socklen_t));
    request = malloc(sizeof(char) * MAX_CONTENT_SIZE);
    response = malloc(sizeof(char) * MAX_CONTENT_SIZE);

    // Listen
    if (listen(sockfd, BACKLOG_SIZE) != 0)
    {
        fprintf(stderr, "[httpproxy] Failed listening on socket\n");
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
    bzero(response, MAX_CONTENT_SIZE);
    response_size = handleRequest(request, response, cache);

    // Write response to the connection
    if (write(client_sockfd, response, response_size) < 0)
    {
        fprintf(stderr, "[httpproxy] Failed writing to the connection\n");
        exit(EXIT_FAILURE);
    }
    printf("[httpproxy] Wrote response to the connection\n");

    printCache(cache);

    // Close client socket
    close(client_sockfd);
    printf("[httpproxy] Closed connection\n");

    free(request);
    free(response);
    free(client_addr);
    free(client_addrlen);
}

// Function  : handleRequest
// Arguments : char * of request, char * of response, and Cache * of cache
// Does      : 1) parses the request
//             2) handles the request appropriately and fills up response
// Returns   : ssize_t of response size
ssize_t
handleRequest(char *request, char *response, Cache *cache)
{
    char *line, *key, *host;
    char *str;
    char *line_saveptr, *get_saveptr, *host_saveptr;
    char line_delim[3] = "\r\n";
    char token_delim[2] = " ";
    ssize_t response_size;

    printf("[httpproxy] Handling HTTP request\n");

    // Find key
    str = strdup(request); // strtok_r manipulates the string - make a copy
    for (line = strtok_r(str, line_delim, &line_saveptr); line;
         line = strtok_r(NULL, line_delim, &line_saveptr))
    {
        if (strstr(line, "GET ") == line)
        {
            strtok_r(line, token_delim, &get_saveptr);
            key = strtok_r(NULL, token_delim, &get_saveptr);
        }
        if (strstr(line, "Host: ") == line)
        {
            strtok_r(line, token_delim, &host_saveptr);
            host = strtok_r(NULL, token_delim, &host_saveptr);
        }
    }

    // Query cache
    response_size = getFromCache(cache, key, response);

    if (response_size == 0) // If the key-value pair was not in the cache,
    {
        // Directly query server
        response_size = queryServer(host, request, response);
        putIntoCache(cache, key, response, response_size); // Without age field
        response_size = addAgeField(response, response_size, 0);
    }

    free(str); // for malloc() within strdup()

    return response_size;
}

// Function  : queryServer
// Arguments : char * of request and char * of <hostname>:<portnumber>
// Does      : 1) queries the server of the hostname with the request
//             2) fills up/returns the response from the server
// Returns   : ssize_t of the size of response
ssize_t
queryServer(char *host, char *request, char *response)
{
    int sockfd;
    long portNum;
    char *saveptr, *rest, *addendum;
    char *hostname;
    struct hostent *server;
    struct sockaddr_in server_addr;
    ssize_t response_size, read_size;
    char delim[2] = ":";
    char *buf;

    buf = malloc(MAX_CONTENT_SIZE);

    // Get hostname and port number
    hostname = strtok_r(host, delim, &saveptr);
    addendum = strtok_r(NULL, delim, &saveptr);
    if (addendum)
        portNum = strtol(addendum, &rest, 10);
    else 
        portNum = DEFAULT_PORT;

    // Create TCP socket
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0)
    {
        fprintf(stderr, "[httpproxy] Failed to create socket in queryServer\n");
        exit(EXIT_FAILURE);
    }

    // Get server information
    server = gethostbyname(hostname);
    if (server == NULL)
    {
        fprintf(stderr, "[httpproxy] No such host as %s\n", hostname);
        fprintf(stderr, "[httpproxy] h_errno: %d\n", h_errno);
        strcpy(response, "No such host indicated by the hostname\n");
        return 0;
    }

    // Build the server's Internet address
    bzero((char *) &server_addr, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    bcopy((char *)server->h_addr,(char *)&server_addr.sin_addr.s_addr,
           server->h_length);
    server_addr.sin_port = htons(portNum);

    // Connect with the server
    if (connect(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr))
        < 0) 
    {
        fprintf(stderr, "[httpproxy] Failed to connect to %s on port %ld\n",
                hostname, portNum);
        strcpy(response, CONNECTION_FAIL);
        return 0;
    }

    // Send the HTTP request to the server
    if(write(sockfd, request, strlen(request)) < 0)
        fprintf(stderr, "[httpproxy] Failed to write to %s\n", hostname);
    printf("[httpproxy] Querying host %s\n", hostname);
    
    // Read the HTTP response from the server
    response_size = read(sockfd, response, MAX_CONTENT_SIZE);
    while ((read_size = read(sockfd, buf, MAX_CONTENT_SIZE)) > 0)
    {
        memcpy(response + response_size, buf, read_size);
        response_size += read_size;
    }
    if (response_size < 0)
        fprintf(stderr, "[httpproxy] Failed to read from %s\n", hostname);
    printf("[httpproxy] Received response from host %s\n", hostname);

    close(sockfd);
    free(buf);

    return response_size;
}

// Function  : putIntoCache
// Arguments : Cache * of cache, char * of key, char * of response, and ssize_t
//             of response_size
// Does      : 1) Hashes the key
//             2) Puts the key-response pair in an appropriate place in cache
// Returns   : nothing
void
putIntoCache(Cache *cache, char *key, char *response, ssize_t response_size)
{
    CacheBlock *newBlock, *currBlock;
    char *line_saveptr, *cache_saveptr, *rest;
    char *str, *line, *token;
    unsigned hash;
    long maxAge = DEFAULT_MAXAGE;
    char line_delim[3] = "\r\n";
    char cache_delim[9] = "max-age=";

    hash = hashKey(key);
    printf("[httpproxy] Caching key %s into cache\n", key);

    if (cache->numBlocks == CACHE_SIZE)
    {
        organizeCache(cache);
        if (cache->numBlocks == CACHE_SIZE) // If none were stale
            removeCacheBlock(cache, cache->lru);
    }

    // Find max age
    str = strdup(response); // strtok_r manipulates the string
    for (line = strtok_r(str, line_delim, &line_saveptr); line;
         line = strtok_r(NULL, line_delim, &line_saveptr))
    {
        if (strstr(line, "Cache-Control: ") == line)
        {
            strtok_r(line, cache_delim, &cache_saveptr);
            token = strtok_r(NULL, cache_delim, &cache_saveptr);
            if (token) maxAge = strtol(token, &rest, 10);
        }
    }

    newBlock = malloc(sizeof(*newBlock));
    newBlock->key = malloc(strlen(key) + 1);
    memcpy(newBlock->key, key, strlen(key) + 1);
    newBlock->value = malloc(response_size);
    memcpy(newBlock->value, response, response_size);
    newBlock->size = response_size;
    newBlock->production = time(NULL);
    newBlock->expiration = newBlock->production + (time_t)maxAge;
    
    // Recent usage linked list operations
    newBlock->moreRU = NULL; // New block is always the MRU
    newBlock->lessRU = cache->mru;
    if (!cache->lru) cache->lru = newBlock;
    if (cache->mru) cache->mru->moreRU = newBlock;
    cache->mru = newBlock;

    // Hash map operations
    currBlock = cache->hashMap[hash];
    if (currBlock) // If there's something already in hash at position hash
    {
        while (currBlock->hmNext) currBlock = currBlock->hmNext;
        currBlock->hmNext = newBlock;
        newBlock->hmPrev = currBlock;
    }
    else // If there's nothing at hashed yet
    {
        cache->hashMap[hash] = newBlock;
        newBlock->hmPrev = NULL;
    }
    newBlock->hmNext = NULL; // New block always at the end of a hash chaining

    cache->numBlocks++;

    free(str);
}

// Function  : getFromCache
// Arguments : Cache * of cache, char * of key, and char * of response
// Does      : 1) Searches HTTP response in cache for the given key
//             2) If found, inserts "Age" field to the HTTP header
//             2) returns/fills in the response with the corresponding response
// Returns   : ssize_t of response size
ssize_t
getFromCache(Cache *cache, char *key, char *response)
{
    CacheBlock *curr;
    unsigned hash;
    time_t age;
    ssize_t response_size;

    hash = hashKey(key);

    organizeCache(cache);

    curr = cache->hashMap[hash];
    while (curr)
    {
        if (strcmp(key, curr->key) == 0)
        {
            response_size = curr->size;
            memcpy(response, curr->value, response_size);
            age = time(NULL) - curr->production;

            // Add age field
            response_size = addAgeField(response, response_size, age);

            printf("[httpproxy] Retrieving cache with key %s\n", key);

            // Update recent usage linked list
            if (curr != cache->mru)
            {
                if (curr == cache->lru)cache->lru = curr->moreRU;
                if (curr->moreRU) curr->moreRU->lessRU = curr->lessRU;
                if (curr->lessRU) curr->lessRU->moreRU = curr->moreRU;
                curr->moreRU = NULL;
                curr->lessRU = cache->mru;
                cache->mru->moreRU = curr;
                cache->mru = curr;
            }

            return response_size;
        }

        curr = curr->hmNext;
    }

    return 0;
}

// Function  : organizeCache
// Arguments : Cache * of cache
// Does      : 1) removes stale cache block
// Returns   : nothing
void
organizeCache(Cache *cache)
{
    CacheBlock *curr, *next;

    printf("[httpproxy] Organizing cache...\n");

    curr = cache->mru;
    while (curr)
    {
        next = curr->lessRU;

        if (curr->expiration < time(NULL)) // if stale
        {
            removeCacheBlock(cache, curr);
        }

        curr = next;
    }
}

// Function  : removeCacheBlock
// Arguments : Cache * of cache, CacheBlock * of block that needs to be deleted
// Does      : 1) This function is called when space needs to be cleared up
//             2) removes any stale cache block
//             3) if it's still full, remove the LRU block
// Returns   : nothing
void
removeCacheBlock(Cache *cache, CacheBlock *block)
{
    unsigned hash;

    printf("[httpproxy] Removing stale cache with key %s\n", block->key);

    // Recent usage linked list operation: remove
    if (block == cache->mru) cache->mru = block->lessRU;
    if (block == cache->lru) cache->lru = block->moreRU;
    if (block->moreRU) block->moreRU->lessRU = block->lessRU;
    if (block->lessRU) block->lessRU->moreRU = block->moreRU;

    // Hash map chaining operation: remove
    hash = hashKey(block->key);
    if (block == cache->hashMap[hash]) cache->hashMap[hash] = block->hmNext;
    if (block->hmPrev) block->hmPrev->hmNext = block->hmNext;
    if (block->hmNext) block->hmNext->hmPrev = block->hmPrev;

    free(block->key);
    free(block->value);
    free(block);
    cache->numBlocks--;

    printf("[httpproxy] Done removing cache block\n");
}

// Function  : printCache
// Arguments : Cache * of cache, char * of key, and char * of response
// Does      : 1) Searches the cache for the key
//             2) returns/fills in the response with the corresponding response
// Returns   : nothing
void
printCache(Cache *cache)
{
    CacheBlock *curr, *prev;
    unsigned counter = 0;

    curr = cache->mru;
    while (curr)
    {
        prev = curr;
        curr = curr->lessRU;
        printf("[httpproxy] Cache Block %d\n", counter);
        printf("[httpproxy]         Key: %s\n", prev->key);
        printf("[httpproxy]         Size: %ld\n", prev->size);
        printf("[httpproxy]         Production: %d\n", prev->production);
        printf("[httpproxy]         Expiration: %d\n", prev->expiration);
        counter++;

        if (counter > CACHE_SIZE)
        {
            printf("[httpproxy] Cache size violated!\n");
            break;
        }
    }
}

// Function  : hashKey
// Arguments : char * of a string key
// Does      : 1) hashes the string key into an integer index
// Returns   : unsigned of hashed value
// Reference : The C Programming Language (Kernighan & Ritchie), Section 6.6
unsigned
hashKey(char *key)
{
    unsigned hashval;

    for (hashval = 0; *key != '\0'; key++)
        hashval = *key + 31 * hashval;

    return hashval % HASH_SIZE;
}

// Function  : addAgeField
// Arguments : char * of response, ssize_t of response_size, and time_t of age
// Does      : 1) adds the age field to the HTTP response
// Returns   : ssize_t of new response_size
ssize_t
addAgeField(char *response, ssize_t response_size, time_t age)
{
    size_t offset;
    char *originalCopy;
    char *save_ptr;
    char *str, *token;
    char line_delim[3] = "\r\n";
    char age_header[6] = "Age: ";
    char ageStr[256];

    printf("[httpproxy] Adding age field\n");

    offset = 0;
    originalCopy = malloc(response_size);
    str = malloc(response_size);
    memcpy(originalCopy, response, response_size);
    memcpy(str, response, response_size);
    sprintf(ageStr, "%ld", age);

    // Add age field
    token = strtok_r(str, line_delim, &save_ptr); // First line
    memcpy(response + offset, token, strlen(token));
    offset += strlen(token);
    memcpy(response + offset, line_delim, strlen(line_delim));
    offset += strlen(line_delim);
    memcpy(response + offset, age_header, strlen(age_header));
    offset += strlen(age_header);
    memcpy(response + offset, ageStr, strlen(ageStr));
    offset += strlen(ageStr);
    memcpy(response + offset, line_delim, strlen(line_delim));
    offset += strlen(line_delim);
    memcpy(response + offset,
           originalCopy + strlen(token) + strlen(line_delim),
           response_size - (strlen(token) + strlen(line_delim)));

    free(str);
    free(originalCopy);

    return response_size + strlen(age_header) + (2 * strlen(line_delim)) + 
           strlen(ageStr);
}