CC=gcc
CFLAGS=-Wall -Wextra -pedantic -std=c99

# Absolute syntax directory in user config
SYNTAX_DIR ?= $(HOME)/.config/wee/syntax
CFLAGS += -DSYNTAX_DIR=\"$(SYNTAX_DIR)\"

.PHONY: all clean install-syntax

all: wee

wee: wee.c cJSON.c
	$(CC) $(CFLAGS) -o $@ $^ -lm

# Create the syntax directory in $HOME and copy bundled syntax files there
install-syntax:
	mkdir -p "$(SYNTAX_DIR)"
	cp -r syntax/* "$(SYNTAX_DIR)/" 2>/dev/null || true

clean:
	rm -f wee
