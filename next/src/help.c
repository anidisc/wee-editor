#define _DEFAULT_SOURCE
#include "help.h"
#include "utils.h"
#include <stdio.h>
#include <unistd.h>
#include <string.h>

static const char *help_text[] = {
    "WEE EDITOR NEXT - HELP",
    "======================",
    "",
    "NAVIGATION",
    "----------",
    "Arrows         : Move cursor",
    "Shift + Arrows : Select text",
    "n / N          : Find next / previous (after search)",
    "ALT + Left/Right: Switch between open buffers (Tabs)",
    "CTRL + p       : Fuzzy File Finder (Quick open)",
    "CTRL + o       : Open File Browser",
    "CTRL + w       : New Empty Buffer",
    "CTRL + j       : Go to Line",
    "CTRL + l       : Select Line",
    "",
    "EDITING",
    "-------",
    "CTRL + s       : Save",
    "CTRL + a       : Save As",
    "CTRL + z / y   : Undo / Redo",
    "CTRL + c / x   : Copy / Cut",
    "CTRL + v       : Paste",
    "CTRL + k       : Delete Line",
    "ALT  + w       : Toggle Line Wrapping (Soft Wrap)",
    "CTRL + n       : Toggle Line Numbers",
    "TAB / BACKSPACE: Indent / De-indent selection (if selected)",
    "",
    "CODE BLOCK",
    "----------",
    "ALT + j       : Jump to matching brace",
    "ALT + b       : Select code block between braces",
    "ALT + f       : Fold / Unfold code block",
    "",
    "SEARCH & REPLACE",
    "----------------",
    "CTRL + f       : Find (interactive)",
    "CTRL + r       : Replace (interactive)",
    "n / N          : Next / Previous match",
    "",
    "SYSTEM",
    "------",
    "F1 / CTRL + h  : Show this help",
    "CTRL + q       : Close Buffer (Quit if last)",
    "",
    "Press any key to return to editor..."
};

void editor_show_help(Editor *E) {
    char header_with_ver[128];
    snprintf(header_with_ver, sizeof(header_with_ver), "WEE EDITOR NEXT - v%s - HELP", EDITOR_VERSION);

    int rows = sizeof(help_text) / sizeof(help_text[0]);
    int scroll_off = 0;
    int max_rows = E->terminal.screenrows - 2;

    while (1) {
        struct abuf ab = ABUF_INIT;
        abAppend(&ab, "\x1b[?25l", 6);
        abAppend(&ab, "\x1b[H", 3);

        abAppend(&ab, "  ", 2);
        abAppend(&ab, header_with_ver, (int)strlen(header_with_ver));
        abAppend(&ab, "\x1b[K\r\n", 5);

        for (int i = 1 + scroll_off; i < E->terminal.screenrows - 1 + scroll_off; i++) {
            if (i < rows) {
                int len = (int)strlen(help_text[i]);
                if (len > E->terminal.screencols) len = E->terminal.screencols;
                abAppend(&ab, "  ", 2);
                abAppend(&ab, help_text[i], len);
            }
            abAppend(&ab, "\x1b[K\r\n", 5);
        }

        write(STDOUT_FILENO, ab.b, ab.len);
        abFree(&ab);

        char seq[4] = {0};
        int n = read(STDIN_FILENO, &seq[0], 1);
        if (n <= 0) continue;

        if (seq[0] == '\x1b') {
            n = read(STDIN_FILENO, &seq[1], 1);
            if (n <= 0) {
                break;
            }
            if (seq[1] == 'O') {
                read(STDIN_FILENO, &seq[2], 1);
                break;
            }
            if (seq[1] == '[') {
                n = read(STDIN_FILENO, &seq[2], 1);
                if (n <= 0) {
                    break;
                }
                if (seq[2] == 'A') {
                    if (scroll_off > 0) scroll_off--;
                } else if (seq[2] == 'B') {
                    if (scroll_off < rows - max_rows) scroll_off++;
                } else if (seq[2] == 0 || seq[2] == 3 || seq[2] == 4) {
                    break;
                }
            }
            break;
        }
    }

    write(STDOUT_FILENO, "\x1b[?25h\x1b[2J\x1b[H", 14);
}
