#include "highlight.h"
#include <ctype.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>

/* --- DATABASE DEI LINGUAGGI (Enterprise Edition) --- */

static char *C_extensions[] = { ".c", ".h", NULL };
static char *C_keywords[] = {
    "switch", "if", "while", "for", "break", "continue", "return", "else", "struct", "union", "typedef", "static", "enum", "class", "case", "volatile", "extern", "const", "inline", "register", "auto", "signed", "short", "long", "unsigned", "void", "char", "int", "float", "double", "size_t", "ptrdiff_t", "FILE", "NULL",
    "printf|", "scanf|", "sprintf|", "sscanf|", "fprintf|", "fscanf|", "malloc|", "free|", "calloc|", "realloc|", "memcpy|", "memmove|", "memset|", "memcmp|", "memchr|", "strlen|", "strcpy|", "strncpy|", "strcat|", "strncat|", "strcmp|", "strncmp|", "strchr|", "strrchr|", "strstr|", "strerror|", "perror|", "fopen|", "fclose|", "fread|", "fwrite|", "fgets|", "fputs|", "fgetc|", "fputc|", "fseek|", "ftell|", "rewind|", "fflush|", "feof|", "ferror|", "clearerr|", "exit|", "atexit|", "abort|", "system|", "getenv|", "setenv|", "unsetenv|", "assert|", "sizeof|", "offsetof|", "va_start|", "va_arg|", "va_end|", "va_list|", "true|", "false|", NULL
};

static char *C_types[] = {
    "int", "long", "double", "float", "char", "unsigned", "signed", "void", "bool", "size_t", "int8_t", "uint8_t", "int16_t", "uint16_t", "int32_t", "uint32_t", "int64_t", "uint64_t", "ssize_t", "off_t", "mode_t", "uid_t", "gid_t", "pid_t", "time_t", "clock_t", "struct stat", "struct tm", "struct dirent", "struct passwd", "struct group", "struct utsname", "struct winsize", NULL
};

static char *CPP_extensions[] = { ".cpp", ".hpp", ".cc", ".cxx", ".c++", ".hh", NULL };
static char *CPP_keywords[] = {
    "class", "public", "private", "protected", "template", "typename", "new", "delete", "this", "namespace", "using", "try", "catch", "throw", "virtual", "override", "final", "friend", "operator", "explicit", "constexpr", "static_cast|", "dynamic_cast|", "const_cast|", "reinterpret_cast|", "auto|", "decltype|", "nullptr|", "noexcept|",
    "std|", "string|", "vector|", "map|", "set|", "list|", "deque|", "array|", "pair|", "tuple|", "shared_ptr|", "unique_ptr|", "weak_ptr|", "make_shared|", "make_unique|", "cout|", "cin|", "cerr|", "endl|", "stdin|", "stdout|", "stderr|", NULL
};

static char *Rust_extensions[] = { ".rs", NULL };
static char *Rust_keywords[] = {
    "fn", "let", "mut", "match", "use", "mod", "pub", "crate", "struct", "enum", "impl", "trait", "type", "where", "move", "loop", "return", "if", "else", "while", "for", "in", "break", "continue", "as", "async", "await", "unsafe",
    "i8|", "i16|", "i32|", "i64|", "u8|", "u16|", "u32|", "u64|", "f32|", "f64|", "str|", "String|", "Option|", "Result|", "Self|", "self|", "true|", "false|", NULL
};

static char *Py_extensions[] = { ".py", ".pyw", ".pyx", NULL };
static char *Py_keywords[] = {
    "def", "class", "if", "else", "elif", "for", "while", "return", "import", "from", "as", "try", "except", "finally", "with", "yield", "lambda", "global", "nonlocal", "pass", "assert", "del", "and", "or", "not", "is", "in", "raise", "break", "continue", "True|", "False|", "None|", "self|", "print|", "len|", "range|", "enumerate|", "zip|", "map|", "filter|", "reduce|", "sorted|", "reversed|", "any|", "all|", "sum|", "min|", "max|", "abs|", "open|", "input|", "isinstance|", "issubclass|", "hasattr|", "getattr|", "setattr|", "delattr|", "callable|", "type|", "id|", "repr|", "str|", "int|", "float|", "bool|", "list|", "dict|", "tuple|", "set|", "frozenset|", "bytes|", "bytearray|", "complex|",
    "__init__|", "__name__|", "__main__|", "__file__|", "__line__|", "__dict__|", "__class__|", "__doc__|", "__enter__|", "__exit__|", "__iter__|", "__next__|", "__len__|", "__getitem__|", "__setitem__|", "__delitem__|", "__contains__|", "__str__|", "__repr__|", "__eq__|", "__ne__|", "__lt__|", "__le__|", "__gt__|", "__ge__|", "__add__|", "__sub__|", "__mul__|", "__div__|", "__mod__|", "__and__|", "__or__|", "__xor__|", "__lshift__|", "__rshift__|", "__pow__|", "__neg__|", "__pos__|", "__abs__|", "__invert__|", NULL
};

