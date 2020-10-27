// Date   : October 21, 2020
// Author : Eric Park

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <fcntl.h>
#include <sys/select.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

typedef struct ClientListNode
{
    struct ClientListNode *next;
    char *name;
    int sockfd;
} ClientListNode;

typedef struct
{
    ClientListNode *head;
    size_t size;
} ClientList;

typedef struct
{
    void *buffer;
    ssize_t size;
    time_t last_retrieved;
} Message;

enum MessageType
{
    HELLO = 1,
    HELLO_ACK,
    LIST_REQUEST,
    CLIENT_LIST,
    CHAT,
    EXIT,
    CLIENT_ALREADY_PRESENT_ERROR,
    CANNOT_DELIVER_ERROR
};

unsigned getPortNumber(int argc, char **argv);
void serveClients(int sockfd);
void handleConnectionRequest(int sockfd, fd_set *active_fd_set);
void readFromClient(int sockfd, Message **messages);
void handleMessages(Message **messages, ClientList *clientList, 
                    fd_set *read_fd_set, fd_set *active_fd_set);
bool isMessagePartial(Message *message);
int dispatchMessage(unsigned short type, char *source, char *destination,
                    unsigned int length, unsigned int msg_id, void *data,
                    ClientList *clientList, int sockfd);
size_t makeClientListBuffer(void *buffer, ClientList *clientList);
void *makeMessageBuffer(unsigned short type, char *source, char *destination,
                       unsigned int length, unsigned int msg_id, void *data);
void printMessage(unsigned short type, char *source, char *destination,
                  unsigned int length, unsigned int msg_id, void *data);
int registerClient(ClientList *clientList, char *name, int sockfd);
void deregisterClient(ClientList *clientList, int sockfd);
void freeClientList(ClientList *clientList);
void printClientList(ClientList *clientList);
int clientNameToSockFd(ClientList *clientList, char *clientName);

#define BACKLOG_SIZE 10
#define SERVING_SIZE 10
#define TYPE_FIELD_SIZE 2
#define TYPE_FIELD_START_INDEX 0
#define SOURCE_FIELD_SIZE 20
#define SOURCE_FIELD_START_INDEX 2
#define DESTINATION_FIELD_SIZE 20
#define DESTINATION_FIELD_START_INDEX 22
#define LENGTH_FIELD_SIZE 4
#define LENGTH_FIELD_START_INDEX  42
#define MESSAGE_ID_FIELD_SIZE 4
#define MESSAGE_ID_FIELD_START_INDEX 46
#define DATA_START_INDEX 50
#define MAX_MESSAGE_SIZE 450
#define MAX_DATA_SIZE 400
#define HEADER_SIZE 50
#define SIG_STOP_SERVING -1
#define SIG_OK 1
#define SIG_CLIENT_ALREADY_PRESENT -1
#define SIG_CLIENT_NOT_PRESENT -2
#define SERVER_NAME "Server"

