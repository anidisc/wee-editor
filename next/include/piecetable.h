#ifndef PIECETABLE_H
#define PIECETABLE_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

typedef enum {
    BUFFER_ORIGINAL,
    BUFFER_ADD
} BufferType;

typedef struct {
    char *data;
    size_t length;
} Buffer;

typedef struct Piece {
    BufferType type;
    size_t start;
    size_t length;
    struct Piece *next;
    struct Piece *prev;
} Piece;

typedef struct {
    Buffer original;
    Buffer add;
    Piece *head;
    size_t total_length;
} PieceTable;

PieceTable *pt_create(const char *initial_data, size_t length);
PieceTable *pt_open(const char *filename);
void pt_destroy(PieceTable *pt);
char *pt_get_text(PieceTable *pt, size_t offset, size_t length);
void pt_insert(PieceTable *pt, size_t offset, const char *text, size_t len);
void pt_delete_fixed(PieceTable *pt, size_t offset, size_t len);
bool pt_save(PieceTable *pt, const char *filename);
bool pt_save_ext(PieceTable *pt, const char *filename, bool force_crlf);
int64_t pt_find(PieceTable *pt, const char *query, size_t start_offset, int direction, bool case_insensitive);
int pt_count_occurrences(PieceTable *pt, const char *query);

#endif
