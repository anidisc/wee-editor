#define _GNU_SOURCE
#define _DEFAULT_SOURCE
#include "editor.h"
#include "utils.h"
#include "highlight.h"
#include "file_browser.h"
#include "help.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <ctype.h>
#include <string.h>

#define ctrl_key(k) ((k) & 0x1f)

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
    if (start_y > end_y) {
        int t = start_y; start_y = end_y; end_y = t;
        t = s_cx; s_cx = e_cx; e_cx = t;
    }
    size_t last_line_len = get_line_len(E, end_y);
    return (s_cx == 0 && e_cx >= (int)last_line_len);
}

static bool is_offset_selected(Editor *E, size_t offset) {
    if (!E->selecting) return false;
    size_t start = li_get_offset(E->li, E->sel_cy) + E->sel_cx;
    size_t end = li_get_offset(E->li, E->cy) + E->cx;
    if (start > end) { size_t tmp = start; start = end; end = tmp; }
    return offset >= start && offset < end;
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
    E->tab_size = 4;
    E->dirty = false;
    E->syntax = NULL;
    E->pt = pt_create("", 0);
    E->li = li_create();
    E->undo_stack = undo_create();
    if (terminal_get_size(&E->terminal.screenrows, &E->terminal.screencols) == -1) exit(1);
    E->terminal.screenrows--;
    E->vp = vp_create(E->terminal.screenrows);
}

static void editor_sync_model(Editor *E) {
    char *text = pt_get_text(E->pt, 0, E->pt->total_length);
    li_rebuild(E->li, text, E->pt->total_length);
    free(text);
}

void editor_load(Editor *E, const char *filename) {
    if (E->filename) free(E->filename);
    E->filename = strdup(filename);
    E->syntax = hl_get_syntax(filename);
    if (E->pt) pt_destroy(E->pt);
    E->pt = pt_open(filename);
    editor_sync_model(E);
    E->dirty = false;
}

void editor_save(Editor *E) {
    if (E->filename == NULL) { editor_save_as(E); return; }
    if (pt_save(E->pt, E->filename)) E->dirty = false;
}

