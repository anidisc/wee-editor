#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include "piecetable.h"

PieceTable *pt_create(const char *initial_data, size_t length) {
    PieceTable *pt = malloc(sizeof(PieceTable));
    if (!pt) return NULL;
    pt->original.data = malloc(length > 0 ? length : 1);
    if (length > 0 && initial_data) memcpy(pt->original.data, initial_data, length);
    pt->original.length = length;
    pt->add.data = NULL;
    pt->add.length = 0;
    pt->total_length = length;

    Piece *p = malloc(sizeof(Piece));
    p->type = BUFFER_ORIGINAL;
    p->start = 0;
    p->length = length;
    p->next = NULL;
    p->prev = NULL;
    pt->head = p;
    return pt;
}

PieceTable *pt_open(const char *filename) {
    FILE *f = fopen(filename, "rb");
    if (!f) return pt_create("", 0);
    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    if (fsize < 0) { fclose(f); return pt_create("", 0); }
    size_t length = (size_t)fsize;
    fseek(f, 0, SEEK_SET);
    char *data = malloc(length > 0 ? length : 1);
    if (length > 0) {
        if (fread(data, 1, length, f) != length) { free(data); fclose(f); return pt_create("", 0); }
    }
    fclose(f);
    PieceTable *pt = pt_create(data, length);
    free(data);
    return pt;
}

void pt_destroy(PieceTable *pt) {
    if (!pt) return;
    Piece *curr = pt->head;
    while (curr) {
        Piece *next = curr->next;
        free(curr);
        curr = next;
    }
    if (pt->original.data) free(pt->original.data);
    if (pt->add.data) free(pt->add.data);
    free(pt);
}

char *pt_get_text(PieceTable *pt, size_t offset, size_t length) {
    if (!pt || offset + length > pt->total_length) return NULL;
    char *res = malloc(length + 1);
    if (!res) return NULL;
    size_t res_idx = 0;
    size_t curr_off = 0;
    Piece *curr = pt->head;
    while (curr && res_idx < length) {
        if (curr_off + curr->length > offset) {
            size_t start_in_piece = (offset > curr_off) ? offset - curr_off : 0;
            size_t to_copy = curr->length - start_in_piece;
            if (res_idx + to_copy > length) to_copy = length - res_idx;
            const char *src = (curr->type == BUFFER_ORIGINAL) ? pt->original.data : pt->add.data;
            if (src) memcpy(res + res_idx, src + curr->start + start_in_piece, to_copy);
            res_idx += to_copy;
        }
        curr_off += curr->length;
        curr = curr->next;
    }
    res[res_idx] = '\0';
    return res;
}

void pt_insert(PieceTable *pt, size_t offset, const char *data, size_t length) {
    if (!pt || length == 0) return;
    char *new_add = realloc(pt->add.data, pt->add.length + length);
    if (!new_add) return;
    pt->add.data = new_add;
    memcpy(pt->add.data + pt->add.length, data, length);
    size_t add_start = pt->add.length;
    pt->add.length += length;

    Piece *p = pt->head;
    size_t curr_off = 0;
    while (p) {
        if (curr_off <= offset && curr_off + p->length >= offset) {
            size_t split_point = offset - curr_off;
            if (split_point == 0) {
                Piece *new_p = malloc(sizeof(Piece));
                new_p->type = BUFFER_ADD; new_p->start = add_start; new_p->length = length;
                new_p->next = p; new_p->prev = p->prev;
                if (p->prev) p->prev->next = new_p; else pt->head = new_p;
                p->prev = new_p;
                break;
            } else if (split_point == p->length) {
                Piece *new_p = malloc(sizeof(Piece));
                new_p->type = BUFFER_ADD; new_p->start = add_start; new_p->length = length;
                new_p->next = p->next; new_p->prev = p;
                if (p->next) p->next->prev = new_p;
                p->next = new_p;
                break;
            } else {
                Piece *p2 = malloc(sizeof(Piece));
                p2->type = p->type; p2->start = p->start + split_point; p2->length = p->length - split_point;
                p2->next = p->next; p2->prev = p;
                if (p->next) p->next->prev = p2;
                p->next = p2;
                p->length = split_point;
                Piece *new_p = malloc(sizeof(Piece));
                new_p->type = BUFFER_ADD; new_p->start = add_start; new_p->length = length;
                new_p->next = p2; new_p->prev = p;
                p->next = new_p;
                p2->prev = new_p;
                break;
            }
        }
        curr_off += p->length;
        p = p->next;
    }
    pt->total_length += length;
}

