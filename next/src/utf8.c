#define _XOPEN_SOURCE
#include "utf8.h"
#include <wchar.h>
#include <string.h>
#include <stdlib.h>

int utf8_char_len(const char *s) {
    if (!s || *s == '\0') return 0;
    unsigned char c = (unsigned char)*s;
    if (c < 0x80) return 1;
    if ((c & 0xe0) == 0xc0) return 2;
    if ((c & 0xf0) == 0xe0) return 3;
    if ((c & 0xf0) == 0xf0) return 4;
    return 1;
}

int utf8_char_width(const char *s) {
    if (!s || *s == '\0') return 0;
    if (*s == '\t') return 4; // Configurable later
    
    wchar_t wc;
    mbstate_t state;
    memset(&state, 0, sizeof(state));
    size_t len = mbrtowc(&wc, s, 4, &state);
    if (len == (size_t)-1 || len == (size_t)-2) return 1;
    
    int w = wcwidth(wc);
    return (w < 0) ? 1 : w;
}
