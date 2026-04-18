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
    E->dirty = false;
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
        if (pt_save(E->pt, E->filename)) E->dirty = false;
        free(new_name);
    }
}

static void editor_move_to_offset(Editor *E, int64_t offset) {
    int line_count = li_get_line_count(E->li);
    for (int i = 0; i < line_count; i++) {
        size_t start = li_get_offset(E->li, i);
        size_t end = (i + 1 < line_count) ? li_get_offset(E->li, i + 1) : E->pt->total_length;
        if (offset >= (int64_t)start && offset < (int64_t)end) {
            E->cy = i; E->cx = (int)(offset - start); break;
        }
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
    if (E->last_search) free(E->last_search); E->last_search = query; E->last_match_off = -1; E->search_match_len = 0;
}

void editor_replace(Editor *E) {
    char *query = editor_prompt(E, "Find (Case Sensitive)", NULL);
    if (!query) return;
    char *replacement = editor_prompt(E, "Replace with", NULL);
    if (!replacement) { free(query); return; }

    // Always start from the beginning for Replace
    int64_t found = pt_find(E->pt, query, 0, 1, false);

    while (found != -1) {
        E->last_match_off = (int)found;
        E->search_match_len = (int)strlen(query);
        editor_move_to_offset(E, found);
        editor_refresh_screen(E);

        char msg[256];
        snprintf(msg, sizeof(msg), "\x1b[7m Replace '%s' with '%s'? (y/n/a/ESC) \x1b[m", query, replacement);
        char move_buf[32];
        snprintf(move_buf, sizeof(move_buf), "\x1b[%d;1H", E->terminal.screenrows + 1);
        write(STDOUT_FILENO, move_buf, strlen(move_buf));
        write(STDOUT_FILENO, msg, strlen(msg)); write(STDOUT_FILENO, "\x1b[K", 3);
        int ln_width = E->show_line_numbers ? snprintf(NULL, 0, "%d ", li_get_line_count(E->li)) + 1 : 0;
        snprintf(move_buf, sizeof(move_buf), "\x1b[%d;%dH", (E->cy - E->rowoff) + 1, (E->rx - E->coloff) + 1 + ln_width);
        write(STDOUT_FILENO, move_buf, strlen(move_buf));

        char c = '\0';
        if (read(STDIN_FILENO, &c, 1) <= 0) continue;
        if (c == '\x1b') break;
        if (c == 'y' || c == 'Y' || c == 'a' || c == 'A') {
            size_t qlen = strlen(query);
            size_t rlen = strlen(replacement);
            char *deleted = pt_get_text(E->pt, (size_t)found, qlen);
            undo_push(E->undo_stack, ACTION_DELETE, (size_t)found, deleted, qlen);
            free(deleted);
            pt_delete_fixed(E->pt, (size_t)found, qlen);
            pt_insert(E->pt, (size_t)found, replacement, rlen);
            undo_push(E->undo_stack, ACTION_INSERT, (size_t)found, replacement, rlen);
            editor_sync_model(E);
            E->dirty = true;
            if (c == 'y' || c == 'Y') {
                found = pt_find(E->pt, query, (size_t)found + rlen, 1, false);
            } else {
                found = pt_find(E->pt, query, (size_t)found + rlen, 1, false);
                continue;
            }
        } else if (c == 'n' || c == 'N') {
            found = pt_find(E->pt, query, (size_t)found + 1, 1, false);
        } else break;
    }
    E->last_match_off = -1; E->search_match_len = 0;
    free(query); free(replacement);
}

void editor_open_browser(Editor *E) {
    if (E->dirty) {
        char *ans = editor_prompt(E, "Unsaved changes! Open another file anyway? (y/n)", NULL);
        if (!ans || (ans[0] != 'y' && ans[0] != 'Y')) { free(ans); return; }
        free(ans);
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
        if (nread == -1) return NULL; if (nread == 0) continue;
        if (c == '\r') { if (buflen != 0) return buf; }
        else if (c == '\x1b') { free(buf); return NULL; }
        else if (c == 127) { if (buflen != 0) buf[--buflen] = '\0'; }
        else if (!iscntrl(c)) {
            if (buflen == bufsize - 1) { bufsize *= 2; buf = realloc(buf, bufsize); }
            buf[buflen++] = c; buf[buflen] = '\0'; if (callback) callback(E, buf, c);
        }
    }
}

void editor_scroll(Editor *E) {
    if (E->cy < E->rowoff) E->rowoff = E->cy;
    if (E->cy >= E->rowoff + E->terminal.screenrows) E->rowoff = E->cy - E->terminal.screenrows + 1;
    if (E->rx < E->coloff) E->coloff = E->rx;
    if (E->rx >= E->coloff + E->terminal.screencols - (E->show_line_numbers ? 6 : 0)) 
        E->coloff = E->rx - (E->terminal.screencols - (E->show_line_numbers ? 6 : 0)) + 1;
}

static bool is_offset_selected(Editor *E, size_t offset) {
    if (!E->selecting) return false;
    size_t start = li_get_offset(E->li, E->sel_cy) + E->sel_cx;
    size_t end = li_get_offset(E->li, E->cy) + E->cx;
    if (start > end) { size_t tmp = start; start = end; end = tmp; }
    return offset >= start && offset < end;
}

void editor_resize(Editor *E) {
    if (terminal_get_size(&E->terminal.screenrows, &E->terminal.screencols) == -1) return;
    E->terminal.screenrows--; // Room for status bar
    
    // Re-create viewport with new size
    vp_destroy(E->vp);
    E->vp = vp_create(E->terminal.screenrows);
}

void editor_refresh_screen(Editor *E) {
    editor_scroll(E); vp_sync(E->vp, E->pt, E->li, E->rowoff);
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
            int current_color = -1; int current_visual_pos = vl->cx_to_rx[start_idx];
            for (int j = start_idx; j < vl->len && current_visual_pos < E->coloff + drawlen; j++) {
                int char_off = (int)line_start_off + j; int color = vl->hl[j];
                if (E->last_match_off != -1 && char_off >= E->last_match_off && char_off < E->last_match_off + E->search_match_len) color = HL_MATCH;
                if (is_offset_selected(E, char_off)) color = HL_SELECT;
                if (color != current_color) { const char *ansi = hl_to_ansi(color); abAppend(&ab, ansi, (int)strlen(ansi)); current_color = color; }
                abAppend(&ab, &vl->chars[j], 1); current_visual_pos = vl->cx_to_rx[j+1];
            }
            abAppend(&ab, "\x1b[39;49m", 7);
        }
        abAppend(&ab, "\x1b[K\r\n", 5);
    }
    abAppend(&ab, "\x1b[7m", 4);
    char status[128], rstatus[64];
    const char *display_name = E->filename ? strrchr(E->filename, '/') : NULL;
    display_name = display_name ? display_name + 1 : (E->filename ? E->filename : "[No Name]");
    
    int len = snprintf(status, sizeof(status), " %s - %d lines %s", display_name, li_get_line_count(E->li), E->dirty ? "(modified)" : "");
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
    char ch = (char)c; pt_insert(E->pt, offset, &ch, 1);
    undo_push(E->undo_stack, ACTION_INSERT, offset, &ch, 1);
    editor_sync_model(E); E->cx++; E->dirty = true;
}

