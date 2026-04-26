#define _GNU_SOURCE
#define _DEFAULT_SOURCE
#include "editor.h"
#include "utils.h"
#include "highlight.h"
#include "file_browser.h"
#include "help.h"
#include "utf8.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <ctype.h>
#include <string.h>
#include <fcntl.h>
#include <libgen.h>
#include <dirent.h>
#include <sys/stat.h>

#define ctrl_key(k) ((k) & 0x1f)
#define SWAP_THRESHOLD 20
#define MAX_FUZZY_FILES 1024

/* --- FUZZY FINDER LOGIC --- */

typedef struct {
    char *path;
    int score;
} FuzzyMatch;

static void scan_files_recursive(const char *path, char **file_list, int *count, int max) {
    if (*count >= max) return;
    DIR *d = opendir(path);
    if (!d) return;
    struct dirent *entry;
    while ((entry = readdir(d)) != NULL) {
        if (entry->d_name[0] == '.') {
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;
            if (strcmp(entry->d_name, ".git") == 0) continue;
        }
        char full_path[1024];
        snprintf(full_path, sizeof(full_path), "%s/%s", path, entry->d_name);
        struct stat st;
        if (stat(full_path, &st) == 0) {
            if (S_ISDIR(st.st_mode)) {
                if (strcmp(entry->d_name, "obj") != 0 && strcmp(entry->d_name, "node_modules") != 0 && strcmp(entry->d_name, ".git") != 0) {
                    scan_files_recursive(full_path, file_list, count, max);
                }
            } else if (S_ISREG(st.st_mode)) {
                const char *p = (strncmp(full_path, "./", 2) == 0) ? full_path + 2 : full_path;
                file_list[*count] = strdup(p);
                (*count)++;
                if (*count >= max) break;
            }
        }
    }
    closedir(d);
}

static int fuzzy_score(const char *pattern, const char *text) {
    if (!pattern || !*pattern) return 0;
    int score = 0;
    const char *p = pattern;
    const char *t = text;
    const char *first_match = NULL;
    
    while (*p && *t) {
        if (tolower(*p) == tolower(*t)) {
            if (!first_match) first_match = t;
            score += 10;
            if (*p == *t) score += 5;
            p++;
        }
        t++;
    }
    if (*p) return -1;
    if (first_match) {
        score -= (int)(t - first_match);
        const char *filename = strrchr(text, '/');
        filename = filename ? filename + 1 : text;
        if (tolower(pattern[0]) == tolower(filename[0])) score += 50;
    }
    return score;
}

void editor_fuzzy_finder(EditorManager *em) {
    Editor *E = em_get_active(em);
    if (!E) return;

    char *file_list[MAX_FUZZY_FILES];
    int file_count = 0;
    scan_files_recursive(".", file_list, &file_count, MAX_FUZZY_FILES);

    char query[128] = "";
    int qlen = 0;
    int selection = 0;
    FuzzyMatch matches[MAX_FUZZY_FILES];
    int match_count = 0;

    while (1) {
        match_count = 0;
        for (int i = 0; i < file_count; i++) {
            int score = fuzzy_score(query, file_list[i]);
            if (score >= 0) {
                matches[match_count].path = file_list[i];
                matches[match_count].score = score;
                match_count++;
            }
        }
        for (int i = 0; i < match_count - 1; i++) {
            for (int j = 0; j < match_count - i - 1; j++) {
                if (matches[j].score < matches[j+1].score) {
                    FuzzyMatch tmp = matches[j];
                    matches[j] = matches[j+1];
                    matches[j+1] = tmp;
                }
            }
        }

        if (selection >= match_count) selection = match_count > 0 ? match_count - 1 : 0;
        if (selection < 0) selection = 0;

        struct abuf ab = ABUF_INIT;
        abAppend(&ab, "\x1b[?25l", 6);
        
        char header[256];
        int hlen = snprintf(header, sizeof(header), "\x1b[2;1H\x1b[1;33m FIND FILE \x1b[m> ");
        abAppend(&ab, header, hlen);
        abAppend(&ab, query, qlen);
        abAppend(&ab, "\x1b[K", 3);

        int rows_to_show = 10;
        if (rows_to_show > E->terminal.screenrows - 2) rows_to_show = E->terminal.screenrows - 2;
        
        for (int i = 0; i < rows_to_show; i++) {
            char move[32]; snprintf(move, sizeof(move), "\x1b[%d;1H", i + 3);
            abAppend(&ab, move, (int)strlen(move));
            if (i < match_count) {
                if (i == selection) abAppend(&ab, "\x1b[7m > ", 7);
                else abAppend(&ab, "   ", 3);
                int path_len = (int)strlen(matches[i].path);
                if (path_len > E->terminal.screencols - 5) path_len = E->terminal.screencols - 5;
                abAppend(&ab, matches[i].path, path_len);
                abAppend(&ab, "\x1b[m\x1b[K", 6);
            } else {
                abAppend(&ab, "\x1b[K", 3);
            }
        }
        
        char move_cursor[32];
        snprintf(move_cursor, sizeof(move_cursor), "\x1b[2;%dH", 14 + qlen);
        abAppend(&ab, move_cursor, (int)strlen(move_cursor));
        abAppend(&ab, "\x1b[?25h", 6);

        write(STDOUT_FILENO, ab.b, ab.len); abFree(&ab);

        char c;
        if (read(STDIN_FILENO, &c, 1) <= 0) continue;
        if (c == '\r') {
            if (match_count > 0) {
                em_add_buffer(em, matches[selection].path);
                break;
            }
        } else if (c == '\x1b') {
            char seq[3];
            if (read(STDIN_FILENO, &seq[0], 1) == 1 && seq[0] == '[') {
                if (read(STDIN_FILENO, &seq[1], 1) == 1) {
                    if (seq[1] == 'A') selection--;
                    if (seq[1] == 'B') selection++;
                }
            } else break;
        } else if (c == 127) {
            if (qlen > 0) query[--qlen] = '\0';
        } else if (!iscntrl(c) && qlen < (int)sizeof(query) - 1) {
            query[qlen++] = c;
            query[qlen] = '\0';
        }
    }

    for (int i = 0; i < file_count; i++) free(file_list[i]);
    editor_refresh_screen(em);
}

/* --- SWAP FILE HELPERS --- */

static char *editor_get_swap_filename(const char *filename) {
    if (!filename) return strdup(".wee_untitled.swp");
    char *dup = strdup(filename);
    char *base = basename(dup);
    char *dir = dirname(dup);
    char *swap = malloc(strlen(dir) + strlen(base) + 10);
    sprintf(swap, "%s/.%s.swp", dir, base);
    free(dup);
    return swap;
}

void editor_update_swap(Editor *E) {
    if (!E->dirty || E->dirty_count < SWAP_THRESHOLD) return;
    char *swap_name = editor_get_swap_filename(E->filename);
    if (pt_save(E->pt, swap_name)) {
        E->dirty_count = 0;
    }
    free(swap_name);
}

static void editor_remove_swap(Editor *E) {
    char *swap_name = editor_get_swap_filename(E->filename);
    unlink(swap_name);
    free(swap_name);
}

/* --- EDITOR MANAGER IMPLEMENTATION --- */

void em_init(EditorManager *em) {
    em->count = 0;
    em->active_idx = 0;
    for (int i = 0; i < MAX_BUFFERS; i++) em->buffers[i] = NULL;
}

void em_add_buffer(EditorManager *em, const char *filename) {
    if (em->count >= MAX_BUFFERS) return;
    Editor *E = malloc(sizeof(Editor));
    editor_init(E);
    if (filename) {
        editor_load(E, filename);
    }
    em->buffers[em->count] = E;
    em->active_idx = em->count;
    em->count++;
}

void em_close_current(EditorManager *em) {
    if (em->count == 0) return;
    Editor *E = em->buffers[em->active_idx];
    if (E->dirty) {
        if (!editor_confirm(E, "Unsaved changes! Close anyway? (y/n)")) return;
    }
    editor_remove_swap(E);
    if (E->filename) free(E->filename);
    if (E->pt) pt_destroy(E->pt);
    if (E->li) li_destroy(E->li);
    if (E->undo_stack) undo_destroy(E->undo_stack);
    free(E);
    for (int i = em->active_idx; i < em->count - 1; i++) {
        em->buffers[i] = em->buffers[i+1];
    }
    em->count--;
    if (em->active_idx >= em->count && em->count > 0) em->active_idx = em->count - 1;
}

