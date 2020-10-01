// Date   : September 16, 2020
// Author : Eric Park
// Status : This work is not complete. Memory management is very bad.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>

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

unsigned handleInput(int argc, char **argv, char **filename);
char *storeCommands(char *filename);
void processCommands(char *commands, unsigned cacheSize, FILE *fd);
void putOperation(Cache *cache, char *putCommand);
void putIntoCache(Cache *cache, char *key, char *value, time_t expiration);
char *getOperation(Cache *cache, char *getCommand);
char *getFromCache(Cache *cache, char *key);
unsigned hashKey(char *key, unsigned hashSize);
void cleanUpCache(Cache *cache);
void printCache(Cache *cache);

#define MAX_VALUE_SIZE 1000000

int
main(int argc, char **argv)
{
    char *filename, *outfilename;
    char *commands, *saveptr;
    FILE *fp;
    unsigned cacheSize;

    // Input handling
    cacheSize = handleInput(argc, argv, &filename);

    // Store commands
    commands = storeCommands(filename);

    // Generate output filename
    outfilename = malloc(strlen(filename) + 8);
    strcpy(outfilename, strtok_r(filename, ".", &saveptr));
    strcat(outfilename, "_output.");
    strcat(outfilename, strtok_r(NULL, ".", &saveptr));

    // Open output file
    fp = fopen(outfilename, "w");
    if (fp == NULL)
    {
        fprintf(stderr, "Could not open the file: %s", outfilename);
        exit(EXIT_FAILURE);
    }

    // Process commands
    processCommands(commands, cacheSize, fp);

    // Close output file
    if (fclose(fp) != 0)
    {
        fprintf(stderr, "Error closing file: %s", outfilename);
        exit(EXIT_FAILURE);
    }

    free(commands);
    free(outfilename);

    return 0;
}

