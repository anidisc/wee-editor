#include "highlight.h"
#include <ctype.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>

/* --- DATABASE DEI LINGUAGGI (Enterprise Edition) --- */

static char *C_extensions[] = { ".c", ".h", NULL };
static char *C_keywords[] = {
    "switch", "if", "while", "for", "break", "continue", "return", "else", "struct", "union", "typedef", "static", "enum", "class", "case", "volatile", "extern", "const", "inline",
    "int|", "long|", "double|", "float|", "char|", "unsigned|", "signed|", "void|", "bool|", "size_t|", "int64_t|", "uint64_t|", "int32_t|", "uint32_t|", "NULL|", NULL
};

static char *CPP_extensions[] = { ".cpp", ".hpp", ".cc", ".cxx", NULL };
static char *CPP_keywords[] = {
    "class", "public", "private", "protected", "template", "typename", "new", "delete", "this", "namespace", "using", "try", "catch", "throw", "virtual", "override", "final", "friend", "operator", "explicit",
    "std|", "string|", "vector|", "map|", "set|", "cout|", "cin|", "endl|", "uint8_t|", "uint16_t|", "uint32_t|", "uint64_t|", NULL
};

static char *Rust_extensions[] = { ".rs", NULL };
static char *Rust_keywords[] = {
    "fn", "let", "mut", "match", "use", "mod", "pub", "crate", "struct", "enum", "impl", "trait", "type", "where", "move", "loop", "return", "if", "else", "while", "for", "in", "break", "continue", "as", "async", "await", "unsafe",
    "i8|", "i16|", "i32|", "i64|", "u8|", "u16|", "u32|", "u64|", "f32|", "f64|", "str|", "String|", "Option|", "Result|", "Self|", "self|", "true|", "false|", NULL
};

static char *Py_extensions[] = { ".py", NULL };
static char *Py_keywords[] = {
    "def", "class", "if", "else", "elif", "for", "while", "return", "import", "from", "as", "try", "except", "finally", "with", "yield", "lambda", "global", "nonlocal", "pass", "assert", "del", "and", "or", "not", "is", "in",
    "True|", "False|", "None|", "self|", "list|", "dict|", "tuple|", "set|", "int|", "str|", "float|", "print|", "range|", "len|", NULL
};

static char *Java_extensions[] = { ".java", NULL };
static char *Java_keywords[] = {
    "public", "private", "protected", "static", "final", "class", "interface", "extends", "implements", "package", "import", "new", "return", "if", "else", "for", "while", "try", "catch", "finally", "throw", "throws", "synchronized", "abstract", "native", "transient", "instanceof",
    "int|", "double|", "float|", "long|", "short|", "byte|", "char|", "boolean|", "String|", "Object|", "System|", "true|", "false|", "null|", NULL
};

static char *Go_extensions[] = { ".go", NULL };
static char *Go_keywords[] = {
    "func", "var", "const", "type", "struct", "interface", "package", "import", "return", "if", "else", "switch", "case", "for", "range", "go", "chan", "select", "defer", "panic", "recover", "map", "default", "fallthrough", "goto",
    "int|", "int8|", "int16|", "int32|", "int64|", "uint|", "uint8|", "uint16|", "uint32|", "uint64|", "float32|", "float64|", "string|", "bool|", "error|", "nil|", "iota|", "true|", "false|", NULL
};

static char *JS_extensions[] = { ".js", ".ts", ".jsx", ".tsx", ".mjs", NULL };
static char *JS_keywords[] = {
    "function", "const", "let", "var", "if", "else", "for", "while", "return", "import", "export", "default", "class", "extends", "try", "catch", "finally", "throw", "async", "await", "new", "this", "super", "switch", "case", "break", "continue", "typeof", "instanceof", "in", "of", "delete",
    "true|", "false|", "null|", "undefined|", "NaN|", "Object|", "Array|", "String|", "Number|", "console|", "window|", "document|", "JSON|", "Math|", "Promise|", NULL
};

static char *Sh_extensions[] = { ".sh", ".bash", ".zsh", NULL };
static char *Sh_keywords[] = {
    "if", "then", "else", "elif", "fi", "case", "esac", "for", "select", "while", "until", "do", "done", "in", "function", "time", "alias", "bg", "bind", "break", "builtin", "caller", "cd", "command", "compgen", "complete", "continue", "declare", "dirs", "disown", "echo", "enable", "eval", "exec", "exit", "export", "fc", "fg", "getopts", "hash", "help", "history", "jobs", "kill", "let", "local", "logout", "popd", "printf", "pushd", "pwd", "read", "readonly", "return", "set", "shift", "shopt", "source", "suspend", "test", "times", "trap", "type", "typeset", "ulimit", "umask", "unalias", "unset", "wait",
    "local|", "export|", "source|", "true|", "false|", NULL
};

static char *HTML_extensions[] = { ".html", ".htm", ".xml", ".svg", NULL };
static char *HTML_keywords[] = {
    "div", "span", "a", "p", "h1", "h2", "h3", "h4", "h5", "h6", "ul", "ol", "li", "br", "hr", "img", "form", "input", "button", "label", "select", "option", "table", "tr", "td", "th", "thead", "tbody", "head", "body", "html", "script", "style", "meta", "link", "title",
    "id|", "class|", "href|", "src|", "style|", "type|", "value|", "name|", "placeholder|", "target|", "rel|", "onclick|", "onload|", NULL
};