int
main(int argc, char **argv)
{
    int sockfd;
    unsigned portNum;
    struct sockaddr_in addr;

    // Handle input and get portnumber
    portNum = getPortNumber(argc, argv);

    // Create a TCP socket
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0)
    {
        fprintf(stderr, "[chatserver] Failed to create socket in main()\n");
        exit(EXIT_FAILURE);
    }

    // Bind socket to the port number
    bzero((char *) &addr, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(portNum);
    if (bind(sockfd, (struct sockaddr *)&addr, sizeof(addr)) < 0) 
    {
        fprintf(stderr, "[chatserver] Failed to bind socket to port %d\n",
                portNum);
        exit(EXIT_FAILURE);
    }

    // Listen
    if (listen(sockfd, BACKLOG_SIZE) != 0)
    {
        fprintf(stderr, "[chatserver] Failed listening on socket\n");
        fprintf(stderr, "[chatserver] errno: %d\n", errno);
        exit(EXIT_FAILURE);
    }
    printf("[chatserver] Listening...\n");

    // Serve clients
    serveClients(sockfd);

    // Close socket
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

// Function  : serveClients
// Arguments : int of socket file descriptor
// Does      : repetitively monitors a set of sockets and serves incoming client
//             requests, by using select()
// Returns   : nothing
void
serveClients(int sockfd)
{
    fd_set *active_fd_set, *read_fd_set;
    Message **messages; // Mapping from socket descriptors to message buffers
    ClientList *clientList;
    int serving_round = 0;
    int i;

    // Allocate memory and initialize
    clientList = malloc(sizeof(ClientList));
    clientList->size = 0;
    clientList->head = NULL;
    active_fd_set = malloc(sizeof(fd_set));
    read_fd_set = malloc(sizeof(fd_set));
    FD_ZERO(read_fd_set);
    FD_ZERO(active_fd_set);
    FD_SET(sockfd, active_fd_set);
    messages = malloc(FD_SETSIZE * sizeof(Message *));
    for (i = 0; i < FD_SETSIZE; i++)
    {
        messages[i] = malloc(sizeof(Message));
        messages[i]->buffer = malloc(MAX_MESSAGE_SIZE);
        messages[i]->size = 0;
    }

    while (serving_round < SERVING_SIZE)
    {
        // Block until input arrives on one or more active sockets
        *read_fd_set = *active_fd_set;
        if (select(FD_SETSIZE, read_fd_set, NULL, NULL, NULL) < 0)
        {
            fprintf(stderr, "[chatserver] select() failed\n");
            exit(EXIT_FAILURE);
        }

        printf("[chatserver] Serving round %d\n", serving_round);

        // Service all the sockets with input pending.
        for (i = 0; i < FD_SETSIZE; ++i)
        {
            if (FD_ISSET(i, read_fd_set))
            {
                if (i == sockfd) // Connection request on original socket.
                    handleConnectionRequest(sockfd, active_fd_set);
                else // Data arriving on an already-connected socket.
                    readFromClient(i, messages);
            }
        }

        handleMessages(messages, clientList, read_fd_set, active_fd_set);

        ++serving_round;
    }

    // Close any sockets that are still active
    for (i = 0; i < FD_SETSIZE; ++i)
    {
        free(messages[i]->buffer);
        free(messages[i]);

        if (FD_ISSET(i, active_fd_set) && i != sockfd)
            close(i);
    }

    freeClientList(clientList);
    free(read_fd_set);
    free(active_fd_set);
    free(messages);
}

// Function  : handleConnectionRequest
// Arguments : int of socket file descriptor of the server socket, fd_set * for
//             a set of active file descriptors
// Does      : this function is called when there is a connection request on
//             the server socket. It handles the connection request, by adding
//             the new client socket handle to the active_fd_set, if the
//             connection request is accepted successfully. If the accept() was
//             not successful, the function does nothing.
// Returns   : nothing
void
handleConnectionRequest(int sockfd, fd_set *active_fd_set)
{
    struct sockaddr_in client_addr;
    socklen_t *client_addrlen;
    int newsockfd;

    client_addrlen = malloc(sizeof(socklen_t));
    *client_addrlen = sizeof(struct sockaddr_in);

    newsockfd = accept(sockfd, (struct sockaddr *)&client_addr, client_addrlen);
    if (newsockfd >= 0)
    {
        printf("[chatserver] Connected with host %s, port %d on socket %d\n",
               inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port),
               newsockfd);
        FD_SET(newsockfd, active_fd_set);
    }

    free(client_addrlen);
}


