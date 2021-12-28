all: server.c threadpool.c
	gcc -Wall -pthread server.c threadpool.c -o server
all-GDB: server.c threadpool.c
	gcc -g -Wall -pthread server.c threadpool.c -o server