void editor_save_as(Editor *E) {
    char *new_name = editor_prompt(E, "Save as", NULL);
    if (new_name) {
        if (E->filename) free(E->filename);
        E->filename = strdup(new_name);
        E->syntax = hl_get_syntax(new_name);
        if (pt_save(E->pt, E->filename)) E->dirty = false;
        free(new_name);
    }
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

void editor_find(Editor *E) {
    char *query = editor_prompt(E, "Search (Case Insensitive)", NULL);
    if (!query) { E->last_match_off = -1; E->search_match_len = 0; return; }
    int total_matches = pt_count_occurrences(E->pt, query);
    int64_t found = pt_find(E->pt, query, li_get_offset(E->li, E->cy) + E->cx, 1, true);
    if (found == -1) found = pt_find(E->pt, query, 0, 1, true);
    if (found != -1) { E->last_match_off = (int)found; E->search_match_len = (int)strlen(query); editor_move_to_offset(E, found); }
    while (1) {
        editor_refresh_screen(E);
        int current_idx = (E->last_match_off != -1) ? editor_find_match_index(E, query, E->last_match_off) : 0;
        char msg[256]; snprintf(msg, sizeof(msg), "\x1b[7m FIND: %s [%d/%d] (Arrows: next/prev, ESC: exit) \x1b[m", query, current_idx, total_matches);
        char move_buf[32]; snprintf(move_buf, sizeof(move_buf), "\x1b[%d;1H", E->terminal.screenrows + 1);
        write(STDOUT_FILENO, move_buf, strlen(move_buf));
        write(STDOUT_FILENO, msg, strlen(msg)); write(STDOUT_FILENO, "\x1b[K", 3);
        int ln_width = E->show_line_numbers ? snprintf(NULL, 0, "%d ", li_get_line_count(E->li)) + 1 : 0;
        snprintf(move_buf, sizeof(move_buf), "\x1b[%d;%dH", (E->cy - E->rowoff) + 1, (E->rx - E->coloff) + 1 + ln_width);
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
    if (E->last_search) { free(E->last_search); }
    E->last_search = query;
    E->last_match_off = -1;
    E->search_match_len = 0;
}

void editor_replace(Editor *E) {
    char *query = editor_prompt(E, "Find (Case Sensitive)", NULL);
    if (!query) return;
    char *replacement = editor_prompt(E, "Replace with", NULL);
    if (!replacement) { free(query); return; }
    int64_t found = pt_find(E->pt, query, 0, 1, false);
    while (found != -1) {
        E->last_match_off = (int)found;
        E->search_match_len = (int)strlen(query);
        editor_move_to_offset(E, found);
        editor_refresh_screen(E);
        char msg[256]; snprintf(msg, sizeof(msg), "\x1b[7m Replace '%s' with '%s'? (y/n/a/ESC) \x1b[m", query, replacement);
        char move_buf[32]; snprintf(move_buf, sizeof(move_buf), "\x1b[%d;1H", E->terminal.screenrows + 1);
        write(STDOUT_FILENO, move_buf, strlen(move_buf));
        write(STDOUT_FILENO, msg, strlen(msg)); write(STDOUT_FILENO, "\x1b[K", 3);
        int ln_width = E->show_line_numbers ? snprintf(NULL, 0, "%d ", li_get_line_count(E->li)) + 1 : 0;
        snprintf(move_buf, sizeof(move_buf), "\x1b[%d;%dH", (E->cy - E->rowoff) + 1, (E->rx - E->coloff) + 1 + ln_width);
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
            editor_sync_model(E); E->dirty = true;
            if (c == 'y' || c == 'Y') found = pt_find(E->pt, query, (size_t)found + rlen, 1, false);
            else { found = pt_find(E->pt, query, (size_t)found + rlen, 1, false); continue; }
        } else if (c == 'n' || c == 'N') found = pt_find(E->pt, query, (size_t)found + 1, 1, false);
        else break;
    }
    E->last_match_off = -1; E->search_match_len = 0; free(query); free(replacement);
}

void editor_open_browser(Editor *E) {
    if (E->dirty) {
        if (!editor_confirm(E, "Unsaved changes! Open another file anyway? (y/n)")) return;
    }
    char *selected = file_browser_open(E);
    if (selected) { editor_load(E, selected); E->cx = E->cy = 0; E->rowoff = E->coloff = 0; free(selected); }
}

char *editor_prompt(Editor *E, char *prompt, void (*callback)(Editor *, char *, int)) {
    size_t bufsize = 128; char *buf = malloc(bufsize); size_t buflen = 0; buf[0] = '\0';
    while (1) {
        struct abuf ab = ABUF_INIT; char status[256];
        int len = snprintf(status, sizeof(status), "\x1b[7m%s: %s\x1b[m", prompt, buf);
        abAppend(&ab, "\x1b[?25l", 6);
        char move_buf[32]; snprintf(move_buf, sizeof(move_buf), "\x1b[%d;1H", E->terminal.screenrows + 1);
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

bool editor_confirm(Editor *E, char *prompt) {
    while (1) {
        struct abuf ab = ABUF_INIT; char status[256];
        int len = snprintf(status, sizeof(status), "\x1b[7m%s\x1b[m", prompt);
        abAppend(&ab, "\x1b[?25l", 6);
        char move_buf[32]; snprintf(move_buf, sizeof(move_buf), "\x1b[%d;1H", E->terminal.screenrows + 1);
        abAppend(&ab, move_buf, (int)strlen(move_buf)); abAppend(&ab, status, len);
        abAppend(&ab, "\x1b[K", 3);
        write(STDOUT_FILENO, ab.b, ab.len); abFree(&ab);
        char c = '\0'; if (read(STDIN_FILENO, &c, 1) <= 0) continue;
        if (c == 'y' || c == 'Y') return true;
        if (c == 'n' || c == 'N' || c == '\x1b') return false;
    }
}

void editor_scroll(Editor *E) {
    if (E->cy < E->rowoff) E->rowoff = E->cy;
    if (E->cy >= E->rowoff + E->terminal.screenrows) E->rowoff = E->cy - E->terminal.screenrows + 1;
    if (E->rx < E->coloff) E->coloff = E->rx;
    if (E->rx >= E->coloff + E->terminal.screencols - (E->show_line_numbers ? 6 : 0)) 
        E->coloff = E->rx - (E->terminal.screencols - (E->show_line_numbers ? 6 : 0)) + 1;
}

void editor_resize(Editor *E) {
    if (terminal_get_size(&E->terminal.screenrows, &E->terminal.screencols) == -1) return;
    E->terminal.screenrows--; vp_destroy(E->vp); E->vp = vp_create(E->terminal.screenrows);
}

void editor_refresh_screen(Editor *E) {
    editor_scroll(E); vp_sync(E->vp, E->pt, E->li, E->rowoff);
    for (int i = 0; i < E->terminal.screenrows; i++) {
        ViewLine *vl = &E->vp->lines[i];
        hl_apply(vl->chars, vl->len, vl->hl, E->syntax);
    }
    int v_idx = E->cy - E->rowoff;
    if (v_idx >= 0 && v_idx < E->terminal.screenrows) {
        ViewLine *vl = &E->vp->lines[v_idx];
        if (E->cx > vl->len) E->cx = vl->len;
        E->rx = (vl->len > 0) ? vl->cx_to_rx[E->cx] : 0;
    } else E->rx = 0;
    struct abuf ab = ABUF_INIT;
    abAppend(&ab, "\x1b[?25l", 6); abAppend(&ab, "\x1b[H", 3);
    int ln_width = 0;
    if (E->show_line_numbers) { int total_lines = li_get_line_count(E->li); ln_width = snprintf(NULL, 0, "%d", total_lines) + 1; }
    for (int i = 0; i < E->terminal.screenrows; i++) {
        int filerow = i + E->rowoff; abAppend(&ab, "\x1b[m", 3);
        if (E->show_line_numbers) {
            char ln_buf[32];
            if (filerow < li_get_line_count(E->li)) { const char *color = (filerow == E->cy) ? "\x1b[37m" : "\x1b[90m"; int n = snprintf(ln_buf, sizeof(ln_buf), "%s%*d \x1b[m", color, ln_width, filerow + 1); abAppend(&ab, ln_buf, n); }
            else { int n = snprintf(ln_buf, sizeof(ln_buf), "\x1b[90m%*s \x1b[m", ln_width, "~"); abAppend(&ab, ln_buf, n); }
        }
        ViewLine *vl = &E->vp->lines[i];
        size_t line_start_off = li_get_offset(E->li, filerow);
        if (vl->visual_len > E->coloff) {
            int drawlen = vl->visual_len - E->coloff; int effective_cols = E->terminal.screencols - (E->show_line_numbers ? ln_width + 1 : 0);
            if (drawlen > effective_cols) drawlen = effective_cols;
            int start_idx = 0; while (start_idx < vl->len && vl->cx_to_rx[start_idx] < E->coloff) start_idx++;
            
            int first_code = -1, last_code = -1;
            for (int k = 0; k < vl->len; k++) {
                if (!isspace((unsigned char)vl->chars[k])) {
                    if (first_code == -1) first_code = k;
                    last_code = k;
                }
            }

            int current_color = -1; int current_visual_pos = vl->cx_to_rx[start_idx];
            for (int j = start_idx; j < vl->len && current_visual_pos < E->coloff + drawlen; j++) {
                int char_off = (int)line_start_off + j; int color = vl->hl[j];
                if (E->last_match_off != -1 && char_off >= E->last_match_off && char_off < E->last_match_off + E->search_match_len) color = HL_MATCH;
                
                if (is_offset_selected(E, (size_t)char_off)) {
                    if (E->cy != E->sel_cy) {
                        if (first_code != -1 && j >= first_code && j <= last_code) {
                            color = HL_SELECT;
                        }
                    } else {
                        color = HL_SELECT;
                    }
                }

                if (color != current_color) { const char *ansi = hl_to_ansi(color); abAppend(&ab, ansi, (int)strlen(ansi)); current_color = color; }
                abAppend(&ab, &vl->chars[j], 1); current_visual_pos = vl->cx_to_rx[j+1];
            }
            abAppend(&ab, "\x1b[m", 3); // FULL RESET before clearing to avoid right-side bleeding
        }
        abAppend(&ab, "\x1b[K\r\n", 5);
    }
    abAppend(&ab, "\x1b[m", 3); abAppend(&ab, "\x1b[7m", 4);
    char status[128], rstatus[64];
    const char *display_name = E->filename ? strrchr(E->filename, '/') : NULL;
    display_name = display_name ? display_name + 1 : (E->filename ? E->filename : "[No Name]");
    const char *ftype = E->syntax ? E->syntax->filetype : "no ft";
    int len = snprintf(status, sizeof(status), " %s - %d lines (%s) %s", display_name, li_get_line_count(E->li), ftype, E->dirty ? "(modified)" : "");
    int rstatus_len = snprintf(rstatus, sizeof(rstatus), "LN: %s %d:%d ", E->show_line_numbers ? "ON" : "OFF", E->cy + 1, E->rx + 1);
    if (len > E->terminal.screencols) len = E->terminal.screencols;
    abAppend(&ab, status, len);
    while (len < E->terminal.screencols) { if (E->terminal.screencols - len == rstatus_len) { abAppend(&ab, rstatus, rstatus_len); break; } else { abAppend(&ab, " ", 1); len++; } }
    abAppend(&ab, "\x1b[m", 3);
    char buf[32]; int cursor_x = (E->rx - E->coloff) + 1 + (E->show_line_numbers ? ln_width + 1 : 0);
    int n = snprintf(buf, sizeof(buf), "\x1b[%d;%dH", (E->cy - E->rowoff) + 1, cursor_x);
    abAppend(&ab, buf, n); abAppend(&ab, "\x1b[?25h", 6);
    write(STDOUT_FILENO, ab.b, ab.len); abFree(&ab);
}

void editor_move_cursor(Editor *E, int key) {
    int line_count = li_get_line_count(E->li);
    int v_idx = E->cy - E->rowoff;
    int current_line_len = (E->cy < line_count && v_idx >= 0 && v_idx < E->terminal.screenrows) ? E->vp->lines[v_idx].len : 0;
    switch (key) {
        case 'A': if (E->cy > 0) E->cy--; break;
        case 'B': if (E->cy < line_count - 1) E->cy++; break;
        case 'D': if (E->cx > 0) E->cx--; else if (E->cy > 0) { E->cy--; editor_sync_model(E); vp_sync(E->vp, E->pt, E->li, E->rowoff); int idx = E->cy - E->rowoff; if (idx >= 0) E->cx = E->vp->lines[idx].len; } break;
        case 'C': if (E->cx < current_line_len) E->cx++; else if (E->cy < line_count - 1) { E->cy++; E->cx = 0; } break;
    }
}

void editor_insert_char(Editor *E, int c) {
    size_t offset = li_get_offset(E->li, E->cy) + E->cx;
    char ch = (char)c;
    char paired = 0;
    if (ch == '(') paired = ')';
    else if (ch == '[') paired = ']';
    else if (ch == '{') paired = '}';
    else if (ch == '"') paired = '"';
    else if (ch == '\'') paired = '\'';
    if (paired) {
        char buf[2] = {ch, paired}; pt_insert(E->pt, offset, buf, 2);
        undo_push(E->undo_stack, ACTION_INSERT, offset, buf, 2);
        editor_sync_model(E); E->cx++; E->dirty = true; return;
    }
    if (ch == ')' || ch == ']' || ch == '}' || ch == '"' || ch == '\'') {
        size_t next_off = li_get_offset(E->li, E->cy) + E->cx;
        if (next_off < E->pt->total_length) {
            char *next_c = pt_get_text(E->pt, next_off, 1);
            if (next_c && next_c[0] == ch) { free(next_c); E->cx++; return; }
            if (next_c) free(next_c);
        }
    }
    pt_insert(E->pt, offset, &ch, 1);
    undo_push(E->undo_stack, ACTION_INSERT, offset, &ch, 1);
    editor_sync_model(E); E->cx++; E->dirty = true;
}

void editor_insert_newline(Editor *E) {
    size_t offset = li_get_offset(E->li, E->cy) + E->cx;
    size_t line_start = li_get_offset(E->li, E->cy);
    size_t line_len = E->cx;
    char *line_text = pt_get_text(E->pt, line_start, line_len);
    int indent_len = 0;
    while (indent_len < (int)line_len && (line_text[indent_len] == ' ' || line_text[indent_len] == '\t')) indent_len++;
    char *nl_with_indent = malloc(indent_len + 2);
    nl_with_indent[0] = '\n'; if (indent_len > 0) memcpy(nl_with_indent + 1, line_text, indent_len);
    nl_with_indent[indent_len + 1] = '\0';
    pt_insert(E->pt, offset, nl_with_indent, indent_len + 1);
    undo_push(E->undo_stack, ACTION_INSERT, offset, nl_with_indent, indent_len + 1);
    free(line_text); free(nl_with_indent); editor_sync_model(E);
    E->cy++; E->cx = indent_len; E->dirty = true;
}

void editor_delete_char(Editor *E) {
    if (E->cx == 0 && E->cy == 0) return;
    size_t offset = li_get_offset(E->li, E->cy) + E->cx;
    if (E->cx > 0) {
        size_t line_start = li_get_offset(E->li, E->cy);
        char *before = pt_get_text(E->pt, line_start, E->cx);
        bool all_spaces = true;
        for (int i = 0; i < E->cx; i++) if (before[i] != ' ') { all_spaces = false; break; }
        if (all_spaces && (E->cx > 0 && E->cx % E->tab_size == 0)) {
            int to_del = E->tab_size;
            char *deleted_text = pt_get_text(E->pt, offset - to_del, to_del);
            undo_push(E->undo_stack, ACTION_DELETE, offset - to_del, deleted_text, to_del);
            free(deleted_text); pt_delete_fixed(E->pt, offset - to_del, to_del);
            E->cx -= to_del; free(before); editor_sync_model(E); E->dirty = true; return;
        }
        free(before);
    }
    char *deleted_text = pt_get_text(E->pt, offset - 1, 1);
    undo_push(E->undo_stack, ACTION_DELETE, offset - 1, deleted_text, 1);
    free(deleted_text); pt_delete_fixed(E->pt, offset - 1, 1);
    if (E->cx > 0) E->cx--;
    else { E->cy--; editor_sync_model(E); vp_sync(E->vp, E->pt, E->li, E->rowoff); int v_idx = E->cy - E->rowoff; if (v_idx >= 0 && v_idx < E->terminal.screenrows) E->cx = E->vp->lines[v_idx].len; }
    editor_sync_model(E); E->dirty = true;
}

void editor_del_char(Editor *E) {
    int line_count = li_get_line_count(E->li);
    int v_idx = E->cy - E->rowoff;
    if (E->cy == line_count - 1 && E->cx == (v_idx >= 0 ? E->vp->lines[v_idx].len : 0)) return;
    size_t offset = li_get_offset(E->li, E->cy) + E->cx;
    char *deleted_text = pt_get_text(E->pt, offset, 1);
    undo_push(E->undo_stack, ACTION_DELETE, offset, deleted_text, 1);
    free(deleted_text); pt_delete_fixed(E->pt, offset, 1);
    editor_sync_model(E); E->dirty = true;
}

void editor_insert_tab(Editor *E) { for (int i = 0; i < E->tab_size; i++) editor_insert_char(E, ' '); }

void editor_copy(Editor *E) {
    size_t start, end;
    if (E->selecting) {
        start = li_get_offset(E->li, E->sel_cy) + E->sel_cx;
        end = li_get_offset(E->li, E->cy) + E->cx;
        if (start > end) { size_t tmp = start; start = end; end = tmp; }
    } else {
        start = li_get_offset(E->li, E->cy);
        if (E->cy + 1 < li_get_line_count(E->li)) end = li_get_offset(E->li, E->cy + 1);
        else end = E->pt->total_length;
    }
    if (start >= end) return;
    char *text = pt_get_text(E->pt, start, end - start);
    if (E->clipboard) free(E->clipboard);
    E->clipboard = text;
}

void editor_cut(Editor *E) {
    size_t start, end;
    bool was_selecting = E->selecting;
    if (E->selecting) {
        start = li_get_offset(E->li, E->sel_cy) + E->sel_cx;
        end = li_get_offset(E->li, E->cy) + E->cx;
        if (start > end) { size_t tmp = start; start = end; end = tmp; }
    } else {
        start = li_get_offset(E->li, E->cy);
        if (E->cy + 1 < li_get_line_count(E->li)) end = li_get_offset(E->li, E->cy + 1);
        else end = E->pt->total_length;
    }
    if (start >= end) return;
    char *text = pt_get_text(E->pt, start, end - start);
    if (E->clipboard) free(E->clipboard);
    E->clipboard = text;
    undo_push(E->undo_stack, ACTION_DELETE, start, E->clipboard, end - start);
    pt_delete_fixed(E->pt, start, end - start);
    if (was_selecting) editor_move_to_offset(E, (int64_t)start);
    else E->cx = 0;
    editor_sync_model(E); E->selecting = false; E->dirty = true;
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
    editor_move_to_offset(E, (int64_t)start);
    editor_sync_model(E); E->selecting = false; E->dirty = true;
}

void editor_indent_selection(Editor *E, int dir) {
    if (!E->selecting) return;
    if (!is_full_line_selection(E)) return;
    
    int start_y = E->sel_cy, end_y = E->cy;
    bool cursor_was_at_end = (E->cy >= E->sel_cy);
    if (start_y > end_y) { int t = start_y; start_y = end_y; end_y = t; }

    int min_indent = 1000;
    if (dir == -1) {
        for (int y = start_y; y <= end_y; y++) {
            int indent = get_leading_spaces(E, y);
            if (indent == -1) continue;
            if (indent < min_indent) min_indent = indent;
        }
        if (min_indent == 1000 || min_indent == 0) return;
    }

    int to_move = (dir == 1) ? E->tab_size : (min_indent < E->tab_size ? min_indent : E->tab_size);

    for (int y = end_y; y >= start_y; y--) {
        size_t off = li_get_offset(E->li, y);
        if (dir == 1) {
            char spaces[16]; for(int k=0; k<to_move; k++) spaces[k] = ' '; spaces[to_move] = '\0';
            pt_insert(E->pt, off, spaces, to_move);
            undo_push(E->undo_stack, ACTION_INSERT, off, spaces, to_move);
        } else {
            if (get_leading_spaces(E, y) == -1) continue;
            char *txt = pt_get_text(E->pt, off, to_move);
            undo_push(E->undo_stack, ACTION_DELETE, off, txt, to_move);
            free(txt); pt_delete_fixed(E->pt, off, to_move);
        }
    }
    
    editor_sync_model(E);
    if (cursor_was_at_end) { E->sel_cx = 0; E->cx = (int)get_line_len(E, E->cy); }
    else { E->cx = 0; E->sel_cx = (int)get_line_len(E, E->sel_cy); }
    E->dirty = true;
}

void editor_paste(Editor *E) {
    if (!E->clipboard) return;
    size_t offset = li_get_offset(E->li, E->cy) + E->cx;
    pt_insert(E->pt, offset, E->clipboard, strlen(E->clipboard));
    undo_push(E->undo_stack, ACTION_INSERT, offset, E->clipboard, strlen(E->clipboard));
    editor_sync_model(E); editor_move_to_offset(E, (int64_t)(offset + strlen(E->clipboard)));
    E->dirty = true;
}

void editor_undo(Editor *E) {
    Action *a = undo_pop(E->undo_stack); if (!a) return;
    if (a->type == ACTION_INSERT) pt_delete_fixed(E->pt, a->offset, a->len);
    else pt_insert(E->pt, a->offset, a->data, a->len);
    editor_sync_model(E); E->dirty = true;
}

void editor_redo(Editor *E) {
    Action *a = redo_pop(E->undo_stack); if (!a) return;
    if (a->type == ACTION_INSERT) pt_insert(E->pt, a->offset, a->data, a->len);
    else pt_delete_fixed(E->pt, a->offset, a->len);
    editor_sync_model(E); E->dirty = true;
}

static bool is_line_blank(Editor *E, int y) {
    size_t off = li_get_offset(E->li, y);
    size_t next_off = (y + 1 < li_get_line_count(E->li)) ? li_get_offset(E->li, y+1) : E->pt->total_length;
    int len = (int)(next_off - off);
    if (len <= 0) return true;
    char *text = pt_get_text(E->pt, off, len);
    bool blank = true;
    for (int i = 0; i < len; i++) {
        if (text[i] != '\n' && text[i] != '\r' && !isspace(text[i])) { blank = false; break; }
    }
    free(text); return blank;
}

void editor_toggle_comment(Editor *E) {
    if (!E->syntax || !E->syntax->singleline_comment_start) return;
    int start_y = E->sel_cy, end_y = E->cy;
    if (start_y > end_y) { int t = start_y; start_y = end_y; end_y = t; }
    char *cs = E->syntax->singleline_comment_start;
    int cslen = strlen(cs);
    bool should_comment = false;
    for (int y = start_y; y <= end_y; y++) {
        if (is_line_blank(E, y)) continue;
        size_t off = li_get_offset(E->li, y);
        char *prefix = pt_get_text(E->pt, off, cslen);
        if (!prefix || strncmp(prefix, cs, cslen) != 0) should_comment = true;
        free(prefix); if (should_comment) break;
    }
    for (int y = end_y; y >= start_y; y--) {
        if (is_line_blank(E, y)) continue;
        size_t off = li_get_offset(E->li, y);
        if (should_comment) {
            pt_insert(E->pt, off, cs, cslen);
            undo_push(E->undo_stack, ACTION_INSERT, off, cs, cslen);
        } else {
            char *prefix = pt_get_text(E->pt, off, cslen);
            if (prefix && strncmp(prefix, cs, cslen) == 0) {
                undo_push(E->undo_stack, ACTION_DELETE, off, prefix, cslen);
                pt_delete_fixed(E->pt, off, cslen);
            }
            free(prefix);
        }
    }
    editor_sync_model(E); E->dirty = true;
}

void editor_process_keypress(Editor *E) {
    char c; if (read(STDIN_FILENO, &c, 1) <= 0) return;
    switch (c) {
        case '\r': editor_insert_newline(E); break;
        case '\t': if (E->selecting) editor_indent_selection(E, 1); else editor_insert_tab(E); break;
        case ctrl_key('q'):
            if (E->dirty) { if (!editor_confirm(E, "Unsaved changes! Quit anyway? (y/n)")) break; }
            terminal_disable_raw(&E->terminal); write(STDOUT_FILENO, "\x1b[2J\x1b[H", 7); exit(0); break;
        case ctrl_key('s'): editor_save(E); break;
        case ctrl_key('a'): editor_save_as(E); break;
        case ctrl_key('f'): editor_find(E); break;
        case ctrl_key('r'): editor_replace(E); break;
        case ctrl_key('o'): editor_open_browser(E); break;
        case ctrl_key('h'): editor_show_help(E); break;
        case ctrl_key('g'): if (E->last_search) editor_find_next(E, E->last_search, 1); break;
        case ctrl_key('p'): if (E->last_search) editor_find_next(E, E->last_search, -1); break;
        case ctrl_key('z'): editor_undo(E); break;
        case ctrl_key('y'): editor_redo(E); break;
        case ctrl_key('c'): editor_copy(E); E->selecting = false; break;
        case ctrl_key('x'): editor_cut(E); break;
        case ctrl_key('v'): editor_paste(E); break;
        case ctrl_key('n'): E->show_line_numbers = !E->show_line_numbers; break;
        case 127: if (E->selecting) editor_indent_selection(E, -1); else editor_delete_char(E); break;
        case '\x1b': {
            char seq[5]; if (read(STDIN_FILENO, &seq[0], 1) != 1) break;
            if (seq[0] == 'O') {
                if (read(STDIN_FILENO, &seq[1], 1) != 1) break;
                if (seq[1] == 'P') editor_show_help(E); // F1
            } else if (seq[0] == '[') {
                if (read(STDIN_FILENO, &seq[1], 1) != 1) break;
                if (seq[1] >= '0' && seq[1] <= '9') {
                    if (read(STDIN_FILENO, &seq[2], 1) != 1) break;
                    if (seq[2] == ';') {
                        if (read(STDIN_FILENO, &seq[3], 1) != 1) break;
                        if (read(STDIN_FILENO, &seq[4], 1) != 1) break;
                        if (seq[3] == '2') { if (!E->selecting) { E->selecting = true; E->sel_cx = E->cx; E->sel_cy = E->cy; } editor_move_cursor(E, seq[4]); }
                    } else if (seq[2] == '~' && seq[1] == '3') { if (E->selecting) editor_delete_selection(E); else editor_del_char(E); }
                } else { E->selecting = false; editor_move_cursor(E, seq[1]); }
            }
            break;
        }
        default: 
            if (c == '/' && E->selecting) { editor_toggle_comment(E); E->selecting = false; break; }
            if (!iscntrl(c)) { E->selecting = false; editor_insert_char(E, c); } break;
    }
}
