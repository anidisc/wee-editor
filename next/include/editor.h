#ifndef EDITOR_H
#define EDITOR_H

#include "piecetable.h"
#include "line_index.h"
#include "viewport.h"
#include "terminal.h"
#include "undo.h"
#include "highlight.h"

#define EDITOR_VERSION "2.6.0"

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
    int tab_size;
    bool dirty;
    
    EditorSyntax *syntax;
    PieceTable *pt;
    LineIndex *li;
    Viewport *vp;
    UndoStack *undo_stack;
    Terminal terminal;
} Editor;

void editor_init(Editor *E);
void editor_load(Editor *E, const char *filename);
void editor_refresh_screen(Editor *E);
void editor_resize(Editor *E);
void editor_process_keypress(Editor *E);

char *editor_prompt(Editor *E, char *prompt, void (*callback)(Editor *, char *, int));
bool editor_confirm(Editor *E, char *prompt);
void editor_save(Editor *E);
void editor_save_as(Editor *E);
void editor_find(Editor *E);
void editor_replace(Editor *E);
void editor_open_browser(Editor *E);
void editor_undo(Editor *E);
void editor_redo(Editor *E);
void editor_toggle_comment(Editor *E);
void editor_indent_selection(Editor *E, int dir);
void editor_delete_selection(Editor *E);

#endif