// Function  : readFromClient
// Arguments : int of socket file descriptor of the client socket, and an array
//             mapping of socket file handles to message buffers
// Does      : reads from the client socket, and stores the message in the 
//             message buffers
// Returns   : nothing
void
readFromClient(int sockfd, Message **messages)
{
    void *read_buffer;
    ssize_t read_size;

    // Making the new socket non-blocking
    if (fcntl(sockfd, F_SETFL, fcntl(sockfd, F_GETFL, 0)|O_NONBLOCK)== -1)
    {
        fprintf(stderr, "[chatserver] fcntl() failed\n");
    }

    read_buffer = malloc(MAX_MESSAGE_SIZE);

    while ((read_size = read(sockfd, read_buffer,
                             MAX_MESSAGE_SIZE - messages[sockfd]->size)) > 0)
    {
        if (read_size < 0)
            break;

        memcpy(messages[sockfd]->buffer + messages[sockfd]->size, read_buffer,
               read_size);
        messages[sockfd]->size += read_size;
        messages[sockfd]->last_retrieved = time(NULL);

        printf("[chatserver] Read %zd bytes from socket %d\n", read_size,
               sockfd);
    }

    free(read_buffer);
}

// Function  : handleMessages
// Arguments : Message **of array mapping of socket file handles to message
//             buffers, ClientList * of client linked list, fd_set * for a set
//             of read file descriptors, and fd_set
//             * of active file descriptors
// Does      : Dispatches all messages in the Message ** buffer that are
//             complete (aka not partial)
// Returns   : nothing
void
handleMessages(Message **messages, ClientList *clientList, fd_set *read_fd_set,
               fd_set *active_fd_set)
{
    int i;
    unsigned short type;
    char *source, *destination, *data;
    Message *message;
    unsigned int length, msg_id;

    source = malloc(SOURCE_FIELD_SIZE);
    destination = malloc(DESTINATION_FIELD_SIZE);
    data = malloc(MAX_DATA_SIZE);

    // Service all the sockets with input
    for (i = 0; i < FD_SETSIZE; ++i)
    {
        if (FD_ISSET(i, read_fd_set))
        {
            message = messages[i];

            if (!isMessagePartial(message))
            {
                // Parse the buffer
                memcpy(&type, message->buffer + TYPE_FIELD_START_INDEX,
                       TYPE_FIELD_SIZE);
                memcpy(source, message->buffer + SOURCE_FIELD_START_INDEX,
                       SOURCE_FIELD_SIZE);
                memcpy(destination,
                       message->buffer + DESTINATION_FIELD_START_INDEX,
                       DESTINATION_FIELD_SIZE);
                memcpy(&length, message->buffer + LENGTH_FIELD_START_INDEX,
                       LENGTH_FIELD_SIZE);
                memcpy(&msg_id, message->buffer + MESSAGE_ID_FIELD_START_INDEX,
                       MESSAGE_ID_FIELD_SIZE);
                type = ntohs(type); // From network to host byte order
                length = ntohl(length); // Likewise
                msg_id = ntohl(msg_id);
                if (length > 0)
                    memcpy(data, message->buffer + DATA_START_INDEX, length);
                    
                // Dispatch the message
                printf("[chatserver] Dispatching message from socket %d\n", i);
                if (dispatchMessage(type, source, destination, length, msg_id,
                                    data, clientList, i) == SIG_STOP_SERVING)
                {
                    close(i);
                    FD_CLR(i, active_fd_set);
                    deregisterClient(clientList, i);
                    printf("[chatserver] Closed connection with socket ");
                    printf("%d\n", i);
                }
                bzero(messages[i]->buffer, messages[i]->size);
                messages[i]->size = 0;
            }
        }
    }

    printClientList(clientList);

    free(source);
    free(destination);
    free(data);
}

// Function  : isMessagePartial
// Arguments : Message * of message
// Does      : tells whether the message is a partial message or not
// Returns   : bool of whether the message is partial
bool
isMessagePartial(Message *message)
{
    unsigned int length;

    // Checks if this is a message without data
    if (message->size == HEADER_SIZE)
    {
        memcpy(&length, message->buffer + LENGTH_FIELD_START_INDEX,
               LENGTH_FIELD_SIZE);
        length = ntohl(length); // From network to host byte order
        if (length == 0) // This is a message without data
            return false;
    }
    // When there's some data
    else if (message->size > HEADER_SIZE && message->size <= MAX_MESSAGE_SIZE)
    {
        // Check if it has the right amount of data
        memcpy(&length, message->buffer + LENGTH_FIELD_START_INDEX,
               LENGTH_FIELD_SIZE);
        length = ntohl(length); // From network endian to host endian

        // It has the right amount of data
        if (length == message->size - HEADER_SIZE)
            return false;
    }

    // Everything else is considered a partial message
    return true;
}

