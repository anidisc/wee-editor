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

    struct abuf ab = ABUF_INIT;
    abAppend(&ab, "\x1b[?25l", 6);
    abAppend(&ab, "\x1b[H", 3);

    abAppend(&ab, "  ", 2);
    abAppend(&ab, header_with_ver, (int)strlen(header_with_ver));
    abAppend(&ab, "\x1b[K\r\n", 5);

    int rows = sizeof(help_text) / sizeof(help_text[0]);
    for (int i = 1; i < E->terminal.screenrows + 1; i++) {
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

    char c;
    while (read(STDIN_FILENO, &c, 1) == 0);
}