void em_next(EditorManager *em) {
    if (em->count <= 1) return;
    em->active_idx = (em->active_idx + 1) % em->count;
}

void em_prev(EditorManager *em) {
    if (em->count <= 1) return;
    em->active_idx = (em->active_idx + em->count - 1) % em->count;
}

Editor *em_get_active(EditorManager *em) {
    if (em->count == 0) return NULL;
    return em->buffers[em->active_idx];
}

/* --- HELPER FUNCTIONS (Logic & Selection) --- */

static size_t get_line_len(Editor *E, int y) {
    int line_count = li_get_line_count(E->li);
    if (y < 0 || y >= line_count) return 0;
    size_t start = li_get_offset(E->li, y);
    size_t end = (y + 1 < line_count) ? li_get_offset(E->li, y + 1) : E->pt->total_length;
    size_t len = end - start;
    char *txt = pt_get_text(E->pt, start, len);
    if (len > 0 && txt) {
        if (txt[len-1] == '\n' || txt[len-1] == '\r') {
            if (len > 1 && (txt[len-2] == '\n' || txt[len-2] == '\r')) len -= 2;
            else len--;
        }
    }
    if (txt) free(txt);
    return len;
}

static int get_leading_spaces(Editor *E, int y) {
    size_t off = li_get_offset(E->li, y);
    size_t line_len = get_line_len(E, y);
    if (line_len == 0) return -1;
    char *line = pt_get_text(E->pt, off, line_len);
    int count = 0;
    while (count < (int)line_len && line[count] == ' ') count++;
    free(line);
    return count;
}

static bool is_full_line_selection(Editor *E) {
    if (!E->selecting) return false;
    int start_y = E->sel_cy, end_y = E->cy;
    int s_cx = E->sel_cx, e_cx = E->cx;
    if (start_y > end_y) { int t = start_y; start_y = end_y; end_y = t; t = s_cx; s_cx = e_cx; e_cx = t; }
    size_t last_line_len = get_line_len(E, end_y);
    return (s_cx == 0 && e_cx >= (int)last_line_len);
}

static int get_first_non_space(Editor *E, int y) {
    size_t line_start = li_get_offset(E->li, y);
    size_t line_len = (y + 1 < li_get_line_count(E->li)) ? 
        li_get_offset(E->li, y + 1) - line_start : 
        E->pt->total_length - line_start;
    if (line_len == 0) return 0;
    char *line = pt_get_text(E->pt, line_start, line_len);
    int skip = 0;
    while (skip < (int)line_len && (line[skip] == ' ' || line[skip] == '\t')) skip++;
    free(line);
    return skip;
}

static bool editor_handle_mouse(Editor *E) {
    unsigned char b1, b2, b3;
    if (read(STDIN_FILENO, &b1, 1) != 1) return false;
    if (read(STDIN_FILENO, &b2, 1) != 1) return false;
    if (read(STDIN_FILENO, &b3, 1) != 1) return false;
    
    int btn = b1 - 32;
    int x = b2 - 32;
    int y = b3 - 32;
    x--; y--;
    
    int line_count = li_get_line_count(E->li);
    int ln_width = E->show_line_numbers ? snprintf(NULL, 0, "%d", line_count) + 1 : 0;
    y -= 1;
    x -= ln_width + 1;
    
    if (y < 0 || y >= E->terminal.screenrows || x < 0) return true;
    
    int target_y = y + E->rowoff;
    int target_x = x + E->coloff;
    if (target_y >= line_count) target_y = line_count - 1;
    if (target_y < 0) target_y = 0;
    
    if (btn == 0) {
        E->selecting = true; E->sel_cy = target_y; E->sel_cx = 0;
    } else if (btn == 35) {
        E->selecting = false;
    }
    
    E->cy = target_y;
    ViewLine *vl = NULL;
    for (int i = 0; i < E->terminal.screenrows; i++) {
        if (E->vp->lines[i].chars && E->vp->lines[i].logical_row == target_y) {
            vl = &E->vp->lines[i];
            break;
        }
    }
    if (vl) {
        E->cx = target_x + vl->byte_offset;
        if (E->cx > vl->byte_offset + vl->len) E->cx = vl->byte_offset + vl->len;
        if (E->cx < vl->byte_offset) E->cx = vl->byte_offset;
    } else {
        E->cx = target_x;
    }
    return true;
}

static int is_folded(Editor *E, int line) {
    for (int i = 0; i < E->fold_count; i++) {
        if (line > E->folds[i].start && line <= E->folds[i].end) return i;
    }
    return -1;
}

static int logical_to_visual_line(Editor *E, int logical_line) {
    int visual = 0;
    for (int i = 0; i < logical_line; i++) {
        if (is_folded(E, i) < 0) visual++;
    }
    return visual;
}

static int visual_to_logical_line(Editor *E, int visual_line) {
    int line_count = li_get_line_count(E->li);
    int current = 0;
    for (int i = 0; i < line_count && current < visual_line; i++) {
        int fold_idx = is_folded(E, i);
        if (fold_idx < 0) current++;
    }
    for (int i = 0; i < line_count; i++) {
        int fold_idx = is_folded(E, i);
        if (fold_idx < 0 && current == visual_line) return i;
        if (fold_idx < 0) current++;
    }
    return line_count - 1;
}

static int is_foldable_line(Editor *E, int line) {
    if (line < 0) return 0;
    size_t off = li_get_offset(E->li, line);
    if (off >= E->pt->total_length) return 0;
    size_t len = (line + 1 < li_get_line_count(E->li)) ? li_get_offset(E->li, line + 1) - off : E->pt->total_length - off;
    char *l = pt_get_text(E->pt, off, len > 256 ? 256 : len);
    if (!l) return 0;
    for (int i = 0; l[i]; i++) {
        if (l[i] == '{') { free(l); return 1; }
    }
    free(l);
    return 0;
}

static bool is_char_selected(Editor *E, int row, int col) {
    if (!E->selecting) return false;
    size_t offset = li_get_offset(E->li, row) + col;
    size_t start = li_get_offset(E->li, E->sel_cy) + E->sel_cx;
    size_t end = li_get_offset(E->li, E->cy) + E->cx;
    if (start > end) { size_t tmp = start; start = end; end = tmp; }
    if (E->sel_cx == 0) {
        if (row == E->sel_cy) {
            int first_ns = get_first_non_space(E, E->sel_cy);
            if (first_ns > 0 && offset < (size_t)first_ns) return false;
        } else if (row > E->sel_cy && row <= E->cy) {
            int first_ns = get_first_non_space(E, row);
            size_t line_start = li_get_offset(E->li, row);
            if (first_ns > 0 && offset < line_start + first_ns) return false;
        }
    }
    return offset >= start && offset < end;
}

static void editor_move_to_offset(Editor *E, int64_t offset) {
    int line_count = li_get_line_count(E->li);
    if (line_count == 0) { E->cx = E->cy = 0; return; }
    for (int i = 0; i < line_count; i++) {
        size_t start = li_get_offset(E->li, i);
        size_t end = (i + 1 < line_count) ? li_get_offset(E->li, i + 1) : E->pt->total_length;
        if (offset >= (int64_t)start && offset < (int64_t)end) {
            E->cy = i; E->cx = (int)(offset - start); return;
        }
    }
    E->cy = line_count - 1;
    size_t last_start = li_get_offset(E->li, E->cy);
    E->cx = (int)(E->pt->total_length - last_start);
}

static void editor_sync_model(Editor *E) {
    char *text = pt_get_text(E->pt, 0, E->pt->total_length);
    li_rebuild(E->li, text, E->pt->total_length);
    free(text);
}

static bool is_line_blank(Editor *E, int y) {
    size_t off = li_get_offset(E->li, y);
    size_t next_off = (y + 1 < li_get_line_count(E->li)) ? li_get_offset(E->li, y+1) : E->pt->total_length;
    int len = (int)(next_off - off);
    if (len <= 0) return true;
    char *text = pt_get_text(E->pt, off, len);
    bool blank = true;
    for (int i = 0; i < len; i++) {
        if (text[i] != '\n' && text[i] != '\r' && !isspace((unsigned char)text[i])) { blank = false; break; }
    }
    free(text); return blank;
}

/* --- EDITOR CORE --- */

