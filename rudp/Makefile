# Makefile for Reliable UDP
#
#    Copyright 2020 - Eric Park
#
# Useful sample program targets:
#
#    rudpserver  - Reliable UDP File Transfer Server
#    rudpclient  - Reliable UDP File Transfer Client
#
#  Maintenance targets:
#
#    Make sure these clean up and build your code too
#
#    clean       - clean out all compiled object and executable files
#    all         - (default target) make sure everything's compiled
#

# Preliminary
CC = gcc
CLIBFLAGS = -lnsl
CFLAGS = -g $(CLIBFLAGS)

all: rudpserver rudpclient

#
# Build the chat app
#
rudpserver: rudpserver.c
	$(CC) $(CFLAGS) -o rudpserver rudpserver.c

rudpclient: rudpclient.c
	$(CC) $(CFLAGS) -o rudpclient rudpclient.c
#
# Delete all compiled code in preparation
# for forcing complete rebuild#
clean:
	rm -f rudpserver rudpclient