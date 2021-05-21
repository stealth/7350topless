CC=cc
CFLAGS=-Wall -O2 -std=c11 -pedantic

all: topless

topless: 7350topless.c
	$(CC) $(CFLAGS) 7350topless.c -lpthread -o 7350topless

