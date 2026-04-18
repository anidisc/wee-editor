#ifndef HIGHLIGHT_H
#define HIGHLIGHT_H

#include <stddef.h>

typedef enum {
    HL_NORMAL = 0,
    HL_NUMBER,
    HL_STRING,
    HL_KEYWORD,
    HL_COMMENT,
    HL_MATCH,
    HL_SELECT
} HighlightColor;

void hl_apply(const char *text, int len, unsigned char *hl_buffer);
const char* hl_to_ansi(unsigned char hl_type);

#endif
