CC = gcc
CFLAGS = -O2 -Wall -I .
LIB = -lpthread

all: echoclient echoserver

echoclient: echoclient.c csapp.o
	$(CC) $(CFLAGS) -o echoclient echoclient.c csapp.o $(LIB)

echoserver: echoserver.c csapp.o
	$(CC) $(CFLAGS) -o echoserver echoserver.c csapp.o $(LIB)

csapp.o: csapp.c
	$(CC) $(CFLAGS) -c csapp.c

clean:
	rm -f *.o echoclient echoserver csapp *~