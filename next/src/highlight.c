#include "highlight.h"
#include <ctype.h>
#include <string.h>
#include <stdbool.h>

static const char *C_keywords[] = {
    "switch", "if", "while", "for", "break", "continue", "return", "else",
    "struct", "union", "typedef", "static", "enum", "class", "case",
    "int", "long", "double", "float", "char", "unsigned", "signed",
    "void", "bool", "size_t", "NULL", NULL
};

static bool is_separator(int c) {
    return isspace(c) || c == '\0' || strchr(",.()+-/*=~%<>[];", c) != NULL;
}

void hl_apply(const char *text, int len, unsigned char *hl_buffer) {
    int i = 0;
    int in_string = 0;
    bool prev_sep = true;

    while (i < len) {
        char c = text[i];
        unsigned char prev_hl = (i > 0) ? hl_buffer[i-1] : HL_NORMAL;

        if (in_string) {
            hl_buffer[i] = HL_STRING;
            if (c == in_string) in_string = 0;
            i++;
            prev_sep = true;
            continue;
        }

        if (c == '"' || c == '\'') {
            in_string = c;
            hl_buffer[i] = HL_STRING;
            i++;
            continue;
        }

        if (isdigit(c) && (prev_sep || prev_hl == HL_NUMBER)) {
            hl_buffer[i] = HL_NUMBER;
            i++;
            prev_sep = false;
            continue;
        }

        if (prev_sep) {
            int k;
            bool found = false;
            for (k = 0; C_keywords[k]; k++) {
                int klen = strlen(C_keywords[k]);
                if (i + klen <= len && strncmp(&text[i], C_keywords[k], klen) == 0 && 
                    (i + klen == len || is_separator(text[i + klen]))) {
                    memset(&hl_buffer[i], HL_KEYWORD, klen);
                    i += klen;
                    prev_sep = false;
                    found = true;
                    break;
                }
            }
            if (found) continue;
        }

        hl_buffer[i] = HL_NORMAL;
        prev_sep = is_separator(c);
        i++;
    }
}

const char* hl_to_ansi(unsigned char hl_type) {
    switch (hl_type) {
        case HL_NUMBER: return "\x1b[31m"; // Red
        case HL_STRING: return "\x1b[32m"; // Green
        case HL_KEYWORD: return "\x1b[33m"; // Yellow
        case HL_MATCH: return "\x1b[43;30m"; // Search Match
        case HL_SELECT: return "\x1b[44;37m"; // Selection
        default: return "\x1b[39;49m";
    }
}
