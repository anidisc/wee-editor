#ifndef EDITOR_H
#define EDITOR_H

#include "piecetable.h"
#include "line_index.h"
#include "viewport.h"
#include "terminal.h"
#include "undo.h"
#include "highlight.h"

#define EDITOR_VERSION "2.17.MM"
#define MAX_BUFFERS 32

typedef enum {
    END_LF,     // Unix/macOS (\n)
    END_CRLF    // Windows (\r\n)
} LineEnding;

typedef enum {
    ENC_UTF8,
    END_LATIN1
} Encoding;

typedef struct {
    int cx, cy;      // Logical byte offset in line, logical row index
    int rx;          // Calculated visual column
    int rowoff;      // Scroll row offset
    int coloff;      // Scroll column offset
    char *filename;
    char *last_search;
    int last_match_off;
    int search_match_len;
    int search_dir;
    
    int sel_cx, sel_cy; // Selection start point
    bool selecting;
    char *clipboard;

    bool show_line_numbers;
    bool wrap_enabled;
    int tab_size;
    bool dirty;
    int dirty_count; 
    
    LineEnding line_ending;
    Encoding encoding;

    EditorSyntax *syntax;
    PieceTable *pt;
    LineIndex *li;
    Viewport *vp;
    UndoStack *undo_stack;
    Terminal terminal;
} Editor;

typedef struct {
    Editor *buffers[MAX_BUFFERS];
    int count;
    int active_idx;
} EditorManager;

// Editor Manager Functions
void em_init(EditorManager *em);
void em_add_buffer(EditorManager *em, const char *filename);
void em_close_current(EditorManager *em);
void em_next(EditorManager *em);
void em_prev(EditorManager *em);
Editor *em_get_active(EditorManager *em);

// Standard Editor Functions
void editor_init(Editor *E);
void editor_new_file(Editor *E);
void editor_load(Editor *E, const char *filename);
void editor_refresh_screen(EditorManager *em);
void editor_resize(EditorManager *em);
void editor_process_keypress(EditorManager *em);

char *editor_prompt(Editor *E, char *prompt, void (*callback)(Editor *, char *, int));
bool editor_confirm(Editor *E, char *prompt);
void editor_save(Editor *E);
void editor_save_as(Editor *E);
void editor_find(EditorManager *em);
void editor_find_next(Editor *E, const char *query, int dir);
void editor_replace(EditorManager *em);
void editor_goto_line(Editor *E);
void editor_fuzzy_finder(EditorManager *em);
void editor_open_browser(Editor *E);
void editor_undo(Editor *E);
void editor_redo(Editor *E);
void editor_toggle_comment(Editor *E);
void editor_indent_selection(Editor *E, int dir);
void editor_delete_selection(Editor *E);
void editor_delete_line(Editor *E);

#endif