void editor_insert_newline(Editor *E) {
    size_t offset = li_get_offset(E->li, E->cy) + E->cx;
    char nl = '\n'; pt_insert(E->pt, offset, &nl, 1);
    undo_push(E->undo_stack, ACTION_INSERT, offset, &nl, 1);
    editor_sync_model(E); E->cy++; E->cx = 0; E->dirty = true;
}

void editor_delete_char(Editor *E) {
    if (E->cx == 0 && E->cy == 0) return;
    size_t offset = li_get_offset(E->li, E->cy) + E->cx;
    char *deleted_text = pt_get_text(E->pt, offset - 1, 1);
    undo_push(E->undo_stack, ACTION_DELETE, offset - 1, deleted_text, 1);
    free(deleted_text);
    pt_delete_fixed(E->pt, offset - 1, 1);
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
    free(deleted_text);
    pt_delete_fixed(E->pt, offset, 1);
    editor_sync_model(E); E->dirty = true;
}

void editor_insert_tab(Editor *E) { for (int i = 0; i < 4; i++) editor_insert_char(E, ' '); }

void editor_copy(Editor *E) {
    if (!E->selecting) return;
    size_t start = li_get_offset(E->li, E->sel_cy) + E->sel_cx;
    size_t end = li_get_offset(E->li, E->cy) + E->cx;
    if (start > end) { size_t tmp = start; start = end; end = tmp; }
    if (E->clipboard) free(E->clipboard);
    E->clipboard = pt_get_text(E->pt, start, end - start);
}

