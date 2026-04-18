#ifndef FILE_BROWSER_H
#define FILE_BROWSER_H

#include "editor.h"

// Returns the selected filename or NULL if cancelled. 
// Handles directory navigation internally.
char* file_browser_open(Editor *E);

#endif
