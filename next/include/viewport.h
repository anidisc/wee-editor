#ifndef VIEWPORT_H
#define VIEWPORT_H

#include <stddef.h>
#include <stdbool.h>
#include "piecetable.h"
#include "line_index.h"

typedef struct {
    char *chars;
    int len;
    unsigned char *hl;
    int *cx_to_rx; // Maps byte offset to visual column
    int visual_len;
    int logical_row; // The actual row in the file
    int byte_offset; // Start offset within the logical row
    bool is_wrapped; // True if this visual line is a continuation
} ViewLine;

typedef struct {
    ViewLine *lines;
    int rows;
    int rowoff;
} Viewport;

Viewport* vp_create(int rows);
void vp_destroy(Viewport *vp);

// Sync viewport lines with piece table. 
// folds is passed as void* to avoid circular dependency with editor.h
void vp_sync(Viewport *vp, PieceTable *pt, LineIndex *li, int rowoff, int wrap_width, void *folds, int fold_count);

#endif
