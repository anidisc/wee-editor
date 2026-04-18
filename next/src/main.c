#include "editor.h"
#include <locale.h>
#include <signal.h>
#include <stdbool.h>

volatile sig_atomic_t resize_pending = false;

void handle_sigwinch(int sig) {
    (void)sig;
    resize_pending = true;
}

int main(int argc, char **argv) {
    setlocale(LC_ALL, "");

    Editor E;
    editor_init(&E);

    if (argc >= 2) {
        editor_load(&E, argv[1]);
    }

    terminal_enable_raw(&E.terminal);
    signal(SIGWINCH, handle_sigwinch);

    while (1) {
        if (resize_pending) {
            editor_resize(&E);
            resize_pending = false;
        }
        editor_refresh_screen(&E);
        editor_process_keypress(&E);
    }

    return 0;
}