static char *CSS_extensions[] = { ".css", ".scss", ".sass", NULL };
static char *CSS_keywords[] = {
    "margin", "padding", "display", "position", "color", "background", "border", "font", "width", "height", "top", "right", "bottom", "left", "float", "clear", "overflow", "opacity", "visibility", "z-index", "flex", "grid", "justify", "align", "items", "text", "line", "cursor", "pointer", "none", "block", "inline", "absolute", "relative", "fixed", "sticky",
    "important|", "hover|", "active|", "focus|", "before|", "after|", "nth-child|", "root|", NULL
};

static EditorSyntax HLDB[] = {
    { "c", C_extensions, C_keywords, "//", "/*", "*/", HL_HIGHLIGHT_NUMBERS | HL_HIGHLIGHT_STRINGS },
    { "cpp", CPP_extensions, CPP_keywords, "//", "/*", "*/", HL_HIGHLIGHT_NUMBERS | HL_HIGHLIGHT_STRINGS },
    { "rust", Rust_extensions, Rust_keywords, "//", "/*", "*/", HL_HIGHLIGHT_NUMBERS | HL_HIGHLIGHT_STRINGS },
    { "python", Py_extensions, Py_keywords, "#", "'''", "'''", HL_HIGHLIGHT_NUMBERS | HL_HIGHLIGHT_STRINGS },
    { "java", Java_extensions, Java_keywords, "//", "/*", "*/", HL_HIGHLIGHT_NUMBERS | HL_HIGHLIGHT_STRINGS },
    { "go", Go_extensions, Go_keywords, "//", "/*", "*/", HL_HIGHLIGHT_NUMBERS | HL_HIGHLIGHT_STRINGS },
    { "javascript", JS_extensions, JS_keywords, "//", "/*", "*/", HL_HIGHLIGHT_NUMBERS | HL_HIGHLIGHT_STRINGS },
    { "shell", Sh_extensions, Sh_keywords, "#", NULL, NULL, HL_HIGHLIGHT_NUMBERS | HL_HIGHLIGHT_STRINGS },
    { "html", HTML_extensions, HTML_keywords, "<!--", "<!--", "-->", HL_HIGHLIGHT_NUMBERS | HL_HIGHLIGHT_STRINGS },
    { "css", CSS_extensions, CSS_keywords, "/*", "/*", "*/", HL_HIGHLIGHT_NUMBERS | HL_HIGHLIGHT_STRINGS }
};

#define HLDB_ENTRIES (sizeof(HLDB) / sizeof(HLDB[0]))

/* --- LOGICA DI APPLICAZIONE --- */

static bool is_separator(int c) {
    return isspace(c) || c == '\0' || strchr(",.()+-/*=~%<>[];:", c) != NULL;
}

void hl_apply(const char *text, int len, unsigned char *hl_buffer, EditorSyntax *syntax) {
    if (!syntax) { memset(hl_buffer, HL_NORMAL, len); return; }

    int i = 0;
    int in_string = 0;
    bool prev_sep = true;
    char *scs = syntax->singleline_comment_start;
    int scs_len = scs ? strlen(scs) : 0;

    while (i < len) {
        char c = text[i];
        unsigned char prev_hl = (i > 0) ? hl_buffer[i-1] : HL_NORMAL;

        if (!in_string && scs_len && i + scs_len <= len) {
            if (strncmp(&text[i], scs, scs_len) == 0) {
                memset(&hl_buffer[i], HL_COMMENT, len - i);
                break;
            }
        }

        if (in_string) {
            hl_buffer[i] = HL_STRING;
            if (c == '\\' && i + 1 < len) { hl_buffer[i+1] = HL_STRING; i += 2; continue; }
            if (c == in_string) in_string = 0;
            i++; prev_sep = true; continue;
        } else if (c == '"' || c == '\'') {
            in_string = c;
            hl_buffer[i] = HL_STRING;
            i++; continue;
        }

        if ((isdigit(c) && (prev_sep || prev_hl == HL_NUMBER)) || (c == '.' && prev_hl == HL_NUMBER)) {
            hl_buffer[i] = HL_NUMBER;
            i++; prev_sep = false; continue;
        }

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
                    i += kwlen; prev_sep = false; found = true; break;
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

int hl_get_syntax_count(void) {
    return HLDB_ENTRIES;
}

EditorSyntax* hl_get_syntax_by_index(int idx) {
    if (idx < 0 || idx >= HLDB_ENTRIES) return NULL;
    return &HLDB[idx];
}

const char* hl_to_ansi(unsigned char hl_type) {
    switch (hl_type) {
        case HL_NUMBER: return "\x1b[31m";
        case HL_STRING: return "\x1b[32m";
        case HL_KEYWORD1: return "\x1b[33;1m"; // Yellow + Bold
        case HL_KEYWORD2: return "\x1b[36m";   // Cyan
        case HL_COMMENT: return "\x1b[90;3m";  // Grey + Italic
        case HL_MATCH: return "\x1b[43;30m";
        case HL_SELECT: return "\x1b[43;30m";
        default: return "\x1b[39;49m";
    }
}
