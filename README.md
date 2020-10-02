# HTTP Proxy

## Specifications

1. Only supports IPv4.

2. Only handles GET requests.

## Requirements

### HTTP Header Parsing

#### Client Request Header

1. Focus on the following parts:
```
GET http://www.example.com/some/path HTTP/1.1
...
Host: www.example.com
...
```

2. If a non-standard port is used to connect to the server it is appended
to the domain in the ​`Host` field, making it a good resource for making a
connection to the server.

3. If the port number for the server is included in the HTTP request it
should be respected, otherwise port 80 can be assumed.

4. There is no need to worry about any query parameters at the end of URLs. 
Assume that any data the server needs to serve a `GET` request will be included 
as part of the URL path.

#### Server Response Header

1. Focus on the following parts:
```
HTTP/1.1 200 OK
...
Cache-Control: max-age=N
...
Content-Length: 500
```

2. If there is no caching policy the `Cache-Control`​ header will not be present.
Responses that specify no ​`Cache-Control`​ or when `max-age`​ is not specified
should be cacheable and the content considered fresh for one hour.

3. If the time the request is made plus the ​ max-age​ is exceeded, the data is
considered stale and the proxy should refetch from the server.

4. There is no need to implement other values for `Cache-Control`​ (`no-cache`​,
​`no-store​`, ​`private​`, etc​).

5. There is no need to do any special handling for any of the numerous
HTTP status codes. For example, if the resource the client is requesting is not
available the first line of the response will probably read
`HTTP/1.1 404 Not Found`​ but the proxy does not need to handle this any
differently.

6. The proxy should also modify the HTTP header in the response to the client to
include the current ​`Age`​ of the data, `Age`​ is the number of seconds since the
data was first stored in the cache, meaning ​`0 < Age < max-age​` and once
`Age == max-age` ​the data is considered stale.

### Cache

1. The cache should store the responses of the 10 most recently `GET` requested
URLs. If the data for a new request is in the cache and is fresh, the proxy
should return the cached data to the client without passing the request to the
server. If the data is not in the cache the request should be forwarded to the
server and the response should be added to the cache. If the cache already has
10 other objects stored it should prioritize purging a stale item. If none are
stale then it should remove the least recently accessed one.

2. Assume that the name of the object being cached will be less than 100
characters. Similarly, the maximum size of the object being cached will be less
than 10MB.

3. The cache should consider different ports on the same domain to contain
different content and cache them separately.

4. The cache should cache the server’s response in its entirety.

## Implementation

### Cache

```
typedef struct CacheBlock
{
    char *key;
    char *value;
    time_t production;
    time_t expiration;
    struct CacheBlock *llPrev, *llNext; // For doubly linked list
    struct CacheBlock *hmPrev, *hmNext; // For hashmap chaining
} CacheBlock;

// Doubly linked list, used for keeping track of LRU. MRU is the head, LRU is
// the tail.
typedef struct CacheDLL
{
    CacheBlock *head, *tail; // head is most recently used (MRU)
} CacheDLL;

typedef struct Cache
{
    CacheDLL *dll;
    CacheBlock **hashMap;
    unsigned numBlocks;
    unsigned capacity;
} Cache;
```

### Functions

1. `main` - Handles the command line input, and loops infinitely to serve client
requests. Within the loop, once a client connects, it waits for an HTTP request,
from the client, receives and sends the HTTP request to the server (or fetch 
from cache), returns the response, and finally disconnects from both the client
and the server.

2. `createCache`

3. `getFromCache`

4. `putIntoCache`

5. `organizeCache`

6. `deleteCache`