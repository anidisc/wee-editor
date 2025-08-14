CC=gcc
CFLAGS=-Wall -Wextra -pedantic -std=c99

wee: wee.c
	$(CC) $(CFLAGS) -o wee wee.c

clean:
	rm -f wee