void editor_init(Editor *E) {
    E->cx = 0; E->cy = 0; E->rx = 0;
    E->rowoff = 0; E->coloff = 0;
    E->filename = NULL;
    E->last_search = NULL;
    E->last_match_off = -1;
    E->search_match_len = 0;
    E->search_dir = 1;
    E->sel_cx = 0; E->sel_cy = 0;
    E->selecting = false;
    E->clipboard = NULL;
    E->show_line_numbers = true;
    E->wrap_enabled = false;
    E->tab_size = 4;
    E->dirty = false;
    E->dirty_count = 0;
    E->fold_count = 0;
    E->line_ending = END_LF;
    E->encoding = ENC_UTF8;
    E->syntax = NULL;
    E->pt = pt_create("", 0);
    E->li = li_create();
    E->undo_stack = undo_create();
    if (terminal_get_size(&E->terminal.screenrows, &E->terminal.screencols) == -1) exit(1);
    E->terminal.screenrows -= 2;
    E->vp = vp_create(E->terminal.screenrows);
}

bool editor_confirm(Editor *E, char *prompt) {
    while (1) {
        struct abuf ab = ABUF_INIT; char status[256];
        int len = snprintf(status, sizeof(status), "\x1b[7m%s\x1b[m", prompt);
        abAppend(&ab, "\x1b[?25l", 6);
        char move_buf[32]; snprintf(move_buf, sizeof(move_buf), "\x1b[%d;1H", E->terminal.screenrows + 2);
        abAppend(&ab, move_buf, (int)strlen(move_buf)); abAppend(&ab, status, len);
        abAppend(&ab, "\x1b[K", 3);
        write(STDOUT_FILENO, ab.b, ab.len); abFree(&ab);
        char c = '\0'; if (read(STDIN_FILENO, &c, 1) <= 0) continue;
        if (c == 'y' || c == 'Y') return true;
        if (c == 'n' || c == 'N' || c == '\x1b') return false;
    }
}

void editor_new_file(Editor *E) {
    if (E->dirty) {
        if (!editor_confirm(E, "Unsaved changes! Discard and create new file? (y/n)")) return;
    }
    editor_remove_swap(E);
    if (E->filename) { free(E->filename); E->filename = NULL; }
    if (E->pt) pt_destroy(E->pt);
    if (E->li) li_destroy(E->li);
    if (E->undo_stack) undo_destroy(E->undo_stack);
    E->pt = pt_create("", 0);
    E->li = li_create();
    E->undo_stack = undo_create();
    E->cx = E->cy = 0; E->rowoff = E->coloff = 0;
    E->dirty = false; E->dirty_count = 0;
    E->syntax = NULL;
    editor_sync_model(E);
}

void editor_load(Editor *E, const char *filename) {
    char *swap_name = editor_get_swap_filename(filename);
    bool recovered = false;
    if (access(swap_name, F_OK) == 0) {
        if (editor_confirm(E, "Swap file detected! Recover unsaved changes? (y/n)")) {
            if (E->filename) free(E->filename);
            E->filename = strdup(filename);
            E->syntax = hl_get_syntax(filename);
            if (E->pt) pt_destroy(E->pt);
            E->pt = pt_open(swap_name);
            editor_sync_model(E);
            E->dirty = true; E->dirty_count = 0;
            recovered = true;
        } else {
            unlink(swap_name);
        }
    }
    free(swap_name);

    if (!recovered) {
        if (E->filename) free(E->filename);
        E->filename = strdup(filename);
        E->syntax = hl_get_syntax(filename);
        if (E->pt) pt_destroy(E->pt);
        E->pt = pt_open(filename);
        editor_sync_model(E);
        E->dirty = false; E->dirty_count = 0;
    }

    // Detect Encoding and Line Endings
    char *text = pt_get_text(E->pt, 0, E->pt->total_length);
    if (text) {
        if (!utf8_is_valid(text, E->pt->total_length)) E->encoding = END_LATIN1;
        else E->encoding = ENC_UTF8;

        int crlf_count = 0;
        for (size_t i = 0; i < E->pt->total_length; i++) {
            if (text[i] == '\r' && i + 1 < E->pt->total_length && text[i+1] == '\n') crlf_count++;
        }
        if (crlf_count > 0) E->line_ending = END_CRLF;
        else E->line_ending = END_LF;
        free(text);
    }
}

char *editor_prompt(Editor *E, char *prompt, void (*callback)(Editor *, char *, int)) {
    size_t bufsize = 128; char *buf = malloc(bufsize); size_t buflen = 0; buf[0] = '\0';
    while (1) {
        struct abuf ab = ABUF_INIT; char status[256];
        int len = snprintf(status, sizeof(status), "\x1b[7m%s: %s\x1b[m", prompt, buf);
        abAppend(&ab, "\x1b[?25l", 6);
        char move_buf[32]; snprintf(move_buf, sizeof(move_buf), "\x1b[%d;1H", E->terminal.screenrows + 2);
        abAppend(&ab, move_buf, (int)strlen(move_buf)); abAppend(&ab, status, len);
        abAppend(&ab, "\x1b[K", 3); abAppend(&ab, "\x1b[?25h", 6);
        write(STDOUT_FILENO, ab.b, ab.len); abFree(&ab);
        char c = '\0'; int nread = read(STDIN_FILENO, &c, 1);
        if (nread == -1) { free(buf); return NULL; }
        if (nread == 0) continue;
        if (c == '\r') { if (buflen != 0) return buf; }
        else if (c == '\x1b') { free(buf); return NULL; }
        else if (c == 127) { if (buflen != 0) buf[--buflen] = '\0'; }
        else if (!iscntrl(c)) {
            if (buflen == bufsize - 1) { bufsize *= 2; buf = realloc(buf, bufsize); }
            buf[buflen++] = c; buf[buflen] = '\0'; if (callback) callback(E, buf, c);
        }
    }
}

void editor_save_as(Editor *E) {
    char *new_name = editor_prompt(E, "Save as", NULL);
    if (new_name) {
        if (access(new_name, F_OK) == 0) {
            char msg[256]; snprintf(msg, sizeof(msg), "File '%s' already exists. Overwrite? (y/n)", new_name);
            if (!editor_confirm(E, msg)) { free(new_name); return; }
        }
        editor_remove_swap(E);
        if (E->filename) free(E->filename);
        E->filename = strdup(new_name);
        E->syntax = hl_get_syntax(new_name);
        if (pt_save_ext(E->pt, E->filename, E->line_ending == END_CRLF)) { E->dirty = false; E->dirty_count = 0; }
        free(new_name);
    }
}

void editor_save(Editor *E) {
    if (E->filename == NULL) { editor_save_as(E); return; }
    if (pt_save_ext(E->pt, E->filename, E->line_ending == END_CRLF)) {
        E->dirty = false; E->dirty_count = 0;
        editor_remove_swap(E);
    }
}

static int editor_find_match_index(Editor *E, const char *query, int64_t current_match_off) {
    if (!query || current_match_off == -1) return 0;
    int index = 1; size_t offset = 0; const size_t window_size = 8192; size_t qlen = strlen(query);
    while (offset < (size_t)current_match_off) {
        size_t to_read = ((size_t)current_match_off - offset > window_size) ? window_size : ((size_t)current_match_off - offset);
        if (offset + to_read < (size_t)current_match_off) to_read += qlen; 
        char *chunk = pt_get_text(E->pt, offset, to_read);
        if (!chunk) break;
        char *match = strcasestr(chunk, query);
        while (match) {
            size_t match_off = offset + (match - chunk);
            if (match_off < (size_t)current_match_off) { index++; match = strcasestr(match + 1, query); } else break;
        }
        free(chunk); offset += (to_read > qlen) ? (to_read - qlen + 1) : 1;
    }
    return index;
}

void editor_find_next(Editor *E, const char *query, int dir) {
    if (!query) return;
    size_t current_off = li_get_offset(E->li, E->cy) + E->cx;
    size_t start_at = (dir == 1) ? current_off + 1 : current_off - 1;
    if (current_off == 0 && dir == -1) start_at = E->pt->total_length;
    int64_t found = pt_find(E->pt, query, start_at, dir, true);
    if (found == -1) found = pt_find(E->pt, query, (dir == 1) ? 0 : E->pt->total_length, dir, true);
    if (found != -1) { E->last_match_off = (int)found; E->search_match_len = (int)strlen(query); editor_move_to_offset(E, found); }
}

