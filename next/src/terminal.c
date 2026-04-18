#include "terminal.h"
#include <stdlib.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <stdio.h>

void terminal_enable_raw(Terminal *t) {
    if (tcgetattr(STDIN_FILENO, &t->orig_termios) == -1) exit(1);
    struct termios raw = t->orig_termios;
    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    raw.c_oflag &= ~(OPOST);
    raw.c_cflag |= (CS8);
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 1;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

void terminal_disable_raw(Terminal *t) {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &t->orig_termios);
}

int terminal_get_size(int *rows, int *cols) {
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
        return -1;
    }
    *cols = ws.ws_col;
    *rows = ws.ws_row;
    return 0;
}