void editor_cut(Editor *E) {
    if (!E->selecting) return;
    editor_copy(E);
    size_t start = li_get_offset(E->li, E->sel_cy) + E->sel_cx;
    size_t end = li_get_offset(E->li, E->cy) + E->cx;
    if (start > end) { size_t tmp = start; start = end; end = tmp; }
    size_t len = end - start;
    pt_delete_fixed(E->pt, start, len);
    editor_move_to_offset(E, start);
    editor_sync_model(E);
    E->selecting = false; E->dirty = true;
}

void editor_paste(Editor *E) {
    if (!E->clipboard) return;
    size_t offset = li_get_offset(E->li, E->cy) + E->cx;
    pt_insert(E->pt, offset, E->clipboard, strlen(E->clipboard));
    editor_move_to_offset(E, offset + strlen(E->clipboard));
    editor_sync_model(E); E->dirty = true;
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

void editor_process_keypress(Editor *E) {
    char c; if (read(STDIN_FILENO, &c, 1) <= 0) return;
    switch (c) {
        case '\r': editor_insert_newline(E); break;
        case '\t': editor_insert_tab(E); break;
        case ctrl_key('q'):
            if (E->dirty) { char *ans = editor_prompt(E, "Unsaved changes! Quit anyway? (y/n)", NULL); if (!ans || (ans[0] != 'y' && ans[0] != 'Y')) { free(ans); break; } free(ans); }
            terminal_disable_raw(&E->terminal); write(STDOUT_FILENO, "\x1b[2J\x1b[H", 7); exit(0); break;
        case ctrl_key('s'): editor_save(E); break;
        case ctrl_key('a'): editor_save_as(E); break;
        case ctrl_key('f'): editor_find(E); break;
        case ctrl_key('r'): editor_replace(E); break;
        case ctrl_key('o'): editor_open_browser(E); break;
        case ctrl_key('h'): editor_show_help(E); break;
        case ctrl_key('z'): editor_undo(E); break;
        case ctrl_key('y'): editor_redo(E); break;
        case ctrl_key('c'): editor_copy(E); E->selecting = false; break;
        case ctrl_key('x'): editor_cut(E); break;
        case ctrl_key('v'): editor_paste(E); break;
        case ctrl_key('n'): E->show_line_numbers = !E->show_line_numbers; break;
        case 'n': if (E->last_search) editor_find_next(E, E->last_search, 1); break;
        case 'N': if (E->last_search) editor_find_next(E, E->last_search, -1); break;
        case 127: editor_delete_char(E); break;
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
                        if (read(STDIN_FILENO, &seq[3], 1) != 1) break; if (read(STDIN_FILENO, &seq[4], 1) != 1) break;
                        if (seq[3] == '2') { if (!E->selecting) { E->selecting = true; E->sel_cx = E->cx; E->sel_cy = E->cy; } editor_move_cursor(E, seq[4]); }
                    } else if (seq[2] == '~' && seq[1] == '3') editor_del_char(E);
                } else { E->selecting = false; editor_move_cursor(E, seq[1]); }
            }
            break;
        }
        default: if (!iscntrl(c)) { E->selecting = false; editor_insert_char(E, c); } break;
    }
}
