#define _GNU_SOURCE
#define _DEFAULT_SOURCE
#include "piecetable.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <assert.h>

static Piece* create_piece(BufferType type, size_t start, size_t length) {
    Piece *p = malloc(sizeof(Piece));
    if (!p) return NULL;
    p->type = type;
    p->start = start;
    p->length = length;
    p->prev = p->next = NULL;
    return p;
}

static void insert_piece_after(PieceTable *pt, Piece *after, Piece *new_p) {
    if (!after) {
        new_p->next = pt->head;
        if (pt->head) pt->head->prev = new_p;
        pt->head = new_p;
        if (!pt->tail) pt->tail = new_p;
    } else {
        new_p->next = after->next;
        new_p->prev = after;
        if (after->next) after->next->prev = new_p;
        after->next = new_p;
        if (after == pt->tail) pt->tail = new_p;
    }
}

static void remove_piece(PieceTable *pt, Piece *p) {
    if (p->prev) p->prev->next = p->next;
    else pt->head = p->next;
    if (p->next) p->next->prev = p->prev;
    else pt->tail = p->prev;
    free(p);
}

PieceTable* pt_create(const char *initial_data, size_t initial_size) {
    PieceTable *pt = calloc(1, sizeof(PieceTable));
    if (!pt) return NULL;
    if (initial_data && initial_size > 0) {
        pt->original.data = malloc(initial_size);
        memcpy(pt->original.data, initial_data, initial_size);
        pt->original.size = pt->original.capacity = initial_size;
        pt->original.is_mmap = false;
        pt->head = pt->tail = create_piece(BUFFER_ORIGINAL, 0, initial_size);
        pt->total_length = initial_size;
    }
    pt->add.capacity = 4096;
    pt->add.data = malloc(pt->add.capacity);
    return pt;
}

PieceTable* pt_open(const char *filename) {
    int fd = open(filename, O_RDONLY);
    if (fd < 0) return pt_create("", 0);
    struct stat st;
    fstat(fd, &st);
    size_t size = st.st_size;
    if (size == 0) { close(fd); return pt_create("", 0); }
    char *data = mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    PieceTable *pt = calloc(1, sizeof(PieceTable));
    pt->original.data = data;
    pt->original.size = pt->original.capacity = size;
    pt->original.is_mmap = true;
    pt->head = pt->tail = create_piece(BUFFER_ORIGINAL, 0, size);
    pt->total_length = size;
    pt->add.capacity = 4096;
    pt->add.data = malloc(pt->add.capacity);
    return pt;
}

void pt_destroy(PieceTable *pt) {
    if (!pt) return;
    if (pt->original.is_mmap) munmap(pt->original.data, pt->original.size);
    else free(pt->original.data);
    free(pt->add.data);
    Piece *curr = pt->head;
    while (curr) { Piece *next = curr->next; free(curr); curr = next; }
    free(pt);
}

bool pt_insert(PieceTable *pt, size_t offset, const char *text, size_t len) {
    if (offset > pt->total_length) return false;
    if (pt->add.size + len > pt->add.capacity) {
        pt->add.capacity = (pt->add.capacity + len) * 2;
        pt->add.data = realloc(pt->add.data, pt->add.capacity);
    }
    size_t add_offset = pt->add.size;
    memcpy(pt->add.data + add_offset, text, len);
    pt->add.size += len;
    Piece *new_p = create_piece(BUFFER_ADD, add_offset, len);
    if (offset == 0) insert_piece_after(pt, NULL, new_p);
    else if (offset == pt->total_length) insert_piece_after(pt, pt->tail, new_p);
    else {
        Piece *curr = pt->head; size_t cur_off = 0;
        while (curr) {
            if (cur_off + curr->length > offset) {
                size_t split_off = offset - cur_off;
                Piece *next_part = create_piece(curr->type, curr->start + split_off, curr->length - split_off);
                curr->length = split_off;
                next_part->next = curr->next; next_part->prev = new_p;
                if (curr->next) curr->next->prev = next_part; else pt->tail = next_part;
                new_p->next = next_part; new_p->prev = curr; curr->next = new_p;
                break;
            }
            cur_off += curr->length; curr = curr->next;
        }
    }
    pt->total_length += len;
    return true;
}

bool pt_delete_fixed(PieceTable *pt, size_t offset, size_t len) {
    if (len == 0 || offset + len > pt->total_length) return false;
    size_t original_len = len; Piece *curr = pt->head; size_t cur_off = 0;
    while (curr && len > 0) {
        if (cur_off + curr->length > offset) {
            size_t split_off = offset - cur_off;
            size_t can_del = curr->length - split_off;
            if (can_del > len) can_del = len;
            if (split_off == 0 && can_del == curr->length) { Piece *next = curr->next; remove_piece(pt, curr); curr = next; }
            else if (split_off == 0) { curr->start += can_del; curr->length -= can_del; curr = curr->next; }
            else if (split_off + can_del == curr->length) { curr->length = split_off; curr = curr->next; }
            else {
                Piece *p2 = create_piece(curr->type, curr->start + split_off + can_del, curr->length - (split_off + can_del));
                curr->length = split_off; p2->next = curr->next; p2->prev = curr;
                if (curr->next) curr->next->prev = p2; else pt->tail = p2;
                curr->next = p2; len = 0; break;
            }
            len -= can_del; cur_off = offset; 
        } else { cur_off += curr->length; curr = curr->next; }
    }
    pt->total_length -= original_len;
    return true;
}

