# Makefile for Chat Application
#
#    Copyright 2020 - Eric Park
#
# Useful sample program targets:
#
#    chatserver  - chat application
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

all: chatserver

#
# Build the chat app
#
chatserver: chatserver.c
	$(CC) $(CFLAGS) -o chatserver chatserver.c

#
# Delete all compiled code in preparation
# for forcing complete rebuild#
clean:
	rm -f chatserver