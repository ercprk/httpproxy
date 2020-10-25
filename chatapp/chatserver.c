// Date   : October 21, 2020
// Author : Eric Park

#include <stdio.h>
#include <stdlib.h>
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

typedef struct
{
    void *buffer;
    ssize_t size;
    time_t last_retrieved;
} Message;

unsigned getPortNumber(int argc, char **argv);
void serveClients(int sockfd);
void handleConnectionRequest(int sockfd, fd_set *active_fd_set);
void readFromClient(int sockfd, Message **messages);

#define BACKLOG_SIZE 10
#define SERVING_SIZE 20
#define TYPE_FIELD_SIZE 2
#define SOURCE_FIELD_SIZE 20
#define DESTINATION_FIELD_SIZE 20
#define LENGTH_FIELD_SIZE 4
#define MESSAGE_ID_FIELD_SIZE 4
#define MAX_DATA_SIZE 400 // 400 bytes
#define MAX_MESSAGE_SIZE 450

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
    int serving_round = 0;
    int i;

    // Allocate memory and initialize fd_sets and message buffers
    active_fd_set = malloc(sizeof(fd_set));
    read_fd_set = malloc(sizeof(fd_set));
    messages = malloc(FD_SETSIZE * sizeof(Message *));
    for (i = 0; i < FD_SETSIZE; i++)
    {
        messages[i] = malloc(sizeof(Message));
        messages[i]->buffer = malloc(sizeof(MAX_MESSAGE_SIZE));
        messages[i]->size = 0;
    }
    FD_ZERO(read_fd_set);
    FD_ZERO(active_fd_set);
    FD_SET(sockfd, active_fd_set);

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
                    readFromClient(i, messages, active_fd_set);
            }
        }



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
    printf("HRC\n");

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
    printf("RFC\n");

    void *read_buffer;
    ssize_t read_size;

    // Making the new socket non-blocking
    if (fcntl(sockfd, F_SETFL, fcntl(sockfd, F_GETFL, 0)|O_NONBLOCK)== -1)
    {
        fprintf(stderr, "[chatserver] fcntl() failed\n");
    }

    read_buffer = malloc(MAX_MESSAGE_SIZE);

    while ((read_size = read(sockfd, read_buffer, MAX_MESSAGE_SIZE)) > 0)
    {
        if (read_size < 0)
            break;

        messages[sockfd]->size += read_size;
        messages[sockfd]->last_retrieved = time(NULL);
        memcpy(messages[sockfd]->buffer + messages[sockfd]->size, read_buffer,
               read_size);

        printf("[chatserver] Read %zd bytes from socket %d\n", read_size,
               sockfd);
    }

    free(read_buffer);
}

// Function  : handleMessages
// Arguments : int of socket file descriptor of the client socket, and an array
//             mapping of socket file handles to message buffers
// Does      : reads from the client socket, and stores the message in the 
//             message buffers
// Returns   : nothing
void
handleMessages(Message **messages, fd_set *read_fd_set)
{
    int i;

    // Service all the sockets with input pending.
    for (i = 0; i < FD_SETSIZE; ++i)
    {
        if (FD_ISSET(i, read_fd_set))
        {
            if (i == sockf) // Connection request on original socket.
                handleConnectionRequest(sockfd, active_fd_set);
            else // Data arriving on an already-connected socket.
                readFromClient(i, messages, active_fd_set);
        }
    }


}

// Function  : isMessagePartial
// Arguments : Message * of message
// Does      : tells whether the message is a partial message or not
// Returns   : bool of whether the message is partial
bool
isMessagePartial(Message *message)
{
    if (message->size == MAX_MESSAGE_SIZE - MAX_DATA_SIZE)
    {
        
    }
}
