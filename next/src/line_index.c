#include "line_index.h"
#include <stdlib.h>
#include <string.h>

LineIndex* li_create() {
    LineIndex *li = calloc(1, sizeof(LineIndex));
    li->capacity = 128;
    li->offsets = malloc(sizeof(size_t) * li->capacity);
    li->offsets[0] = 0;
    li->count = 1;
    return li;
}

void li_destroy(LineIndex *li) {
    if (!li) return;
    free(li->offsets);
    free(li);
}

void li_rebuild(LineIndex *li, const char *text, size_t len) {
    li->count = 0;
    if (li->count >= li->capacity) { // Reset count and ensure initial capacity
        li->capacity = 128;
        li->offsets = realloc(li->offsets, sizeof(size_t) * li->capacity);
    }
    
    li->offsets[li->count++] = 0;

    for (size_t i = 0; i < len; i++) {
        if (text[i] == '\n') {
            if (li->count >= li->capacity) {
                li->capacity *= 2;
                li->offsets = realloc(li->offsets, sizeof(size_t) * li->capacity);
            }
            li->offsets[li->count++] = i + 1;
        }
    }
}

size_t li_get_offset(LineIndex *li, int line) {
    if (line < 0) return 0;
    if (line >= (int)li->count) return li->offsets[li->count-1];
    return li->offsets[line];
}

int li_get_line_count(LineIndex *li) {
    return (int)li->count;
}