void editor_find(EditorManager *em) {
    Editor *E = em_get_active(em);
    char *query = editor_prompt(E, "Search (Case Insensitive)", NULL);
    if (!query) { E->last_match_off = -1; E->search_match_len = 0; return; }
    int total_matches = pt_count_occurrences(E->pt, query);
    int64_t found = pt_find(E->pt, query, li_get_offset(E->li, E->cy) + E->cx, 1, true);
    if (found == -1) found = pt_find(E->pt, query, 0, 1, true);
    if (found != -1) { E->last_match_off = (int)found; E->search_match_len = (int)strlen(query); editor_move_to_offset(E, found); }
    while (1) {
        editor_refresh_screen(em);
        int current_idx = (E->last_match_off != -1) ? editor_find_match_index(E, query, E->last_match_off) : 0;
        char msg[256]; snprintf(msg, sizeof(msg), "\x1b[7m FIND: %s [%d/%d] (Arrows: next/prev, ESC: exit) \x1b[m", query, current_idx, total_matches);
        char move_buf[32]; snprintf(move_buf, sizeof(move_buf), "\x1b[%d;1H", E->terminal.screenrows + 2);
        write(STDOUT_FILENO, move_buf, strlen(move_buf));
        write(STDOUT_FILENO, msg, strlen(msg)); write(STDOUT_FILENO, "\x1b[K", 3);
        int ln_width = E->show_line_numbers ? snprintf(NULL, 0, "%d ", li_get_line_count(E->li)) + 1 : 0;
        snprintf(move_buf, sizeof(move_buf), "\x1b[%d;%dH", (E->cy - E->rowoff) + 2, (E->rx - E->coloff) + 1 + ln_width);
        write(STDOUT_FILENO, move_buf, strlen(move_buf));
        char c = '\0'; if (read(STDIN_FILENO, &c, 1) <= 0) continue;
        if (c == '\x1b') {
            char seq[3]; if (read(STDIN_FILENO, &seq[0], 1) == 1 && seq[0] == '[') {
                if (read(STDIN_FILENO, &seq[1], 1) == 1) {
                    if (seq[1] == 'A' || seq[1] == 'D') editor_find_next(E, query, -1);
                    if (seq[1] == 'B' || seq[1] == 'C') editor_find_next(E, query, 1);
                }
            } else break;
        } else if (c == '\r') break;
    }
    if (E->last_search) free(E->last_search);
    E->last_search = query; E->last_match_off = -1; E->search_match_len = 0;
}

void editor_replace(EditorManager *em) {
    Editor *E = em_get_active(em);
    char *query = editor_prompt(E, "Find (Case Sensitive)", NULL);
    if (!query) return;
    char *replacement = editor_prompt(E, "Replace with", NULL);
    if (!replacement) { free(query); return; }
    int64_t found = pt_find(E->pt, query, 0, 1, false);
    while (found != -1) {
        E->last_match_off = (int)found; E->search_match_len = (int)strlen(query);
        editor_move_to_offset(E, found);
        editor_refresh_screen(em);
        char msg[256]; snprintf(msg, sizeof(msg), "\x1b[7m Replace '%s' with '%s'? (y/n/a/ESC) \x1b[m", query, replacement);
        char move_buf[32]; snprintf(move_buf, sizeof(move_buf), "\x1b[%d;1H", E->terminal.screenrows + 2);
        write(STDOUT_FILENO, move_buf, strlen(move_buf));
        write(STDOUT_FILENO, msg, strlen(msg)); write(STDOUT_FILENO, "\x1b[K", 3);
        int ln_width = E->show_line_numbers ? snprintf(NULL, 0, "%d ", li_get_line_count(E->li)) + 1 : 0;
        snprintf(move_buf, sizeof(move_buf), "\x1b[%d;%dH", (E->cy - E->rowoff) + 2, (E->rx - E->coloff) + 1 + ln_width);
        write(STDOUT_FILENO, move_buf, strlen(move_buf));
        char c = '\0'; if (read(STDIN_FILENO, &c, 1) <= 0) continue;
        if (c == '\x1b') break;
        if (c == 'y' || c == 'Y' || c == 'a' || c == 'A') {
            size_t qlen = strlen(query); size_t rlen = strlen(replacement);
            char *deleted = pt_get_text(E->pt, (size_t)found, qlen);
            undo_push(E->undo_stack, ACTION_DELETE, (size_t)found, deleted, qlen);
            free(deleted);
            pt_delete_fixed(E->pt, (size_t)found, qlen);
            pt_insert(E->pt, (size_t)found, replacement, rlen);
            undo_push(E->undo_stack, ACTION_INSERT, (size_t)found, replacement, rlen);
            editor_sync_model(E); E->dirty = true; E->dirty_count++; editor_update_swap(E);
            if (c == 'y' || c == 'Y') found = pt_find(E->pt, query, (size_t)found + rlen, 1, false);
            else { found = pt_find(E->pt, query, (size_t)found + rlen, 1, false); continue; }
        } else if (c == 'n' || c == 'N') found = pt_find(E->pt, query, (size_t)found + 1, 1, false);
        else break;
    }
    E->last_match_off = -1; E->search_match_len = 0; free(query); free(replacement);
}

void editor_goto_line(Editor *E) {
    int total_lines = li_get_line_count(E->li);
    int visible_lines = 0;
    for (int i = 0; i < total_lines; i++) {
        if (is_folded(E, i) < 0) visible_lines++;
    }
    char prompt[128]; snprintf(prompt, sizeof(prompt), "Go to line (1-%d)", visible_lines);
    char *input = editor_prompt(E, prompt, NULL);
    if (input) {
        int line = atoi(input);
        if (line < 1) line = 1;
        int target_logical = visual_to_logical_line(E, line - 1);
        E->cy = target_logical;
        E->cx = 0;
        free(input);
    }
}

void editor_open_browser(Editor *E) {
    if (E->dirty) { if (!editor_confirm(E, "Unsaved changes! Open another file anyway? (y/n)")) return; }
    char *selected = file_browser_open(E);
    if (selected) { editor_load(E, selected); E->cx = E->cy = 0; E->rowoff = E->coloff = 0; free(selected); }
}

static int count_visible_lines(Editor *E, int start_line, int end_line) {
    int count = 0;
    for (int i = start_line; i <= end_line && i < li_get_line_count(E->li); i++) {
        int is_hidden = 0;
        for (int f = 0; f < E->fold_count; f++) {
            if (i > E->folds[f].start && i <= E->folds[f].end) { is_hidden = 1; break; }
        }
        if (!is_hidden) count++;
    }
    return count;
}

void editor_scroll(Editor *E) {
    int visible = count_visible_lines(E, E->rowoff, E->cy);
    if (visible <= 0) visible = 1;
    if (E->cy < E->rowoff) E->rowoff = E->cy;
    else if (visible > E->terminal.screenrows) E->rowoff = E->cy - E->terminal.screenrows + 1;
    if (E->rx < E->coloff) E->coloff = E->rx;
    if (E->rx >= E->coloff + E->terminal.screencols - (E->show_line_numbers ? 6 : 0)) 
        E->coloff = E->rx - (E->terminal.screencols - (E->show_line_numbers ? 6 : 0)) + 1;
}

void editor_resize(EditorManager *em) {
    int rows, cols; if (terminal_get_size(&rows, &cols) == -1) return;
    for (int i = 0; i < em->count; i++) {
        Editor *E = em->buffers[i];
        E->terminal.screenrows = rows - 2; E->terminal.screencols = cols;
        vp_destroy(E->vp); E->vp = vp_create(E->terminal.screenrows);
    }
}