// Function  : dispatchMessage
// Arguments : unsigned short of type, char * of source, char * of destination,
//             unsigned int of length, unsigned int of message id, void * of
//             data, ClientList * of client linked list, and int of client
//             socket file descriptor
// Does      : dispatches the message according to its field
// Returns   : SIG_STOP_SERVING on end of communication and SIG_OK otherwise
int
dispatchMessage(unsigned short type, char *source, char *destination,
                unsigned int length, unsigned int msg_id, void *data,
                ClientList *clientList, int sockfd)
{
    void *reply, *clientListBuffer;
    int dest_sockfd;
    int return_val;
    size_t clientListBufferSize;

    printMessage(type, source, destination, length, msg_id, data);
    return_val = SIG_OK;

    switch (type)
    {
        case HELLO:
            if (registerClient(clientList, source, sockfd) ==
                SIG_CLIENT_ALREADY_PRESENT)
            {
                // Send CLIENT_ALREADY_PRESENT_ERROR
                reply = makeMessageBuffer(CLIENT_ALREADY_PRESENT_ERROR,
                                          destination, source, 0, 0, NULL);
                write(sockfd, reply, HEADER_SIZE);
                printf("[chatserver] Sent CLIENT_ALREADY_PRESENT_ERROR ");
                printf(" to socket %d\n", sockfd);
                free(reply);
                return_val = SIG_STOP_SERVING;
            }
            else
            {
                // Send HELLOACK
                reply = makeMessageBuffer(HELLO_ACK, destination, source, 0, 0,
                                          NULL);
                if (write(sockfd, reply, HEADER_SIZE) < 0)
                    return_val = SIG_STOP_SERVING;
                free(reply);
                
                // Send CLIENT_LIST
                clientListBuffer = malloc(MAX_DATA_SIZE);
                clientListBufferSize = makeClientListBuffer(clientListBuffer,
                                                            clientList);
                clientListBuffer = realloc(clientListBuffer,
                                           clientListBufferSize);
                reply = makeMessageBuffer(CLIENT_LIST, destination, source,
                                          clientListBufferSize, 0,
                                          clientListBuffer);
                if (write(sockfd, reply, HEADER_SIZE+clientListBufferSize) < 0)
                    return_val = SIG_STOP_SERVING;
                free(reply);
                free(clientListBuffer);
            }
            break;
        case LIST_REQUEST:
            // Send CLIENT_LIST
            clientListBuffer = malloc(MAX_DATA_SIZE);
            clientListBufferSize = makeClientListBuffer(clientListBuffer,
                                                        clientList);
            clientListBuffer = realloc(clientListBuffer, clientListBufferSize);
            reply = makeMessageBuffer(CLIENT_LIST, destination, source,
                                      clientListBufferSize, 0,
                                      clientListBuffer);
            if (write(sockfd, reply, HEADER_SIZE+clientListBufferSize) < 0)
                return_val = SIG_STOP_SERVING;
            free(reply);
            free(clientListBuffer);
            break;
        case CHAT:
            // Relay CHAT or send CANNOT_DELIVER_ERROR
            dest_sockfd = clientNameToSockFd(clientList, destination);
            if (dest_sockfd == SIG_CLIENT_NOT_PRESENT)
            {
                reply = makeMessageBuffer(CANNOT_DELIVER_ERROR, SERVER_NAME,
                                          source, 0, msg_id, NULL);
                if (write(sockfd, reply, HEADER_SIZE) < 0)
                    return_val = SIG_STOP_SERVING;
                free(reply);
            }
            else
            {
                reply = makeMessageBuffer(CHAT, source, destination, length,
                                          msg_id, data);
                if (write(sockfd, reply, HEADER_SIZE + length) < 0)
                    return_val = SIG_STOP_SERVING;
                free(reply);
            }
            break;
        case EXIT:
            return_val = SIG_STOP_SERVING;
            break;
        default:
            printf("[chatserver] Invalid message type: type %u\n", type);
            return_val = SIG_STOP_SERVING;
            break;
    }

    return return_val;
}

