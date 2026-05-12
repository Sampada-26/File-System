CC=gcc
CFLAGS=`pkg-config fuse3 --cflags` -Wall
LIBS=`pkg-config fuse3 --libs`

all:
	$(CC) main.c disk.c -o fusefs $(CFLAGS) $(LIBS)

clean:
	rm -f fusefs
