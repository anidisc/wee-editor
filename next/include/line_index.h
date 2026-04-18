#ifndef LINE_INDEX_H
#define LINE_INDEX_H

#include <stddef.h>

typedef struct {
    size_t *offsets;
    size_t count;
    size_t capacity;
} LineIndex;

LineIndex* li_create();
void li_destroy(LineIndex *li);

// Full rebuild from piece table text
void li_rebuild(LineIndex *li, const char *full_text, size_t len);

// Utilities
size_t li_get_offset(LineIndex *li, int line);
int li_get_line_count(LineIndex *li);

#endif
