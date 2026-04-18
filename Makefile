CC=gcc
CFLAGS=`pkg-config fuse --cflags` -Wall
LIBS=`pkg-config fuse --libs`

all:
	$(CC) main.c disk.c -o fusefs $(CFLAGS) $(LIBS)

clean:
	rm -f fusefs