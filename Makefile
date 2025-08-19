CC=gcc
CFLAGS=-Wall -Wextra -pedantic -std=c99

wee: wee.c cJSON.c
	$(CC) $(CFLAGS) -o $@ $^ -lm

clean:
	rm -f wee