void editor_refresh_screen(EditorManager *em) {
    Editor *E = em_get_active(em); if (!E) return;

    int total_lines = li_get_line_count(E->li);
    int ln_width = E->show_line_numbers ? snprintf(NULL, 0, "%d", total_lines) + 1 : 0;
    int fold_indic = 0;
    if (E->fold_count > 0) {
        for (int i = 0; i < total_lines; i++) {
            if (is_foldable_line(E, i) || is_folded(E, i) >= 0) { fold_indic = 2; break; }
        }
    }
    int wrap_width = E->wrap_enabled ? (E->terminal.screencols - ln_width - fold_indic - 1) : -1;

    editor_scroll(E); 
    vp_sync(E->vp, E->pt, E->li, E->rowoff, wrap_width, E->folds, E->fold_count);
    
    for (int i = 0; i < E->terminal.screenrows; i++) {
        ViewLine *vl = &E->vp->lines[i]; 
        if (vl->chars) hl_apply(vl->chars, vl->len, vl->hl, E->syntax);
    }

    int v_row = -1;
    for (int i = 0; i < E->terminal.screenrows; i++) {
        if (E->vp->lines[i].chars && E->vp->lines[i].logical_row == E->cy) {
            int start_byte = E->vp->lines[i].byte_offset;
            int end_byte = start_byte + E->vp->lines[i].len;
            if (E->cx >= start_byte && (E->cx < end_byte || (E->cx == end_byte && (i + 1 == E->terminal.screenrows || E->vp->lines[i+1].logical_row != E->cy)))) {
                v_row = i;
                E->rx = E->vp->lines[i].cx_to_rx[E->cx - start_byte];
                break;
            }
        }
    }
    if (v_row == -1) E->rx = 0;

    struct abuf ab = ABUF_INIT;
    abAppend(&ab, "\x1b[?25l", 6); abAppend(&ab, "\x1b[H", 3);
    
    abAppend(&ab, "\x1b[1;1H\x1b[7m", 10);
    for (int i = 0; i < em->count; i++) {
        Editor *buf = em->buffers[i];
        const char *name = buf->filename ? strrchr(buf->filename, '/') : NULL;
        name = name ? name + 1 : (buf->filename ? buf->filename : "[No Name]");
        if (i == em->active_idx) abAppend(&ab, "\x1b[1;37m", 7); else abAppend(&ab, "\x1b[22;90m", 8);
        char tab[128]; int n = snprintf(tab, sizeof(tab), " %s%s ", name, buf->dirty ? "*" : ""); abAppend(&ab, tab, n);
    }
    abAppend(&ab, "\x1b[m\x1b[K", 6);

    for (int i = 0; i < E->terminal.screenrows; i++) {
        char move_to_row[32]; snprintf(move_to_row, sizeof(move_to_row), "\x1b[%d;1H", i + 2);
        abAppend(&ab, move_to_row, (int)strlen(move_to_row));
        
        ViewLine *vl = &E->vp->lines[i];
        abAppend(&ab, "\x1b[m", 3);
        int row = vl->logical_row;
        int fold_idx = is_folded(E, row);
        if (fold_idx >= 0) continue;
        if (E->show_line_numbers) {
            char ln_buf[32];
            if (vl->chars && !vl->is_wrapped) {
                const char *color = (vl->logical_row == E->cy) ? "\x1b[37m" : "\x1b[90m";
                int is_fold_start = 0;
                for (int f = 0; f < E->fold_count; f++) {
                    if (E->folds[f].start == vl->logical_row) { is_fold_start = 1; break; }
                }
                int foldable = is_foldable_line(E, vl->logical_row);
                if (is_fold_start) {
                    int n = snprintf(ln_buf, sizeof(ln_buf), "%s%*d+\x1b[m", color, ln_width, vl->logical_row + 1);
                    abAppend(&ab, ln_buf, n);
                } else if (foldable) {
                    int n = snprintf(ln_buf, sizeof(ln_buf), "%s%*d-\x1b[m", color, ln_width, vl->logical_row + 1);
                    abAppend(&ab, ln_buf, n);
                } else {
                    int n = snprintf(ln_buf, sizeof(ln_buf), "%s%*d \x1b[m", color, ln_width, vl->logical_row + 1);
                    abAppend(&ab, ln_buf, n);
                }
            } else if (vl->chars) {
                int n = snprintf(ln_buf, sizeof(ln_buf), "\x1b[90m%*s\x1b[m", ln_width, " ");
                abAppend(&ab, ln_buf, n);
            } else {
                int n = snprintf(ln_buf, sizeof(ln_buf), "\x1b[90m%*s\x1b[m", ln_width, "~");
                abAppend(&ab, ln_buf, n);
            }
        }

        if (vl->chars && vl->visual_len > E->coloff) {
            int drawlen = vl->visual_len - E->coloff; int effective_cols = E->terminal.screencols - ln_width - fold_indic - 1;
            if (drawlen > effective_cols) drawlen = effective_cols;
            int start_idx = 0; while (start_idx < vl->len && vl->cx_to_rx[start_idx] < E->coloff) start_idx++;
            int draw_row = vl->logical_row;
            for (int f = 0; f < E->fold_count; f++) {
                if (draw_row == E->folds[f].start && E->folds[f].end > draw_row) {
                    int fold_lines = E->folds[f].end - E->folds[f].start;
                    char fold_indicator[64];
                    int n = snprintf(fold_indicator, sizeof(fold_indicator), "\x1b[90m... %d lines", fold_lines);
                    abAppend(&ab, fold_indicator, n);
                    break;
                }
            }
            int current_color = -1;
            for (int j = start_idx; j < vl->len && vl->cx_to_rx[j] < E->coloff + drawlen; j++) {
                int color = vl->hl[j];
                if (is_char_selected(E, draw_row, vl->byte_offset + j)) color = HL_SELECT;
                if (color != current_color) { const char *ansi = hl_to_ansi(color); abAppend(&ab, ansi, (int)strlen(ansi)); current_color = color; }
                abAppend(&ab, &vl->chars[j], 1);
            }
            abAppend(&ab, "\x1b[m", 3);
        }
        abAppend(&ab, "\x1b[K", 3);
    }
    
    char move_to_status[32]; snprintf(move_to_status, sizeof(move_to_status), "\x1b[%d;1H", E->terminal.screenrows + 2);
    abAppend(&ab, move_to_status, (int)strlen(move_to_status));
    abAppend(&ab, "\x1b[7m", 4);
    char status[128], rstatus[64];
    const char *display_name = E->filename ? strrchr(E->filename, '/') : NULL;
    display_name = display_name ? display_name + 1 : (E->filename ? E->filename : "[No Name]");
    const char *ftype = E->syntax ? E->syntax->filetype : "no ft";
    const char *le = (E->line_ending == END_CRLF) ? "CRLF" : "LF";
    const char *enc = (E->encoding == ENC_UTF8) ? "UTF-8" : "LATIN1";
    int len = snprintf(status, sizeof(status), " %s - %d lines (%s) %s %s [%s %s]", display_name, total_lines, ftype, E->dirty ? "(modified)" : "", E->wrap_enabled ? "[W]" : "", enc, le);
    int rstatus_len = snprintf(rstatus, sizeof(rstatus), "LN: %s %d:%d ", E->show_line_numbers ? "ON" : "OFF", E->cy + 1, E->cx + 1);
    if (len > E->terminal.screencols - 1) len = E->terminal.screencols - 1;
    abAppend(&ab, status, len);
    while (len < E->terminal.screencols - 1) { if (E->terminal.screencols - 1 - len == rstatus_len) { abAppend(&ab, rstatus, rstatus_len); break; } else { abAppend(&ab, " ", 1); len++; } }
    abAppend(&ab, "\x1b[m", 3);
    
    char buf[32];
    int cursor_y = (v_row != -1) ? v_row + 2 : (E->cy - E->rowoff + 2);
    int cursor_x = (E->rx - E->coloff) + 1 + ln_width + (E->show_line_numbers ? 1 : 0);
    int n = snprintf(buf, sizeof(buf), "\x1b[%d;%dH", cursor_y, cursor_x);
    abAppend(&ab, buf, n); abAppend(&ab, "\x1b[?25h", 6);
    write(STDOUT_FILENO, ab.b, ab.len); abFree(&ab);
}

static int find_next_visible_line(Editor *E, int from_line) {
    int line_count = li_get_line_count(E->li);
    for (int i = from_line + 1; i < line_count; i++) {
        int is_hidden = 0;
        for (int f = 0; f < E->fold_count; f++) {
            if (i > E->folds[f].start && i <= E->folds[f].end) { is_hidden = 1; break; }
        }
        if (!is_hidden) return i;
    }
    return from_line;
}

static int find_prev_visible_line(Editor *E, int from_line) {
    for (int i = from_line - 1; i >= 0; i--) {
        int is_hidden = 0;
        for (int f = 0; f < E->fold_count; f++) {
            if (i > E->folds[f].start && i <= E->folds[f].end) { is_hidden = 1; break; }
        }
        if (!is_hidden) return i;
    }
    return from_line;
}