// Function  : makeClientListBuffer
// Arguments : char * of buffer and ClientList * of client linked list
// Does      : makes the data part of the CLIENT_LIST message
// Returns   : size_t of the total size of null-terminated strings of client
//             IDs
size_t
makeClientListBuffer(void *buffer, ClientList *clientList)
{
    size_t buffer_size, clientname_size;
    ClientListNode *curr;

    buffer_size = 0;

    curr = clientList->head;
    while (curr)
    {
        clientname_size = strlen(curr->name) + 1; // For null terminator
        memcpy(buffer + buffer_size, curr->name, clientname_size);
        buffer_size += clientname_size;
        curr = curr->next;
    }

    return buffer_size;
}

// Function  : makeMessageBuffer
// Arguments : unsigned short of type, char * of source, char * of destination,
//             unsigned int of length, unsigned int of message id, and void * of
//             data
// Does      : makes a message that is ready to be sent to the client. Memory
//             is allocated for the message and needs to be freed after usage.
// Returns   : void * of message
void *
makeMessageBuffer(unsigned short type, char *source, char *destination,
                  unsigned int length, unsigned int msg_id, void *data)
{
    void *msg_buffer;

    msg_buffer = malloc(length + HEADER_SIZE);

    if (length > 0)
        memcpy(msg_buffer + DATA_START_INDEX, data, length);
    type = htons(type); // From host to network byte order
    length = htonl(length); // Likewise
    msg_id = htonl(msg_id);
    memcpy(msg_buffer + TYPE_FIELD_START_INDEX, &type, TYPE_FIELD_SIZE);
    memcpy(msg_buffer + SOURCE_FIELD_START_INDEX, source, SOURCE_FIELD_SIZE);
    memcpy(msg_buffer + DESTINATION_FIELD_START_INDEX, destination,
           DESTINATION_FIELD_SIZE);
    memcpy(msg_buffer + LENGTH_FIELD_START_INDEX, &length, LENGTH_FIELD_SIZE);
    memcpy(msg_buffer + MESSAGE_ID_FIELD_START_INDEX, &msg_id,
           MESSAGE_ID_FIELD_SIZE);

    return msg_buffer;
}

// Function  : printMessage
// Arguments : unsigned short of type, char * of source, char * of destination,
//             unsigned int of length, unsigned int of message id, and void * of
//             data
// Does      : prints out the message in a human readable format
// Returns   : nothing
void
printMessage(unsigned short type, char *source, char *destination,
             unsigned int length, unsigned int msg_id, void *data)
{
    printf("[chatserver]         Message type: ");
    switch (type)
    {
        case HELLO: printf("HELLO\n"); break;
        case HELLO_ACK: printf("HELLO_ACK\n"); break;
        case LIST_REQUEST: printf("LIST_REQUEST\n"); break;
        case CLIENT_LIST: printf("CLIENT_LIST\n"); break;
        case CHAT: printf("CHAT\n"); break;
        case EXIT: printf("EXIT\n"); break;
        case CLIENT_ALREADY_PRESENT_ERROR: break;
            printf("CLIENT_ALREADY_PRESENT_ERROR\n"); break;
        case CANNOT_DELIVER_ERROR: printf("CANNOT_DELIVER_ERROR\n"); break;
        default: printf("THIS_SHOULDNT_HAPPEN\n"); break;
    }
    printf("[chatserver]         Source: %s\n", source);
    printf("[chatserver]         Destination: %s\n", destination);
    printf("[chatserver]         Length: %u\n", length);
    printf("[chatserver]         Message ID: %u\n", msg_id);
    if (length > 0)
        printf("[chatserver]         Data: (start after newline)\n%s\n", data);
}

