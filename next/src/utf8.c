#define _XOPEN_SOURCE
#include "utf8.h"
#include <wchar.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>

int utf8_char_len(const char *s) {
    if (!s || *s == '\0') return 0;
    unsigned char c = (unsigned char)*s;
    if (c < 0x80) return 1;
    if ((c & 0xe0) == 0xc0) return 2;
    if ((c & 0xf0) == 0xe0) return 3;
    if ((c & 0xf8) == 0xf0) return 4;
    return 1;
}

int utf8_char_width(const char *s) {
    if (!s || *s == '\0') return 0;
    if (*s == '\t') return 4; 
    
    wchar_t wc;
    mbstate_t state;
    memset(&state, 0, sizeof(state));
    size_t len = mbrtowc(&wc, s, 4, &state);
    if (len == (size_t)-1 || len == (size_t)-2) return 1;
    
    int w = wcwidth(wc);
    return (w < 0) ? 1 : w;
}

bool utf8_is_valid(const char *s, size_t len) {
    const unsigned char *u = (const unsigned char *)s;
    size_t i = 0;
    while (i < len) {
        if (u[i] < 0x80) {
            i++;
        } else if ((u[i] & 0xe0) == 0xc0) {
            if (i + 1 >= len || (u[i+1] & 0xc0) != 0x80) return false;
            i += 2;
        } else if ((u[i] & 0xf0) == 0xe0) {
            if (i + 2 >= len || (u[i+1] & 0xc0) != 0x80 || (u[i+2] & 0xc0) != 0x80) return false;
            i += 3;
        } else if ((u[i] & 0xf8) == 0xf0) {
            if (i + 3 >= len || (u[i+1] & 0xc0) != 0x80 || (u[i+2] & 0xc0) != 0x80 || (u[i+3] & 0xc0) != 0x80) return false;
            i += 4;
        } else {
            return false;
        }
    }
    return true;
}
