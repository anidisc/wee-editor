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
    "CTRL + o       : Open File Browser",
    "CTRL + w       : New File",

    "",
    "EDITING",
    "-------",
    "CTRL + s       : Save",
    "CTRL + a       : Save As",
    "CTRL + z / y   : Undo / Redo",
    "CTRL + c       : Copy selected text",
    "CTRL + x       : Cut selected text",
    "CTRL + v       : Paste from clipboard",
    "CTRL + n       : Toggle Line Numbers",
    "TAB            : Insert 4 spaces",
    "BACKSPACE      : Delete left",
    "DEL            : Delete right",
    "",
    "SEARCH & REPLACE",
    "----------------",
    "CTRL + f       : Find (Enter to search, Arrows to navigate, ESC to exit)",
    "CTRL + r       : Replace (Case Sensitive, interactive)",
    "",
    "SYSTEM",
    "------",
    "F1 / CTRL + h  : Show this help",
    "CTRL + q       : Quit (with confirmation if modified)",
    "",
    "Press any key to return to editor..."
};

void editor_show_help(Editor *E) {
    char header_with_ver[128];
    snprintf(header_with_ver, sizeof(header_with_ver), "WEE EDITOR NEXT - v%s - HELP", EDITOR_VERSION);

    struct abuf ab = ABUF_INIT;
    abAppend(&ab, "\x1b[?25l", 6);
    abAppend(&ab, "\x1b[H", 3);

    abAppend(&ab, "  ", 2);
    abAppend(&ab, header_with_ver, strlen(header_with_ver));
    abAppend(&ab, "\x1b[K\r\n", 5);

    int rows = sizeof(help_text) / sizeof(help_text[0]);
    for (int i = 1; i < E->terminal.screenrows; i++) {
        if (i < rows) {
            int len = strlen(help_text[i]);
            if (len > E->terminal.screencols) len = E->terminal.screencols;
            abAppend(&ab, "  ", 2);
            abAppend(&ab, help_text[i], len);
        }
        abAppend(&ab, "\x1b[K\r\n", 5);
    }

    write(STDOUT_FILENO, ab.b, ab.len);
    abFree(&ab);

    // Wait for any key
    char c;
    while (read(STDIN_FILENO, &c, 1) == 0);
}
