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
} ViewLine;

typedef struct {
    ViewLine *lines;
    int rows;
    int rowoff;
} Viewport;

Viewport* vp_create(int rows);
void vp_destroy(Viewport *vp);

// Sync viewport lines with piece table starting from rowoff
void vp_sync(Viewport *vp, PieceTable *pt, LineIndex *li, int rowoff);

#endif
