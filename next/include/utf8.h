#ifndef UTF8_H
#define UTF8_H

#include <stddef.h>
#include <stdbool.h>

int utf8_char_len(const char *s);
int utf8_char_width(const char *s);
bool utf8_is_valid(const char *s, size_t len);

#endif
