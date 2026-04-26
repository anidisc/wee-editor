#ifndef HIGHLIGHT_H
#define HIGHLIGHT_H

typedef enum {
    HL_NORMAL = 0,
    HL_NUMBER,
    HL_STRING,
    HL_KEYWORD1,
    HL_KEYWORD2,
    HL_TYPE,
    HL_COMMENT,
    HL_MLCOMMENT,
    HL_MATCH,
    HL_SELECT
} HighlightColor;

#define HL_HIGHLIGHT_NUMBERS (1<<0)
#define HL_HIGHLIGHT_STRINGS (1<<1)

typedef struct {
    char *filetype;
    char **filematch;
    char **keywords;
    char **types;
    char *singleline_comment_start;
    char *multiline_comment_start;
    char *multiline_comment_end;
    int flags;
} EditorSyntax;

void hl_apply(const char *text, int len, unsigned char *hl_buffer, EditorSyntax *syntax);
const char* hl_to_ansi(unsigned char hl_type);
EditorSyntax* hl_get_syntax(const char *filename);
int hl_get_syntax_count(void);
EditorSyntax* hl_get_syntax_by_index(int idx);

#endif
