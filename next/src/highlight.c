#include "highlight.h"
#include <ctype.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>

/* Database dei Linguaggi */

static char *C_extensions[] = { ".c", ".h", ".cpp", ".hpp", NULL };
static char *C_keywords[] = {
    "switch", "if", "while", "for", "break", "continue", "return", "else",
    "struct", "union", "typedef", "static", "enum", "class", "case",
    "int|", "long|", "double|", "float|", "char|", "unsigned|", "signed|",
    "void|", "bool|", "size_t|", "NULL|", NULL
};

static char *Py_extensions[] = { ".py", NULL };
static char *Py_keywords[] = {
    "and", "as", "assert", "break", "class", "continue", "def", "del", "elif",
    "else", "except", "finally", "for", "from", "global", "if", "import",
    "in", "is", "lambda", "nonlocal", "not", "or", "pass", "raise", "return",
    "try", "while", "with", "yield",
    "False|", "None|", "True|", "self|", NULL
};

static char *JS_extensions[] = { ".js", ".ts", NULL };
static char *JS_keywords[] = {
    "break", "case", "catch", "class", "const", "continue", "debugger",
    "default", "delete", "do", "else", "export", "extends", "finally",
    "for", "function", "if", "import", "in", "instanceof", "new", "return",
    "super", "switch", "this", "throw", "try", "typeof", "var", "void",
    "while", "with", "yield", "let", "static", "enum",
    "true|", "false|", "null|", "undefined|", NULL
};

static char *Sh_extensions[] = { ".sh", ".bash", NULL };
static char *Sh_keywords[] = {
    "if", "then", "else", "elif", "fi", "case", "esac", "for", "select",
    "while", "until", "do", "done", "in", "function", "time",
    "alias", "bg", "bind", "break", "builtin", "caller", "cd", "command",
    "local|", "export|", "source|", NULL
};

static EditorSyntax HLDB[] = {
    {
        "c", C_extensions, C_keywords, "//", "/*", "*/",
        HL_HIGHLIGHT_NUMBERS | HL_HIGHLIGHT_STRINGS
    },
    {
        "python", Py_extensions, Py_keywords, "#", "'''", "'''",
        HL_HIGHLIGHT_NUMBERS | HL_HIGHLIGHT_STRINGS
    },
    {
        "javascript", JS_extensions, JS_keywords, "//", "/*", "*/",
        HL_HIGHLIGHT_NUMBERS | HL_HIGHLIGHT_STRINGS
    },
    {
        "shell", Sh_extensions, Sh_keywords, "#", NULL, NULL,
        HL_HIGHLIGHT_NUMBERS | HL_HIGHLIGHT_STRINGS
    }
};

#define HLDB_ENTRIES (sizeof(HLDB) / sizeof(HLDB[0]))

static bool is_separator(int c) {
    return isspace(c) || c == '\0' || strchr(",.()+-/*=~%<>[];", c) != NULL;
}

void hl_apply(const char *text, int len, unsigned char *hl_buffer, EditorSyntax *syntax) {
    if (!syntax) {
        memset(hl_buffer, HL_NORMAL, len);
        return;
    }

    int i = 0;
    int in_string = 0;
    bool prev_sep = true;
    char *scs = syntax->singleline_comment_start;
    int scs_len = scs ? strlen(scs) : 0;

    while (i < len) {
        char c = text[i];
        unsigned char prev_hl = (i > 0) ? hl_buffer[i-1] : HL_NORMAL;

        // Commenti riga singola
        if (!in_string && scs_len && i + scs_len <= len) {
            if (strncmp(&text[i], scs, scs_len) == 0) {
                memset(&hl_buffer[i], HL_COMMENT, len - i);
                break;
            }
        }

        // Stringhe
        if (in_string) {
            hl_buffer[i] = HL_STRING;
            if (c == '\\' && i + 1 < len) {
                hl_buffer[i+1] = HL_STRING;
                i += 2; continue;
            }
            if (c == in_string) in_string = 0;
            i++; prev_sep = true; continue;
        } else if (c == '"' || c == '\'') {
            in_string = c;
            hl_buffer[i] = HL_STRING;
            i++; continue;
        }

        // Numeri
        if ((isdigit(c) && (prev_sep || prev_hl == HL_NUMBER)) ||
            (c == '.' && prev_hl == HL_NUMBER)) {
            hl_buffer[i] = HL_NUMBER;
            i++; prev_sep = false; continue;
        }

        // Keywords
        if (prev_sep) {
            int k;
            bool found = false;
            for (k = 0; syntax->keywords[k]; k++) {
                char *kw = syntax->keywords[k];
                int kwlen = strlen(kw);
                bool kw2 = kw[kwlen - 1] == '|';
                if (kw2) kwlen--;

                if (i + kwlen <= len && strncmp(&text[i], kw, kwlen) == 0 &&
                    (i + kwlen == len || is_separator(text[i + kwlen]))) {
                    memset(&hl_buffer[i], kw2 ? HL_KEYWORD2 : HL_KEYWORD1, kwlen);
                    i += kwlen;
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

EditorSyntax* hl_get_syntax(const char *filename) {
    if (!filename) return NULL;
    char *ext = strrchr(filename, '.');
    for (unsigned int j = 0; j < HLDB_ENTRIES; j++) {
        EditorSyntax *s = &HLDB[j];
        for (int i = 0; s->filematch[i]; i++) {
            if (ext && strcmp(ext, s->filematch[i]) == 0) return s;
        }
    }
    return NULL;
}

const char* hl_to_ansi(unsigned char hl_type) {
    switch (hl_type) {
        case HL_NUMBER: return "\x1b[31m";
        case HL_STRING: return "\x1b[32m";
        case HL_KEYWORD1: return "\x1b[33m"; // Keywords primarie (giallo)
        case HL_KEYWORD2: return "\x1b[36m"; // Tipi/Valori (ciano)
        case HL_COMMENT: return "\x1b[90m";  // Grigio
        case HL_MATCH: return "\x1b[43;30m";
        case HL_SELECT: return "\x1b[43;30m";
        default: return "\x1b[39;49m";
    }
}