static char *Java_extensions[] = { ".java", NULL };
static char *Java_keywords[] = {
    "public", "private", "protected", "static", "final", "class", "interface", "extends", "implements", "package", "import", "new", "return", "if", "else", "for", "while", "try", "catch", "finally", "throw", "throws", "synchronized", "abstract", "native", "transient", "instanceof", "volatile", "strictfp", "assert", "enum", "goto", "const", "super", "this", "do", "switch", "case", "break", "continue", "default", "byte|", "short|", "int|", "long|", "float|", "double|", "char|", "boolean|", "void|",
    "String|", "Integer|", "Long|", "Double|", "Float|", "Boolean|", "Character|", "Byte|", "Short|", "Object|", "Class|", "System|", "Math|", "Arrays|", "Collections|", "List|", "ArrayList|", "LinkedList|", "Map|", "HashMap|", "TreeMap|", "Set|", "HashSet|", "TreeSet|", "Queue|", "Stack|", "Vector|", "Iterator|", "ListIterator|", "Scanner|", "BufferedReader|", "FileReader|", "FileWriter|", "PrintWriter|", "InputStream|", "OutputStream|", "FileInputStream|", "FileOutputStream|", "BufferedInputStream|", "BufferedOutputStream|", "DataInputStream|", "DataOutputStream|", "ObjectInputStream|", "ObjectOutputStream|", "Thread|", "Runnable|", "Callable|", "Future|", "ExecutorService|", "Executors|", "StringBuilder|", "StringBuffer|", "Pattern|", "Matcher|", "Regex|", "Integer|", "Long|", "Double|", "Float|", "Boolean|", "Character|", "Number|", NULL
};

static char *Go_extensions[] = { ".go", NULL };
static char *Go_keywords[] = {
    "func", "var", "const", "type", "struct", "interface", "package", "import", "return", "if", "else", "switch", "case", "for", "range", "go", "chan", "select", "defer", "panic", "recover", "map", "default", "fallthrough", "goto", "break", "continue", "fallthrough|",
    "int|", "int8|", "int16|", "int32|", "int64|", "uint|", "uint8|", "uint16|", "uint32|", "uint64|", "uintptr|", "float32|", "float64|", "complex64|", "complex128|", "string|", "bool|", "byte|", "rune|", "error|", "nil|", "iota|", "true|", "false|", "append|", "cap|", "close|", "complex|", "copy|", "delete|", "imag|", "len|", "make|", "new|", "panic|", "print|", "println|", "real|", "recover|", "slice|", "sort|", "fmt|", "os|", "io|", "ioutil|", "bufio|", "path|", "filepath|", "flag|", "log|", "encoding|", "json|", "xml|", "html|", "text|", "template|", "net|", "http|", "url|", "reflect|", "sync|", "atomic|", "time|", "timer|", "ticker|", NULL
};

static char *JS_extensions[] = { ".js", ".ts", ".jsx", ".tsx", ".mjs", ".cjs", NULL };
static char *JS_keywords[] = {
    "function", "const", "let", "var", "if", "else", "for", "while", "return", "import", "export", "default", "class", "extends", "try", "catch", "finally", "throw", "async", "await", "new", "this", "super", "switch", "case", "break", "continue", "typeof", "instanceof", "in", "of", "delete", "static", "get", "set", "extends|",
    "true|", "false|", "null|", "undefined|", "NaN|", "Infinity|", "Object|", "Array|", "String|", "Number|", "Boolean|", "Function|", "Symbol|", "BigInt|", "Map|", "Set|", "WeakMap|", "WeakSet|", "Promise|", "console|", "window|", "document|", "JSON|", "Math|", "Date|", "RegExp|", "Error|", "Event|", "fetch|", "URL|", "localStorage|", "sessionStorage|", "alert|", "confirm|", "prompt|", "setTimeout|", "setInterval|", "clearTimeout|", "clearInterval|", "parseInt|", "parseFloat|", "isNaN|", "isFinite|", "encodeURI|", "decodeURI|", "encodeURIComponent|", "decodeURIComponent|", NULL
};