void editor_move_cursor(Editor *E, int key) {
    int line_count = li_get_line_count(E->li);
    int v_idx = -1;
    for (int i = 0; i < E->terminal.screenrows; i++) {
        if (E->vp->lines[i].chars && E->vp->lines[i].logical_row == E->cy) {
            int start = E->vp->lines[i].byte_offset;
            int end = start + E->vp->lines[i].len;
            if (E->cx >= start && E->cx <= end) { v_idx = i; break; }
        }
    }

    switch (key) {
        case 'A': // Up
            if (E->wrap_enabled && v_idx > 0 && E->vp->lines[v_idx-1].logical_row == E->cy) {
                int rel_off = E->cx - E->vp->lines[v_idx].byte_offset;
                int prev_len = E->vp->lines[v_idx-1].len;
                E->cx = E->vp->lines[v_idx-1].byte_offset + (rel_off < prev_len ? rel_off : prev_len);
            } else if (E->cy > 0) {
                int prev = find_prev_visible_line(E, E->cy);
                if (prev != E->cy) E->cy = prev;
                else if (E->cy > 0) E->cy--;
                int len = (int)get_line_len(E, E->cy);
                if (E->cx > len) E->cx = len;
            }
            break;
        case 'B': // Down
            if (E->wrap_enabled && v_idx != -1 && v_idx < E->terminal.screenrows - 1 && E->vp->lines[v_idx+1].logical_row == E->cy && E->vp->lines[v_idx+1].chars) {
                int rel_off = E->cx - E->vp->lines[v_idx].byte_offset;
                int next_len = E->vp->lines[v_idx+1].len;
                E->cx = E->vp->lines[v_idx+1].byte_offset + (rel_off < next_len ? rel_off : next_len);
            } else if (E->cy < line_count - 1) {
                int next = find_next_visible_line(E, E->cy);
                if (next != E->cy) E->cy = next;
                else if (E->cy < line_count - 1) E->cy++;
                int len = (int)get_line_len(E, E->cy);
                if (E->cx > len) E->cx = len;
            }
            break;
        case 'D': // Left
            if (E->cx > 0) E->cx--;
            else if (E->cy > 0) { E->cy--; E->cx = (int)get_line_len(E, E->cy); }
            break;
        case 'C': // Right
            if (E->cx < (int)get_line_len(E, E->cy)) E->cx++;
            else if (E->cy < line_count - 1) { E->cy++; E->cx = 0; }
            break;
    }
}

void editor_insert_char(Editor *E, int c) {
    size_t offset = li_get_offset(E->li, E->cy) + E->cx;
    char ch = (char)c; char paired = 0;
    if (ch == '(') paired = ')'; else if (ch == '[') paired = ']'; else if (ch == '{') paired = '}'; else if (ch == '"') paired = '"'; else if (ch == '\'') paired = '\'';
    if (paired) {
        char buf[2] = {ch, paired}; pt_insert(E->pt, offset, buf, 2); undo_push(E->undo_stack, ACTION_INSERT, offset, buf, 2);
        editor_sync_model(E); E->cx++; E->dirty = true; E->dirty_count++; editor_update_swap(E); return;
    }
    if (ch == ')' || ch == ']' || ch == '}' || ch == '"' || ch == '\'') {
        size_t next_off = li_get_offset(E->li, E->cy) + E->cx;
        if (next_off < E->pt->total_length) {
            char *next_c = pt_get_text(E->pt, next_off, 1);
            if (next_c && next_c[0] == ch) { free(next_c); E->cx++; return; }
            if (next_c) free(next_c);
        }
    }
    pt_insert(E->pt, offset, &ch, 1); undo_push(E->undo_stack, ACTION_INSERT, offset, &ch, 1);
    editor_sync_model(E); E->cx++; E->dirty = true; E->dirty_count++; editor_update_swap(E);
}

void editor_insert_newline(Editor *E) {
    size_t offset = li_get_offset(E->li, E->cy) + E->cx;
    size_t line_start = li_get_offset(E->li, E->cy); size_t line_len = E->cx;
    char *line_text = pt_get_text(E->pt, line_start, line_len); int indent_len = 0;
    while (indent_len < (int)line_len && (line_text[indent_len] == ' ' || line_text[indent_len] == '\t')) indent_len++;
    char *nl_with_indent = malloc(indent_len + 2); nl_with_indent[0] = '\n'; if (indent_len > 0) memcpy(nl_with_indent + 1, line_text, indent_len);
    nl_with_indent[indent_len + 1] = '\0'; pt_insert(E->pt, offset, nl_with_indent, indent_len + 1);
    undo_push(E->undo_stack, ACTION_INSERT, offset, nl_with_indent, indent_len + 1);
    free(line_text); free(nl_with_indent); editor_sync_model(E); E->cy++; E->cx = indent_len; E->dirty = true; E->dirty_count++; editor_update_swap(E);
}

void editor_delete_char(Editor *E) {
    if (E->cx == 0 && E->cy == 0) return;
    size_t offset = li_get_offset(E->li, E->cy) + E->cx;
    if (E->cx > 0) {
        size_t line_start = li_get_offset(E->li, E->cy); char *before = pt_get_text(E->pt, line_start, E->cx);
        bool all_spaces = true; for (int i = 0; i < E->cx; i++) if (before[i] != ' ') { all_spaces = false; break; }
        if (all_spaces && (E->cx > 0 && E->cx % E->tab_size == 0)) {
            int to_del = E->tab_size; char *deleted_text = pt_get_text(E->pt, offset - to_del, to_del);
            undo_push(E->undo_stack, ACTION_DELETE, offset - to_del, deleted_text, to_del);
            free(deleted_text); pt_delete_fixed(E->pt, offset - to_del, to_del);
            E->cx -= to_del; free(before); editor_sync_model(E); E->dirty = true; E->dirty_count++; editor_update_swap(E); return;
        }
        free(before);
    }
    char *deleted_text = pt_get_text(E->pt, offset - 1, 1); undo_push(E->undo_stack, ACTION_DELETE, offset - 1, deleted_text, 1);
    free(deleted_text); pt_delete_fixed(E->pt, offset - 1, 1);
    if (E->cx > 0) E->cx--;
    else { E->cy--; editor_sync_model(E); E->cx = (int)get_line_len(E, E->cy); }
    editor_sync_model(E); E->dirty = true; E->dirty_count++; editor_update_swap(E);
}

void editor_del_char(Editor *E) {
    int line_count = li_get_line_count(E->li);
    if (E->cy == line_count - 1 && E->cx == (int)get_line_len(E, E->cy)) return;
    size_t offset = li_get_offset(E->li, E->cy) + E->cx;
    char *deleted_text = pt_get_text(E->pt, offset, 1); undo_push(E->undo_stack, ACTION_DELETE, offset, deleted_text, 1);
    free(deleted_text); pt_delete_fixed(E->pt, offset, 1); editor_sync_model(E); E->dirty = true; E->dirty_count++; editor_update_swap(E);
}

void editor_delete_line(Editor *E) {
    int line_count = li_get_line_count(E->li); if (line_count == 0) return;
    size_t start = li_get_offset(E->li, E->cy);
    size_t end = (E->cy + 1 < line_count) ? li_get_offset(E->li, E->cy + 1) : E->pt->total_length;
    char *text = pt_get_text(E->pt, start, end - start); undo_push(E->undo_stack, ACTION_DELETE, start, text, end - start);
    free(text); pt_delete_fixed(E->pt, start, end - start);
    if (E->cy >= li_get_line_count(E->li) && E->cy > 0) E->cy--;
    E->cx = 0; editor_sync_model(E); E->dirty = true; E->dirty_count++; editor_update_swap(E);
}

void editor_insert_tab(Editor *E) { for (int i = 0; i < E->tab_size; i++) editor_insert_char(E, ' '); }

void editor_copy(Editor *E) {
    size_t start, end;
    if (E->selecting) {
        start = li_get_offset(E->li, E->sel_cy) + E->sel_cx; end = li_get_offset(E->li, E->cy) + E->cx;
        if (start > end) { size_t tmp = start; start = end; end = tmp; }
    } else {
        start = li_get_offset(E->li, E->cy); end = (E->cy + 1 < li_get_line_count(E->li)) ? li_get_offset(E->li, E->cy + 1) : E->pt->total_length;
    }
    if (start >= end) return;
    if (E->clipboard) free(E->clipboard); E->clipboard = pt_get_text(E->pt, start, end - start);
}

