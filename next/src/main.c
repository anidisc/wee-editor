#include "editor.h"
#include <locale.h>
#include <signal.h>
#include <stdbool.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

volatile sig_atomic_t resize_pending = false;

void handle_sigwinch(int sig) {
    (void)sig;
    resize_pending = true;
}

int main(int argc, char **argv) {
    setlocale(LC_ALL, "");

    EditorManager EM;
    em_init(&EM);

    if (argc >= 2) {
        for (int i = 1; i < argc; i++) {
            em_add_buffer(&EM, argv[i]);
        }
    } else {
        em_add_buffer(&EM, NULL);
    }

    Editor *active = em_get_active(&EM);
    if (!active) exit(1);
    
    // Salva lo stato del terminale all'inizio usando una struttura pulita
    Terminal main_term;
    memset(&main_term, 0, sizeof(Terminal));
    terminal_enable_raw(&main_term);
    signal(SIGWINCH, handle_sigwinch);

    while (1) {
        if (resize_pending) {
            editor_resize(&EM);
            resize_pending = false;
        }
        editor_refresh_screen(&EM);
        editor_process_keypress(&EM);
        
        if (EM.count == 0) break;
    }

    // --- CLEANUP ON EXIT ---
    terminal_disable_raw(&main_term); 
    write(STDOUT_FILENO, "\x1b[2J\x1b[H", 7);
    
    return 0;
}