static char *Sh_extensions[] = { ".sh", ".bash", ".zsh", ".bashrc", ".zshrc", ".profile", NULL };
static char *Sh_keywords[] = {
    "if", "then", "else", "elif", "fi", "case", "esac", "for", "select", "while", "until", "do", "done", "in", "function", "time", "alias", "bg", "bind", "break", "builtin", "caller", "cd", "command", "compgen", "complete", "continue", "declare", "dirs", "disown", "echo", "enable", "eval", "exec", "exit", "export", "fc", "fg", "getopts", "hash", "help", "history", "jobs", "kill", "let", "local", "logout", "popd", "printf", "pushd", "pwd", "read", "readonly", "return", "set", "shift", "shopt", "source", "suspend", "test", "times", "trap", "type", "typeset", "ulimit", "umask", "unalias", "unset", "wait",
    "cat|", "grep|", "sed|", "awk|", "cut|", "sort|", "uniq|", "wc|", "head|", "tail|", "find|", "xargs|", "tr|", "tee|", "chmod|", "chown|", "chgrp|", "mkdir|", "rmdir|", "rm|", "cp|", "mv|", "ln|", "touch|", "tar|", "gzip|", "gunzip|", "zip|", "unzip|", "curl|", "wget|", "ssh|", "scp|", "rsync|", "git|", "svn|", "make|", "cmake|", "gcc|", "g++|", "clang|", "go|", "rustc|", "javac|", "java|", "python|", "python3|", "pip|", "npm|", "yarn|", "node|", "npm|", "cargo|", "docker|", "kubectl|", "systemctl|", "journalctl|", "ps|", "top|", "htop|", "kill|", "killall|", "pkill|", "pidof|", "pgrep|", "jobs|", "fg|", "bg|", "nohup|", "screen|", "tmux|", "watch|", "date|", "cal|", "df|", "du|", "free|", "uptime|", "whoami|", "id|", "groups|", "passwd|", "su|", "sudo|", "visudo|", "useradd|", "usermod|", "userdel|", "groupadd|", "groupdel|", "ln|", "readlink|", "dirname|", "basename|", "path|", "realpath|", "mktemp|", "mkfifo|", "stat|", "file|", "which|", "whereis|", "type|", "hash|", "compgen|", "complete|", "export|", "local|", "source|", "return|", "set|", "unset|", "shopt|", "trap|", "true|", "false|", NULL
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
    { "c", C_extensions, C_keywords, C_types, "//", "/*", "*/", HL_HIGHLIGHT_NUMBERS | HL_HIGHLIGHT_STRINGS },
    { "cpp", CPP_extensions, CPP_keywords, NULL, "//", "/*", "*/", HL_HIGHLIGHT_NUMBERS | HL_HIGHLIGHT_STRINGS },
    { "rust", Rust_extensions, Rust_keywords, NULL, "//", "/*", "*/", HL_HIGHLIGHT_NUMBERS | HL_HIGHLIGHT_STRINGS },
    { "python", Py_extensions, Py_keywords, NULL, "#", "'''", "'''", HL_HIGHLIGHT_NUMBERS | HL_HIGHLIGHT_STRINGS },
    { "java", Java_extensions, Java_keywords, NULL, "//", "/*", "*/", HL_HIGHLIGHT_NUMBERS | HL_HIGHLIGHT_STRINGS },
    { "go", Go_extensions, Go_keywords, NULL, "//", "/*", "*/", HL_HIGHLIGHT_NUMBERS | HL_HIGHLIGHT_STRINGS },
    { "javascript", JS_extensions, JS_keywords, NULL, "//", "/*", "*/", HL_HIGHLIGHT_NUMBERS | HL_HIGHLIGHT_STRINGS },
    { "shell", Sh_extensions, Sh_keywords, NULL, "#", NULL, NULL, HL_HIGHLIGHT_NUMBERS | HL_HIGHLIGHT_STRINGS },
    { "html", HTML_extensions, HTML_keywords, NULL, "<!--", "<!--", "-->", HL_HIGHLIGHT_NUMBERS | HL_HIGHLIGHT_STRINGS },
    { "css", CSS_extensions, CSS_keywords, NULL, "/*", "/*", "*/", HL_HIGHLIGHT_NUMBERS | HL_HIGHLIGHT_STRINGS }
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
    char *mcs = syntax->multiline_comment_start;
    int mcs_len = mcs ? strlen(mcs) : 0;
    int in_mlcomment = 0;

    while (i < len) {
        char c = text[i];
        unsigned char prev_hl = (i > 0) ? hl_buffer[i-1] : HL_NORMAL;

        if (mcs_len && !in_string && i + mcs_len <= len) {
            if (!in_mlcomment && strncmp(&text[i], mcs, mcs_len) == 0) {
                in_mlcomment = 1;
                memset(&hl_buffer[i], HL_MLCOMMENT, mcs_len);
                i += mcs_len;
                continue;
            } else if (in_mlcomment) {
                char *mce = syntax->multiline_comment_end;
                int mce_len = mce ? strlen(mce) : 0;
                if (mce_len && i + mce_len <= len && strncmp(&text[i], mce, mce_len) == 0) {
                    memset(&hl_buffer[i], HL_MLCOMMENT, mce_len);
                    i += mce_len;
                    in_mlcomment = 0;
                    continue;
                }
                hl_buffer[i] = HL_MLCOMMENT;
                i++;
                continue;
            }
        }

        if (in_mlcomment) {
            hl_buffer[i] = HL_MLCOMMENT;
            i++;
            continue;
        }

        if (!in_string && scs_len && i + scs_len <= len) {
            if (strncmp(&text[i], scs, scs_len) == 0) {
                memset(&hl_buffer[i], HL_COMMENT, len - i);
                break;
            }
        }

        if (c == '#' && prev_sep && i + 1 < len && text[i + 1] != '#') {
            int j = i + 1;
            while (j < len && text[j] == ' ') j++;
            int hash_len = j - i;
            if (hash_len > 0 && (strncmp(&text[i + 1], "include", 6) == 0 || strncmp(&text[i + 1], "define", 6) == 0 || strncmp(&text[i + 1], "ifdef", 5) == 0 || strncmp(&text[i + 1], "ifndef", 5) == 0 || strncmp(&text[i + 1], "endif", 5) == 0 || strncmp(&text[i + 1], "pragma", 6) == 0 || strncmp(&text[i + 1], "error", 5) == 0 || strncmp(&text[i + 1], "warning", 6) == 0 || strncmp(&text[i + 1], "undef", 5) == 0 || strncmp(&text[i + 1], "if", 2) == 0 || strncmp(&text[i + 1], "else", 4) == 0 || strncmp(&text[i + 1], "elif", 4) == 0)) {
                while (i < len && text[i] != '\n' && text[i] != '\r') {
                    hl_buffer[i] = HL_KEYWORD1;
                    i++;
                }
                if (i < len) hl_buffer[i] = HL_NORMAL;
                continue;
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
            if (syntax->types) {
                for (int k = 0; syntax->types[k]; k++) {
                    char *tp = syntax->types[k];
                    int tplen = strlen(tp);
                    if (i + tplen <= len && strncmp(&text[i], tp, tplen) == 0 &&
                        (i + tplen == len || is_separator(text[i + tplen]))) {
                        memset(&hl_buffer[i], HL_TYPE, tplen);
                        i += tplen; prev_sep = false; continue;
                    }
                }
            }

            for (int k = 0; syntax->keywords[k]; k++) {
                char *kw = syntax->keywords[k];
                int kwlen = strlen(kw);
                bool kw2 = kw[kwlen - 1] == '|';
                if (kw2) kwlen--;

                if (i + kwlen <= len && strncmp(&text[i], kw, kwlen) == 0 &&
                    (i + kwlen == len || is_separator(text[i + kwlen]))) {
                    memset(&hl_buffer[i], kw2 ? HL_KEYWORD2 : HL_KEYWORD1, kwlen);
                    i += kwlen; prev_sep = false; break;
                }
            }
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
        case HL_KEYWORD1: return "\x1b[33;1m";  // Yellow + Bold
        case HL_KEYWORD2: return "\x1b[35;1m";  // Magenta + Bold
        case HL_TYPE: return "\x1b[36;1m";       // Cyan + Bold
        case HL_COMMENT: return "\x1b[90;3m";   // Grey + Italic
        case HL_MLCOMMENT: return "\x1b[90;3m";   // Grey + Italic
        case HL_MATCH: return "\x1b[43;30m";
        case HL_SELECT: return "\x1b[43;30m";
        default: return "\x1b[39;49m";
    }
}