void editor_cut(Editor *E) {
    size_t start, end; bool was_selecting = E->selecting;
    if (E->selecting) {
        start = li_get_offset(E->li, E->sel_cy) + E->sel_cx; end = li_get_offset(E->li, E->cy) + E->cx;
        if (start > end) { size_t tmp = start; start = end; end = tmp; }
    } else {
        start = li_get_offset(E->li, E->cy); end = (E->cy + 1 < li_get_line_count(E->li)) ? li_get_offset(E->li, E->cy + 1) : E->pt->total_length;
    }
    if (start >= end) return;
    if (E->clipboard) free(E->clipboard); E->clipboard = pt_get_text(E->pt, start, end - start);
    undo_push(E->undo_stack, ACTION_DELETE, start, E->clipboard, end - start);
    pt_delete_fixed(E->pt, start, end - start);
    if (was_selecting) editor_move_to_offset(E, (int64_t)start); else E->cx = 0;
    editor_sync_model(E); E->selecting = false; E->dirty = true; E->dirty_count++; editor_update_swap(E);
}

void editor_delete_selection(Editor *E) {
    if (!E->selecting) return;
    size_t start = li_get_offset(E->li, E->sel_cy) + E->sel_cx;
    size_t end = li_get_offset(E->li, E->cy) + E->cx;
    if (start > end) { size_t tmp = start; start = end; end = tmp; }
    if (start == end) return;
    char *text = pt_get_text(E->pt, start, end - start);
    undo_push(E->undo_stack, ACTION_DELETE, start, text, end - start);
    free(text); pt_delete_fixed(E->pt, start, end - start);
    editor_move_to_offset(E, (int64_t)start); editor_sync_model(E); E->selecting = false; E->dirty = true; E->dirty_count++; editor_update_swap(E);
}

void editor_indent_selection(Editor *E, int dir) {
    if (!E->selecting || !is_full_line_selection(E)) return;
    int start_y = E->sel_cy, end_y = E->cy; bool cursor_was_at_end = (E->cy >= E->sel_cy);
    if (start_y > end_y) { int t = start_y; start_y = end_y; end_y = t; }
    int min_indent = 1000;
    if (dir == -1) {
        for (int y = start_y; y <= end_y; y++) {
            int indent = get_leading_spaces(E, y); if (indent == -1) continue;
            if (indent < min_indent) min_indent = indent;
        }
        if (min_indent == 1000 || min_indent == 0) return;
    }
    int to_move = (dir == 1) ? E->tab_size : (min_indent < E->tab_size ? min_indent : E->tab_size);
    for (int y = end_y; y >= start_y; y--) {
        size_t off = li_get_offset(E->li, y);
        if (dir == 1) {
            char spaces[16]; for(int k=0; k<to_move; k++) spaces[k] = ' '; spaces[to_move] = '\0';
            pt_insert(E->pt, off, spaces, to_move); undo_push(E->undo_stack, ACTION_INSERT, off, spaces, to_move);
        } else {
            if (get_leading_spaces(E, y) == -1) continue;
            char *txt = pt_get_text(E->pt, off, to_move); undo_push(E->undo_stack, ACTION_DELETE, off, txt, to_move);
            free(txt); pt_delete_fixed(E->pt, off, to_move);
        }
    }
    editor_sync_model(E); if (cursor_was_at_end) { E->sel_cx = 0; E->cx = (int)get_line_len(E, E->cy); } else { E->cx = 0; E->sel_cx = (int)get_line_len(E, E->sel_cy); }
    E->dirty = true; E->dirty_count++; editor_update_swap(E);
}

void editor_paste(Editor *E) {
    if (!E->clipboard) return;
    size_t offset = li_get_offset(E->li, E->cy) + E->cx;
    pt_insert(E->pt, offset, E->clipboard, strlen(E->clipboard));
    undo_push(E->undo_stack, ACTION_INSERT, offset, E->clipboard, strlen(E->clipboard));
    editor_sync_model(E); editor_move_to_offset(E, (int64_t)(offset + strlen(E->clipboard))); E->dirty = true; E->dirty_count++; editor_update_swap(E);
}

void editor_undo(Editor *E) {
    Action *a = undo_pop(E->undo_stack); if (!a) return;
    if (a->type == ACTION_INSERT) pt_delete_fixed(E->pt, a->offset, a->len); else pt_insert(E->pt, a->offset, a->data, a->len);
    editor_sync_model(E); E->dirty = true; E->dirty_count++; editor_update_swap(E);
}

void editor_redo(Editor *E) {
    Action *a = redo_pop(E->undo_stack); if (!a) return;
    if (a->type == ACTION_INSERT) pt_insert(E->pt, a->offset, a->data, a->len); else pt_delete_fixed(E->pt, a->offset, a->len);
    editor_sync_model(E); E->dirty = true; E->dirty_count++; editor_update_swap(E);
}

void editor_set_syntax(Editor *E) {
    int count = hl_get_syntax_count();
    int selected = 0;
    if (E->syntax) {
        for (int i = 0; i < count; i++) {
            if (hl_get_syntax_by_index(i) == E->syntax) {
                selected = i; break;
            }
        }
    }
    while (1) {
        struct abuf ab = ABUF_INIT;
        abAppend(&ab, "\x1b[?25l", 6);
        char header[256];
        int hlen = snprintf(header, sizeof(header), "\x1b[2;1H\x1b[1;33m SELECT FILETYPE \x1b[m> ");
        abAppend(&ab, header, hlen);
        int rows_to_show = count + 2;
        if (rows_to_show > E->terminal.screenrows - 2) rows_to_show = E->terminal.screenrows - 2;
        for (int i = 0; i < rows_to_show; i++) {
            char move[32]; snprintf(move, sizeof(move), "\x1b[%d;1H", i + 3);
            abAppend(&ab, move, (int)strlen(move));
            if (i < count) {
                if (i == selected) abAppend(&ab, "\x1b[7m > ", 7);
                else abAppend(&ab, "   ", 3);
                EditorSyntax *s = hl_get_syntax_by_index(i);
                abAppend(&ab, s->filetype, (int)strlen(s->filetype));
                abAppend(&ab, "\x1b[m\x1b[K", 6);
            } else {
                abAppend(&ab, "\x1b[K", 3);
            }
        }
        write(STDOUT_FILENO, ab.b, ab.len); abFree(&ab);
        char c;
        if (read(STDIN_FILENO, &c, 1) <= 0) continue;
        if (c == '\x1b') {
            char seq[4];
            if (read(STDIN_FILENO, &seq[0], 1) == 1 && seq[0] == '[') {
                if (read(STDIN_FILENO, &seq[1], 1) == 1) {
                    if (seq[1] == 'A') { selected--; if (selected < 0) selected = count - 1; }
                    else if (seq[1] == 'B') { selected++; if (selected >= count) selected = 0; }
                }
            }
            continue;
        }
        if (c == '\r') {
            E->syntax = hl_get_syntax_by_index(selected);
            return;
        }
    }
}

static int find_brace_block_end(Editor *E, int start_line) {
    int line_count = li_get_line_count(E->li);
    size_t start_off = li_get_offset(E->li, start_line);
    char *line = pt_get_text(E->pt, start_off, 256);
    if (!line) return -1;
    int has_open_brace = 0;
    for (int i = 0; line[i]; i++) {
        if (line[i] == '{') { has_open_brace = 1; break; }
    }
    free(line);
    if (!has_open_brace) return -1;
    int brace_count = 0;
    for (int y = start_line; y < line_count; y++) {
        size_t off = li_get_offset(E->li, y);
        size_t len = (y + 1 < line_count) ? li_get_offset(E->li, y + 1) - off : E->pt->total_length - off;
        char *l = pt_get_text(E->pt, off, len > 256 ? 256 : len);
        if (!l) continue;
        for (int i = 0; l[i]; i++) {
            if (l[i] == '{') brace_count++;
            else if (l[i] == '}') brace_count--;
        }
        free(l);
        if (brace_count == 0 && y > start_line) return y;
    }
    return -1;
}

static int find_fold_containing(Editor *E, int line) {
    for (int f = 0; f < E->fold_count; f++) {
        if (line > E->folds[f].start && line <= E->folds[f].end) return f;
    }
    return -1;
}