// Function  : registerClient
// Arguments : ClientList * of client linked list, char * of client name, and
//             an int of client socket file descriptor
// Does      : registers the client information to the client linked list
// Returns   : SIG_CLIENT_ALREADY_PRESENT on error when the client already
//             exists, SIG_OK otherwise
int
registerClient(ClientList *clientList, char *name, int sockfd)
{
    ClientListNode* curr;
    
    if (clientList->head == NULL)
    {
        clientList->head = malloc(sizeof(ClientListNode));
        clientList->head->next = NULL;
        clientList->head->name = strdup(name);
        clientList->head->sockfd = sockfd;

    }
    else
    {
        curr = clientList->head;
        while (curr)
        {
            if (strcmp(curr->name, name) == 0) // If the client already exists
            {
                printf("[chatserver] Client %s already exists\n", name);
                return SIG_CLIENT_ALREADY_PRESENT;
            }

            if (curr->next)
                curr = curr->next;
            else
                break;
        }

        curr->next = malloc(sizeof(ClientListNode));
        curr->next->next = NULL;
        curr->next->name = strdup(name);
        curr->next->sockfd = sockfd;
    }

    ++clientList->size;

    printf("[chatserver] Registered client %s with socket %d\n", name, sockfd);

    return SIG_OK;
}

// Function  : deregisterClient
// Arguments : ClientList * of client linked list and an int of client socket
//             file descriptor
// Does      : deregisters the client information from the client linked list
// Returns   : nothing
void
deregisterClient(ClientList *clientList, int sockfd)
{
    ClientListNode *curr, *prev;
    char *name;

    curr = clientList->head;
    while (curr)
    {
        if (curr->sockfd == sockfd)
        {
            if (curr == clientList->head)
            {
                clientList->head = curr->next;
                prev = curr;
                curr = curr->next;
                name = strdup(prev->name);
                free(prev->name);
                free(prev);
            }
            else
            {
                prev->next = curr->next;
                name = strdup(curr->name);
                free(curr->name);
                free(name);
                curr = prev->next;
            }

            printf("[chatserver] Deregistered client %s with socket %d\n",
                   name, sockfd);
            free(name);
        }
        else
        {
            prev = curr;
            curr = curr->next;
        }
    }

    --clientList->size;
}

// Function  : freeClientList
// Arguments : ClientList * of client linked list
// Does      : frees up allocated memory for the client linked list
// Returns   : nothing
void
freeClientList(ClientList *clientList)
{
    ClientListNode *curr, *prev;

    curr = clientList->head;
    while (curr)
    {
        prev = curr;
        curr = curr->next;
        free(prev->name);
        free(prev);
    }

    free(clientList);
}

// Function  : printClientList
// Arguments : ClientList * of client linked list
// Does      : prints the client names and socket numbers for clients
// Returns   : nothing
void
printClientList(ClientList *clientList)
{
    ClientListNode *curr;

    printf("[chatserver] Client list:\n");

    if (clientList->size == 0)
        printf("[chatserver]         empty\n");

    curr = clientList->head;
    while (curr)
    {
        printf("[chatserver]         %s (socket %d)\n", curr->name,
               curr->sockfd);
        curr = curr->next;
    }
}

// Function  : printClientList
// Arguments : ClientList * of client linked list
// Does      : prints the client names and socket numbers for clients
// Returns   : int of sockfd, SIG_CLIENT_NOT_PRESENT when no such client is
//             found
int
clientNameToSockFd(ClientList *clientList, char *clientName)
{
    ClientListNode *curr;

    curr = clientList->head;
    while (curr)
    {
        if (strcmp(curr->name, clientName) == 0)
            return curr->sockfd;
        curr = curr->next;
    }

    return SIG_CLIENT_NOT_PRESENT;
}