// Function  : handleInput
// Arguments : int of argc, and char ** of argv, from the main function.
//             char * of filename will also be passed in for returning the
//             filename
// Does      : checks for the two arguments, checks for the existence of the
//             input file, and checks that the cache size is valid
// Returns   : unsigned int of cache size
unsigned
handleInput(int argc, char ** argv, char ** filename)
{
    struct stat statBuf;
    char *rest;
    long cacheSize;

    // Checks for two arguments
    if (argc != 3)
    {
        fprintf(stderr, "Usage: %s <input file> <size of cache>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    // Checks for file existence
    if (stat(argv[1], &statBuf) != 0)
    {
        fprintf(stderr, "Could not located the file: %s\n", argv[1]);
        exit(EXIT_FAILURE);
    }
    *filename = argv[1];

    // Checks for the validity of the cache size argument
    cacheSize = strtol(argv[2], &rest, 10);
    if (!(argv[2] != "\0" && *rest == '\0') || cacheSize <= 0) // If invalid
    {
        fprintf(stderr, "Size of cache, %s, is not valid\n", argv[2]);
        exit(EXIT_FAILURE);
    }

    return (unsigned) cacheSize;
}

// Function  : storeCommands
// Arguments : char * of filename
// Does      : reads the file indicated by the filename, stores the entirety of
//             the text into a buffer, returns the buffer
// Returns   : the buffer containing the entirety of the text file
char *
storeCommands(char *filename)
{
    FILE *fp;
    long size;
    char *buffer;

    // Open file
    fp = fopen(filename, "r");
    if (fp == NULL)
    {
        fprintf(stderr, "Could not open the file: %s", filename);
        exit(EXIT_FAILURE);
    }

    // Compute the size of the text file
    fseek(fp, 0L, SEEK_END);
    size = ftell(fp);
    rewind(fp);

    // Allocate the buffer and read from the file
    buffer = (char *) malloc(size + 1); // +1 for null-termination
    if (fread(buffer, size, 1, fp) != 1)
    {
        fprintf(stderr, "Error reading file: %s", filename);
        exit(EXIT_FAILURE);
    }

    // Close file
    if (fclose(fp) != 0)
    {
        fprintf(stderr, "Error closing file: %s", filename);
        exit(EXIT_FAILURE);
    }

    return buffer;
}

// Function  : processCommands
// Arguments : char * of commands, delimited by newlines, an unsigned int of
//             cacheSize, and a file handle to write output to
// Does      : allocates cache, iterates through commands, and carries out
//             corresponding operations
// Returns   : nothing
void
processCommands(char *commands, unsigned cacheSize, FILE *fp)
{
    Cache *cache;
    char *command, *saveptr, *retval;

    // Initialize cache data structures
    cache = malloc(sizeof(Cache));
    cache->capacity = cacheSize;
    cache->numBlocks = 0;
    cache->cacheDll = malloc(sizeof(CacheDLL));
    cache->cacheHashMap = malloc(sizeof(CacheHashMap));
    cache->cacheHashMap->map = malloc(sizeof(cacheSize * sizeof(CacheBlock *)));
    for (int i = 0; i < cacheSize; i++)
        cache->cacheHashMap->map[i] = NULL;

    // Iterate through commands and do corresponding operations
    for (command = strtok_r(commands, "\n", &saveptr); command;
         command = strtok_r(NULL, "\n", &saveptr))
    {
        switch (command[0])
        {
            case 'P':
                putOperation(cache, command);
                break;
            case 'G':
                retval = getOperation(cache, command);
                fwrite(retval, strlen(retval), 1, fp);
                fwrite("\n", 1, 1, fp);
                break;
            default:
                fprintf(stderr, "Command %s is not a valid command\n", command);
                exit(EXIT_FAILURE);
        }
    }

    // Free cache
    CacheBlock* temp;
    CacheBlock* curr = cache->cacheDll->head;
    while (curr)
    {
        printf("%s\n", curr->value);
        temp = curr;
        curr = curr->llNext;
        free(temp);
    }
    free(cache->cacheHashMap->map);
    free(cache->cacheHashMap);
    free(cache->cacheDll);
    free(cache);

    //printf("%s\n", output);
}

// Function  : putOperation
// Arguments : struct cacheBlock * of cache, long int of cacheSize, and char *
//             of a PUT command
// Does      : parses the PUT command, puts the "value" of object at the "key"
//             block of cache
// Returns   : nothing
void
putOperation(Cache *cache, char *putCommand)
{
    char *key;
    char *value;
    time_t maxAge;
    char *token, *saveptr;

    // Parse the PUT command into key, value, and maxAge
    for (token = strtok_r(putCommand, "\\", &saveptr); token;
         token = strtok_r(NULL, "\\", &saveptr))
    {
        switch (token[0])
        {
            case 'P': // PUT:
                key = token + 5; // +5 for "PUT: "
                break;
            case 'O': // OBJECT:
                value = token + 8; // +8 for "OBJECT: "
                break;
            case 'M': // MAX-AGE:
                maxAge = strtol(token + 9, NULL, 10); // + 9 for "MAX-AGE: "
                break;
            default:
                fprintf(stderr, "%s is not a valid command part\n", token);
                exit(EXIT_FAILURE);
        }
    }

    putIntoCache(cache, key, value, time(NULL) + maxAge);
}

// Function  : putIntoCache
// Arguments : Cache * of cache, a key-value pair of char *, and time_t of
//             expiration time
// Does      : puts the key-value pair into a cache structure
// Returns   : nothing
void
putIntoCache(Cache *cache, char *key, char *value, time_t expiration)
{
    CacheBlock *cacheBlock, *cbPtr;
    unsigned hashMapIndex = hashKey(key, cache->capacity);

    cacheBlock = malloc(sizeof(CacheBlock));
    cacheBlock->key = key;
    cacheBlock->value = value;
    cacheBlock->expiration = expiration;
    cacheBlock->llPrev = NULL; // Always at the head of the doubly linked list
    cacheBlock->hmNext = NULL; // Always at the tail of the hash chaining

    if (cache->numBlocks == cache->capacity)
        cleanUpCache(cache);

    if (cache->numBlocks == 0)
    {
        cacheBlock->llNext = NULL;
        cacheBlock->hmPrev = NULL;
        cache->cacheDll->head = cacheBlock;
        cache->cacheDll->tail = cacheBlock;
        cache->cacheHashMap->map[hashMapIndex] = cacheBlock;
    }
    else
    {
        // Cache linked list operations
        cacheBlock->llNext = cache->cacheDll->head;
        cache->cacheDll->head->llPrev = cacheBlock;
        cache->cacheDll->head = cacheBlock;

        // Cache hashMap operations: chaining
        cbPtr = cache->cacheHashMap->map[hashMapIndex];
        while (cbPtr->hmNext != NULL)
            cbPtr = cbPtr->hmNext;
        cacheBlock->hmPrev = cbPtr;
        cbPtr->hmNext = cacheBlock;
    }
}

// Function  : getOperation
// Arguments : Cache * of cache and char * of a GET command
// Does      : parses the GET command, retrieves the value corresponding to the
//             key from the cache
// Returns   : char * of returned value
char *
getOperation(Cache *cache, char *getCommand)
{
    char *key;

    // Parse the GET command into key
    key = getCommand + 5; // +5 for "GET: "

    return getFromCache(cache, key);
}

// Function  : getFromCache
// Arguments : Cache * of cache and char * of key
// Does      : returns the corresponding value for the key. If no such key is
//             found in the cache, it returns "NA"
// Returns   : char * of value
char *
getFromCache(Cache *cache, char *key)
{
    unsigned hashMapIndex;
    CacheBlock *cbPtr;

    hashMapIndex = hashKey(key, cache->capacity);
    cbPtr = cache->cacheHashMap->map[hashMapIndex];
    while (cbPtr)
    {
        if (strcmp(key, cbPtr->key) == 0)
            return cbPtr->value;
            
        cbPtr = cbPtr->hmNext;
    }

    return "NA";
}

// Function  : hashKey
// Arguments : char * of a string key and long int of hash size
// Does      : hashes the string key into an integer index
// Returns   : long int of hashed value
// Reference : The C Programming Language (Kernighan & Ritchie), Section 6.6
unsigned
hashKey(char *key, unsigned hashSize)
{
    unsigned hashval;

    for (hashval = 0; *key != '\0'; key++)
        hashval = *key + 31 * hashval;

    return hashval % hashSize;
}

// Function  : cleanUpCache
// Arguments : Cache * of cache
// Does      : iterates through each cache block and removes stale cache blocks.
//             Since this function is called when the PUT operation notices that
//             the cache is full, if no cache block is stale, it removes the LRU
//             cache block 
// Returns   : nothing
void
cleanUpCache(Cache *cache)
{
    CacheBlock *curr, *temp;

    curr = cache->cacheDll->head;
    while (curr)
    {
        if (curr->expiration < time(NULL))
        {
            if (curr->hmPrev)
                curr->hmPrev->hmNext = curr->hmNext;
            else
                cache->cacheHashMap->map[hashKey(curr->key, cache->capacity)] =
                    curr->hmNext;

            if (curr == cache->cacheDll->head)
            {
                cache->cacheDll->head = curr->llNext;
                curr = curr->llNext;
                free(curr->llPrev);
                curr->llPrev = NULL;
            }
            else if (curr == cache->cacheDll->tail)
            {
                cache->cacheDll->tail = curr->llPrev;
                curr->llPrev->llNext = NULL;
                free(curr);
                curr = NULL;
            }
            else
            {
                temp = curr;
                curr = curr->llNext;
                curr->llPrev = temp->llPrev;
                temp->llPrev->llNext = curr;
                free(temp);
            }
        }
    }

    // After iteration, if the cache is still full, we remove LRU
    if (cache->numBlocks == cache->capacity)
    {
        if (curr->hmPrev)
            curr->hmPrev->hmNext = curr->hmNext;
        else
            cache->cacheHashMap->map[hashKey(curr->key, cache->capacity)] =
                curr->hmNext;

        curr = cache->cacheDll->tail;
        cache->cacheDll->tail = curr->llPrev;
        curr->llPrev->llNext = NULL;
        free(curr);
    }
}

// Function  : printCache
// Arguments : Cache * of cache
// Does      : prints the contents of the cache
// Returns   : nothing
void
printCache(Cache *cache)
{
    CacheBlock* cbPtr;

    cbPtr = cache->cacheDll->head;
    while (cbPtr)
    {
        printf("KEY: %s / VALUE: %s / Expiration: %ld\n", cbPtr->key,
               cbPtr->value, cbPtr->expiration);
        cbPtr = cbPtr->llNext;
    }
}