void pt_delete_fixed(PieceTable *pt, size_t offset, size_t length) {
    if (!pt || length == 0 || pt->total_length == 0) return;
    if (offset + length > pt->total_length) length = pt->total_length - offset;
    size_t to_delete = length;
    Piece *p = pt->head;
    size_t curr_off = 0;
    while (p && to_delete > 0) {
        size_t p_end = curr_off + p->length;
        if (p_end > offset) {
            size_t del_start_in_p = (offset > curr_off) ? offset - curr_off : 0;
            size_t del_len_in_p = p->length - del_start_in_p;
            if (del_len_in_p > to_delete) del_len_in_p = to_delete;

            if (del_start_in_p == 0 && del_len_in_p == p->length) {
                Piece *to_del = p;
                if (p->prev) p->prev->next = p->next; else pt->head = p->next;
                if (p->next) p->next->prev = p->prev;
                p = p->next;
                free(to_del);
                to_delete -= del_len_in_p;
            } else if (del_start_in_p == 0) {
                p->start += del_len_in_p;
                p->length -= del_len_in_p;
                to_delete -= del_len_in_p;
            } else if (del_start_in_p + del_len_in_p == p->length) {
                p->length -= del_len_in_p;
                to_delete -= del_len_in_p;
                curr_off += p->length;
                p = p->next;
            } else {
                Piece *p2 = malloc(sizeof(Piece));
                p2->type = p->type;
                p2->start = p->start + del_start_in_p + del_len_in_p;
                p2->length = p->length - (del_start_in_p + del_len_in_p);
                p2->next = p->next;
                p2->prev = p;
                if (p->next) p->next->prev = p2;
                p->next = p2;
                p->length = del_start_in_p;
                to_delete -= del_len_in_p;
                curr_off += p->length;
                p = p2;
            }
        } else {
            curr_off += p->length;
            p = p->next;
        }
    }
    pt->total_length -= length;
}

bool pt_save(PieceTable *pt, const char *filename) {
    if (!pt || !filename) return false;
    
    // Default save (internal LF format)
    return pt_save_ext(pt, filename, false);
}

bool pt_save_ext(PieceTable *pt, const char *filename, bool force_crlf) {
    if (!pt || !filename) return false;
    
    size_t actual_len = 0;
    Piece *check = pt->head;
    while (check) { actual_len += check->length; check = check->next; }
    if (actual_len != pt->total_length) pt->total_length = actual_len;

    char tmp_name[2048];
    snprintf(tmp_name, sizeof(tmp_name), "%s.tmp", filename);
    
    FILE *f = fopen(tmp_name, "wb");
    if (!f) return false;

    Piece *curr = pt->head;
    while (curr) {
        if (curr->length > 0) {
            const char *src = (curr->type == BUFFER_ORIGINAL) ? pt->original.data : pt->add.data;
            if (!src) { fclose(f); unlink(tmp_name); return false; }

            if (force_crlf) {
                // Expand \n to \r\n
                for (size_t i = 0; i < curr->length; i++) {
                    char c = src[curr->start + i];
                    if (c == '\n') {
                        // Check if it's already part of a CRLF
                        bool already_crlf = false;
                        if (i > 0 && src[curr->start + i - 1] == '\r') already_crlf = true;
                        
                        if (!already_crlf) {
                            if (fputc('\r', f) == EOF) { fclose(f); unlink(tmp_name); return false; }
                        }
                    }
                    if (fputc(c, f) == EOF) { fclose(f); unlink(tmp_name); return false; }
                }
            } else {
                if (fwrite(src + curr->start, 1, curr->length, f) != curr->length) {
                    fclose(f); unlink(tmp_name); return false;
                }
            }
        }
        curr = curr->next;
    }
    
    if (fflush(f) != 0 || fsync(fileno(f)) != 0) {
        fclose(f); unlink(tmp_name); return false;
    }
    fclose(f);

    if (rename(tmp_name, filename) != 0) {
        unlink(tmp_name); return false;
    }
    return true;
}

int64_t pt_find(PieceTable *pt, const char *query, size_t start_offset, int direction, bool case_insensitive) {
    size_t qlen = query ? strlen(query) : 0;
    if (qlen == 0 || !pt || pt->total_length == 0) return -1;
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
            offset += (to_read > qlen) ? (to_read - qlen + 1) : 1;
        }
    } else {
        int64_t offset = (int64_t)start_offset;
        while (offset >= 0) {
            size_t read_len = (offset + qlen > window_size) ? window_size : (offset + qlen);
            size_t read_start = (offset + qlen >= read_len) ? (offset + qlen - read_len) : 0;
            char *chunk = pt_get_text(pt, read_start, read_len);
            if (!chunk) break;
            char *last_match = NULL;
            char *match = case_insensitive ? strcasestr(chunk, query) : strstr(chunk, query);
            while (match) {
                if (read_start + (match - chunk) <= (size_t)offset) { last_match = match; match = case_insensitive ? strcasestr(match + 1, query) : strstr(match + 1, query); }
                else break;
            }
            if (last_match) { int64_t res = (int64_t)(read_start + (last_match - chunk)); free(chunk); return res; }
            free(chunk);
            if (read_start == 0) break;
            offset = (int64_t)read_start - 1;
        }
    }
    return -1;
}

int pt_count_occurrences(PieceTable *pt, const char *query) {
    if (!pt || !query || strlen(query) == 0) return 0;
    int count = 0; size_t offset = 0; const size_t window_size = 8192; size_t qlen = strlen(query);
    while (offset < pt->total_length) {
        size_t to_read = (pt->total_length - offset > window_size) ? window_size : (pt->total_length - offset);
        char *chunk = pt_get_text(pt, offset, to_read);
        if (!chunk) break;
        char *match = strcasestr(chunk, query);
        while (match) { count++; match = strcasestr(match + 1, query); }
        free(chunk);
        if (offset + to_read >= pt->total_length) break;
        offset += (to_read > qlen) ? (to_read - qlen + 1) : 1;
    }
    return count;
}
