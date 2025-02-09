CC=gcc
CFLAGS=-Wall -W -g -Werror

all: client server

client: client.o raw.o
	$(CC) client.o raw.o $(CFLAGS) -o client

server: server.o
	$(CC) server.o $(CFLAGS) -o server

client.o: client.c
	$(CC) $(CFLAGS) -c client.c

raw.o: raw.c
	$(CC) $(CFLAGS) -c raw.c

server.o: server.c
	$(CC) $(CFLAGS) -c server.c

clean:
	rm -f client server *.o