char* pt_get_text(PieceTable *pt, size_t offset, size_t len) {
    if (offset + len > pt->total_length) return NULL;
    char *res = malloc(len + 1); size_t res_idx = 0; size_t cur_off = 0; Piece *curr = pt->head;
    while (curr && res_idx < len) {
        if (cur_off + curr->length > offset) {
            size_t start_in_piece = (offset > cur_off) ? (offset - cur_off) : 0;
            size_t to_copy = curr->length - start_in_piece;
            if (res_idx + to_copy > len) to_copy = len - res_idx;
            const char *src = (curr->type == BUFFER_ORIGINAL) ? pt->original.data : pt->add.data;
            memcpy(res + res_idx, src + curr->start + start_in_piece, to_copy);
            res_idx += to_copy; offset = cur_off + curr->length;
        }
        cur_off += curr->length; curr = curr->next;
    }
    res[len] = '\0'; return res;
}

bool pt_save(PieceTable *pt, const char *filename) {
    FILE *f = fopen(filename, "wb"); if (!f) return false;
    Piece *curr = pt->head;
    while (curr) {
        const char *src = (curr->type == BUFFER_ORIGINAL) ? pt->original.data : pt->add.data;
        fwrite(src + curr->start, 1, curr->length, f);
        curr = curr->next;
    }
    fclose(f); return true;
}

int64_t pt_find(PieceTable *pt, const char *query, size_t start_offset, int direction, bool case_insensitive) {
    size_t qlen = strlen(query);
    if (qlen == 0 || pt->total_length == 0) return -1;
    const size_t window_size = 8192;
    if (direction == 1) {
        size_t offset = start_offset;
        while (offset < pt->total_length) {
            size_t to_read = (pt->total_length - offset > window_size) ? window_size : (pt->total_length - offset);
            char *chunk = pt_get_text(pt, offset, to_read);
            if (!chunk) break;
            char *match = case_insensitive ? strcasestr(chunk, query) : strstr(chunk, query);
            if (match) { int64_t res = (int64_t)(offset + (match - chunk)); free(chunk); return res; }
            free(chunk);
            if (offset + to_read >= pt->total_length) break;
            offset += (to_read - qlen + 1);
        }
    } else {
        size_t offset = (start_offset > pt->total_length) ? pt->total_length : start_offset;
        while (offset >= qlen) {
            size_t to_read = (offset > window_size) ? window_size : offset;
            size_t chunk_start = offset - to_read;
            char *chunk = pt_get_text(pt, chunk_start, to_read);
            if (!chunk) break;
            char *last_match = NULL;
            char *current_match = case_insensitive ? strcasestr(chunk, query) : strstr(chunk, query);
            while (current_match) {
                if (chunk_start + (current_match - chunk) < start_offset) last_match = current_match;
                current_match = case_insensitive ? strcasestr(current_match + 1, query) : strstr(current_match + 1, query);
            }
            if (last_match) { int64_t res = (int64_t)(chunk_start + (last_match - chunk)); free(chunk); return res; }
            free(chunk);
            if (chunk_start == 0) break;
            offset = chunk_start + qlen - 1;
        }
    }
    return -1;
}

int pt_count_occurrences(PieceTable *pt, const char *query) {
    size_t qlen = strlen(query);
    if (qlen == 0 || pt->total_length == 0) return 0;
    int count = 0; size_t offset = 0; const size_t window_size = 8192;
    while (offset < pt->total_length) {
        size_t to_read = (pt->total_length - offset > window_size) ? window_size : (pt->total_length - offset);
        char *chunk = pt_get_text(pt, offset, to_read);
        if (!chunk) break;
        char *match = strcasestr(chunk, query);
        while (match) { count++; match = strcasestr(match + 1, query); }
        free(chunk);
        if (offset + to_read >= pt->total_length) break;
        offset += (to_read - qlen + 1);
    }
    return count;
}

void pt_print_debug(PieceTable *pt) {
    printf("PieceTable [len: %zu]:\n", pt->total_length);
    Piece *curr = pt->head; int i = 0;
    while (curr) {
        printf("  Piece %d: %s, start %zu, len %zu\n", i++, (curr->type == BUFFER_ORIGINAL ? "ORIG" : "ADD"), curr->start, curr->length);
        curr = curr->next;
    }
}
