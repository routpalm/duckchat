CC=gcc
CFLAGS=-Wall -W -g -Werror

all: my_client my_server

my_client: client.o raw.o
	$(CC) client.o raw.o $(CFLAGS) -o my_client

my_server: server.o
	$(CC) server.o $(CFLAGS) -o my_server

client.o: client.c
	$(CC) $(CFLAGS) -c client.c

raw.o: raw.c
	$(CC) $(CFLAGS) -c raw.c

server.o: server.c
	$(CC) $(CFLAGS) -c server.c

clean:
	rm -f my_client my_server *.o
