#ifndef TERMINAL_H
#define TERMINAL_H

#include <termios.h>

typedef struct {
    int screenrows;
    int screencols;
    struct termios orig_termios;
} Terminal;

void terminal_enable_raw(Terminal *t);
void terminal_disable_raw(Terminal *t);
int terminal_get_size(int *rows, int *cols);

#endif