static int find_next_foldable(Editor *E, int start_line) {
    int line_count = li_get_line_count(E->li);
    for (int y = start_line; y < line_count; y++) {
        size_t off = li_get_offset(E->li, y);
        if (off >= E->pt->total_length) continue;
        size_t len = (y + 1 < line_count) ? li_get_offset(E->li, y + 1) - off : E->pt->total_length - off;
        char *l = pt_get_text(E->pt, off, len > 256 ? 256 : len);
        if (!l) continue;
        for (int i = 0; l[i]; i++) {
            if (l[i] == '{') { free(l); return y; }
        }
        free(l);
    }
    return -1;
}

void editor_toggle_fold(Editor *E) {
    int line_count = li_get_line_count(E->li);
    if (E->cy >= line_count) return;
    for (int f = 0; f < E->fold_count; f++) {
        if (E->cy == E->folds[f].start) {
            E->fold_count--;
            for (int i = f; i < E->fold_count; i++) {
                E->folds[i] = E->folds[i + 1];
            }
            return;
        }
    }
    int containing = find_fold_containing(E, E->cy);
    if (containing >= 0) {
        int fold_start = E->folds[containing].start;
        E->fold_count--;
        for (int i = containing; i < E->fold_count; i++) {
            E->folds[i] = E->folds[i + 1];
        }
        E->cy = fold_start;
        return;
    }
    if (E->fold_count >= MAX_FOLDS) return;
    int end = find_brace_block_end(E, E->cy);
    if (end < 0 || end <= E->cy) {
        int next = find_next_foldable(E, E->cy);
        if (next < 0) return;
        end = find_brace_block_end(E, next);
        if (end < 0 || end <= next) return;
        E->folds[E->fold_count].start = next;
        E->folds[E->fold_count].end = end;
        E->fold_count++;
    } else {
        E->folds[E->fold_count].start = E->cy;
        E->folds[E->fold_count].end = end;
        E->fold_count++;
    }
}

void editor_fold(Editor *E) {
    int line_count = li_get_line_count(E->li);
    if (E->cy >= line_count || E->fold_count >= MAX_FOLDS) return;
    int end = find_brace_block_end(E, E->cy);
    if (end < 0 || end <= E->cy) return;
    E->folds[E->fold_count].start = E->cy;
    E->folds[E->fold_count].end = end;
    E->fold_count++;
}

void editor_unfold(Editor *E) {
    if (E->fold_count == 0) return;
    E->fold_count--;
}

void editor_toggle_comment(Editor *E) {
    if (!E->syntax || !E->syntax->singleline_comment_start) return;
    int start_y = E->sel_cy, end_y = E->cy; if (start_y > end_y) { int t = start_y; start_y = end_y; end_y = t; }
    char *cs = E->syntax->singleline_comment_start; int cslen = strlen(cs); bool should_comment = false;
    for (int y = start_y; y <= end_y; y++) {
        if (is_line_blank(E, y)) continue;
        size_t off = li_get_offset(E->li, y); char *prefix = pt_get_text(E->pt, off, cslen);
        if (!prefix || strncmp(prefix, cs, cslen) != 0) should_comment = true;
        free(prefix); if (should_comment) break;
    }
    for (int y = end_y; y >= start_y; y--) {
        if (is_line_blank(E, y)) continue;
        size_t off = li_get_offset(E->li, y);
        if (should_comment) { pt_insert(E->pt, off, cs, cslen); undo_push(E->undo_stack, ACTION_INSERT, off, cs, cslen); }
        else {
            char *prefix = pt_get_text(E->pt, off, cslen);
            if (prefix && strncmp(prefix, cs, cslen) == 0) { undo_push(E->undo_stack, ACTION_DELETE, off, prefix, cslen); pt_delete_fixed(E->pt, off, cslen); }
            free(prefix);
        }
    }
    editor_sync_model(E); E->dirty = true; E->dirty_count++; editor_update_swap(E);
}

void editor_process_keypress(EditorManager *em) {
    Editor *E = em_get_active(em); if (!E) return;
    char c; if (read(STDIN_FILENO, &c, 1) <= 0) return;

if (c == '\x1b') {
        char seq[8] = {0};
        ssize_t n = read(STDIN_FILENO, &seq[0], 1);
        if (n != 1) { E->selecting = false; return; }
        if (seq[0] == 'w') {
            E->wrap_enabled = !E->wrap_enabled; return;
        }
        if (seq[0] == 'f') {
            editor_toggle_fold(E);
            return;
        }
        if (seq[0] == '[') {
            n = read(STDIN_FILENO, &seq[1], 1);
            if (n != 1) { E->selecting = false; return; }
            if (seq[1] == 'M') {
                if (editor_handle_mouse(E)) return;
            } else if (seq[1] == 'm') {
                return;
            } else if (seq[1] == '1' && read(STDIN_FILENO, &seq[2], 1) == 1 && seq[2] == ';' && read(STDIN_FILENO, &seq[3], 1) == 1 && read(STDIN_FILENO, &seq[4], 1) == 1) {
                if (seq[3] == '2' && seq[4] >= 'A' && seq[4] <= 'D') {
                    if (!E->selecting) { E->selecting = true; E->sel_cx = E->cx; E->sel_cy = E->cy; }
                    editor_move_cursor(E, seq[4]); return;
                }
                if (seq[3] == '3') {
                    if (seq[4] == 'C') { em_next(em); return; }
                    if (seq[4] == 'D') { em_prev(em); return; }
                }
            }
            if (seq[1] >= 'A' && seq[1] <= 'D') { E->selecting = false; editor_move_cursor(E, seq[1]); return; }
            if (seq[1] == 'w') { E->wrap_enabled = !E->wrap_enabled; return; }
        }
        E->selecting = false;
    }

    switch (c) {
        case '\r': editor_insert_newline(E); break;
        case '\t': if (E->selecting) editor_indent_selection(E, 1); else editor_insert_tab(E); break;
        case 'n': if (E->last_search) editor_find_next(E, E->last_search, 1); else editor_insert_char(E, c); break;
        case 'N': if (E->last_search) editor_find_next(E, E->last_search, -1); else editor_insert_char(E, c); break;
        case ctrl_key('q'): em_close_current(em); break;
        case ctrl_key('p'): editor_fuzzy_finder(em); break;
        case ctrl_key('w'): em_add_buffer(em, NULL); break;
        case ctrl_key('s'): editor_save(E); break;
        case ctrl_key('a'): editor_save_as(E); break;
        case ctrl_key('f'): editor_find(em); break;
        case ctrl_key('r'): editor_replace(em); break;
        case ctrl_key('j'): editor_goto_line(E); break;
        case ctrl_key('k'): editor_delete_line(E); break;
        case ctrl_key('o'): {
            char *selected = file_browser_open(E); if (selected) { em_add_buffer(em, selected); free(selected); }
            break;
        }
        case ctrl_key('h'): editor_show_help(E); break;
        case ctrl_key('l'):
            if (!E->selecting) { E->selecting = true; E->sel_cy = E->cy; E->sel_cx = 0; }
            if (E->cy < li_get_line_count(E->li) - 1) {
                int next = find_next_visible_line(E, E->cy);
                E->cy = next;
                E->cx = 0;
            } else E->cx = (int)get_line_len(E, E->cy);
            break;
        case ctrl_key('g'): if (E->last_search) editor_find_next(E, E->last_search, 1); break;
        case ctrl_key('z'): editor_undo(E); break;
        case ctrl_key('y'): editor_redo(E); break;
        case ctrl_key('c'): editor_copy(E); E->selecting = false; break;
        case ctrl_key('x'): editor_cut(E); break;
        case ctrl_key('v'): editor_paste(E); break;
        case ctrl_key('n'): E->show_line_numbers = !E->show_line_numbers; break;
        case ctrl_key('t'): editor_set_syntax(E); break;
        case 0x1d: editor_fold(E); break;
        case 0x1c: editor_unfold(E); break;
        case 127: if (E->selecting) editor_indent_selection(E, -1); else editor_delete_char(E); break;
        default: if (c == '/' && E->selecting) { editor_toggle_comment(E); E->selecting = false; break; }
                 if (!iscntrl(c)) { E->selecting = false; editor_insert_char(E, c); } break;
    }
}
