#ifndef PIECETABLE_H
#define PIECETABLE_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

typedef enum {
    BUFFER_ORIGINAL,
    BUFFER_ADD
} BufferType;

typedef struct Piece {
    BufferType type;
    size_t start;
    size_t length;
    struct Piece *prev;
    struct Piece *next;
} Piece;

typedef struct {
    char *data;
    size_t size;
    size_t capacity;
    bool is_mmap;
} Buffer;

typedef struct {
    Buffer original;
    Buffer add;
    Piece *head;
    Piece *tail;
    size_t total_length;
} PieceTable;

// Lifecycle
PieceTable* pt_create(const char *initial_data, size_t initial_size);
PieceTable* pt_open(const char *filename);
void pt_destroy(PieceTable *pt);

// Core Operations
bool pt_insert(PieceTable *pt, size_t offset, const char *text, size_t len);
bool pt_delete(PieceTable *pt, size_t offset, size_t len);
bool pt_delete_fixed(PieceTable *pt, size_t offset, size_t len);

// Data Access
char* pt_get_text(PieceTable *pt, size_t offset, size_t len);
bool pt_save(PieceTable *pt, const char *filename);
int64_t pt_find(PieceTable *pt, const char *query, size_t start_offset, int direction, bool case_insensitive);
int pt_count_occurrences(PieceTable *pt, const char *query);

// Debug/Internal
void pt_print_debug(PieceTable *pt);

#endif
