# Memory, Strings, & Data Structures in C

## Status
This work is not complete. Memory management is not safe.

## Design
### Big Steps
1. Input Handling: text filename, size of cache
2. Store commands
3. Process commands
4. Produce output

### Cache Structure
```
typedef struct CacheBlock
{
    char *key;
    char *value;
    time_t expiration;
    struct CacheBlock *llPrev, *llNext; // For doubly linked list
    struct CacheBlock *hmPrev, *hmNext; // For hashmap chaining
} CacheBlock;

typedef struct CacheDLL // Doubly Linked List
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
```