#ifndef UTF8_H
#define UTF8_H

#include <stddef.h>

int utf8_char_len(const char *s);
int utf8_char_width(const char *s);

#endif
