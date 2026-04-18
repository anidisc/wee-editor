#define _DEFAULT_SOURCE
#include "file_browser.h"
#include "utils.h"
#include <dirent.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/stat.h>

typedef struct {
    char **names;
    int *is_dir;
    int count;
    int selected;
    int scroll_offset;
    char current_path[1024];
} BrowserState;

static void browser_load_dir(BrowserState *bs) {
    // Clear old
    for (int i = 0; i < bs->count; i++) free(bs->names[i]);
    free(bs->names);
    free(bs->is_dir);
    bs->names = NULL; bs->is_dir = NULL; bs->count = 0; bs->selected = 0;

    DIR *d = opendir(bs->current_path);
    if (!d) return;

    struct dirent *dir;
    while ((dir = readdir(d)) != NULL) {
        if (strcmp(dir->d_name, ".") == 0) continue;
        
        char full_path[2048];
        snprintf(full_path, sizeof(full_path), "%s/%s", bs->current_path, dir->d_name);
        
        struct stat st;
        stat(full_path, &st);
        
        bs->names = realloc(bs->names, sizeof(char *) * (bs->count + 1));
        bs->is_dir = realloc(bs->is_dir, sizeof(int) * (bs->count + 1));
        
        bs->names[bs->count] = strdup(dir->d_name);
        bs->is_dir[bs->count] = S_ISDIR(st.st_mode);
        bs->count++;
    }
    closedir(d);
}

static void browser_draw(Editor *E, BrowserState *bs) {
    struct abuf ab = ABUF_INIT;
    abAppend(&ab, "\x1b[?25l", 6);
    abAppend(&ab, "\x1b[H", 3);

    // Header
    char header[128];
    int hlen = snprintf(header, sizeof(header), "\x1b[7m PATH: %s \x1b[m", bs->current_path);
    abAppend(&ab, header, hlen);
    abAppend(&ab, "\x1b[K\r\n", 5);

    for (int y = 0; y < E->terminal.screenrows - 1; y++) {
        int idx = y + bs->scroll_offset;
        if (idx < bs->count) {
            if (idx == bs->selected) abAppend(&ab, "\x1b[7m > ", 7);
            else abAppend(&ab, "   ", 3);
            
            char line[256];
            int len = snprintf(line, sizeof(line), "%-6s %s", 
                               bs->is_dir[idx] ? "[DIR]" : "", 
                               bs->names[idx]);
            if (len > E->terminal.screencols - 4) len = E->terminal.screencols - 4;
            abAppend(&ab, line, len);
            if (idx == bs->selected) abAppend(&ab, "\x1b[m", 3);
        }
        abAppend(&ab, "\x1b[K\r\n", 5);
    }

    abAppend(&ab, "\x1b[7m [Enter] Open/Enter  [Esc] Cancel \x1b[m", 45);
    abAppend(&ab, "\x1b[K", 3);

    write(STDOUT_FILENO, ab.b, ab.len);
    abFree(&ab);
}

char* file_browser_open(Editor *E) {
    BrowserState bs = {NULL, NULL, 0, 0, 0, "."};
    getcwd(bs.current_path, sizeof(bs.current_path));
    browser_load_dir(&bs);

    while (1) {
        browser_draw(E, &bs);
        
        char c;
        if (read(STDIN_FILENO, &c, 1) <= 0) continue;

        if (c == '\r') {
            char target[2048];
            snprintf(target, sizeof(target), "%s/%s", bs.current_path, bs.names[bs.selected]);
            
            if (bs.is_dir[bs.selected]) {
                realpath(target, bs.current_path);
                browser_load_dir(&bs);
                bs.scroll_offset = 0;
            } else {
                char *res = strdup(target);
                for (int i = 0; i < bs.count; i++) free(bs.names[i]);
                free(bs.names); free(bs.is_dir);
                return res;
            }
        } else if (c == '\x1b') {
            char seq[3];
            if (read(STDIN_FILENO, &seq[0], 1) == 1 && seq[0] == '[') {
                if (read(STDIN_FILENO, &seq[1], 1) == 1) {
                    if (seq[1] == 'A' && bs.selected > 0) bs.selected--;
                    if (seq[1] == 'B' && bs.selected < bs.count - 1) bs.selected++;
                }
            } else {
                break; // Escape
            }
        }
        
        if (bs.selected < bs.scroll_offset) bs.scroll_offset = bs.selected;
        if (bs.selected >= bs.scroll_offset + E->terminal.screenrows - 1)
            bs.scroll_offset = bs.selected - E->terminal.screenrows + 2;
    }

    for (int i = 0; i < bs.count; i++) free(bs.names[i]);
    free(bs.names); free(bs.is_dir);
    return NULL;
}
