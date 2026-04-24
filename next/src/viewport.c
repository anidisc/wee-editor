#include "viewport.h"
#include "utf8.h"
#include "highlight.h"
#include <stdlib.h>
#include <string.h>

Viewport* vp_create(int rows) {
    Viewport *vp = calloc(1, sizeof(Viewport));
    vp->rows = rows;
    vp->lines = calloc(rows, sizeof(ViewLine));
    for (int i = 0; i < rows; i++) vp->lines[i].logical_row = -1;
    return vp;
}

void vp_destroy(Viewport *vp) {
    if (!vp) return;
    for (int i = 0; i < vp->rows; i++) {
        free(vp->lines[i].chars);
        free(vp->lines[i].hl);
        free(vp->lines[i].cx_to_rx);
    }
    free(vp->lines);
    free(vp);
}

static void clear_view_line(ViewLine *vl) {
    free(vl->chars);
    free(vl->hl);
    free(vl->cx_to_rx);
    memset(vl, 0, sizeof(ViewLine));
    vl->logical_row = -1;
}

static void fill_view_line(ViewLine *vl, const char *text, int len, int logical_row, int byte_offset, bool is_wrapped) {
    vl->chars = malloc(len + 1);
    memcpy(vl->chars, text, len);
    vl->chars[len] = '\0';
    vl->len = len;
    vl->hl = malloc(len);
    vl->logical_row = logical_row;
    vl->byte_offset = byte_offset;
    vl->is_wrapped = is_wrapped;

    vl->cx_to_rx = malloc(sizeof(int) * (len + 1));
    int rx = 0;
    for (int j = 0; j < len; ) {
        vl->cx_to_rx[j] = rx;
        int clen = utf8_char_len(vl->chars + j);
        rx += utf8_char_width(vl->chars + j);
        for (int k = 1; k < clen && (j + k) <= len; k++) {
            vl->cx_to_rx[j + k] = rx;
        }
        j += clen;
    }
    vl->cx_to_rx[len] = rx;
    vl->visual_len = rx;
}

void vp_sync(Viewport *vp, PieceTable *pt, LineIndex *li, int rowoff, int wrap_width) {
    vp->rowoff = rowoff;
    int line_count = li_get_line_count(li);
    int v_idx = 0;
    int l_idx = rowoff;

    for (int i = 0; i < vp->rows; i++) clear_view_line(&vp->lines[i]);

    while (v_idx < vp->rows && l_idx < line_count) {
        size_t start = li_get_offset(li, l_idx);
        size_t end = (l_idx + 1 < line_count) ? li_get_offset(li, l_idx + 1) : pt->total_length;
        int total_len = (int)(end - start);
        
        char *text = pt_get_text(pt, start, total_len);
        if (!text) { l_idx++; continue; }

        int len = total_len;
        if (len > 0 && (text[len-1] == '\n' || text[len-1] == '\r')) {
            if (len > 1 && (text[len-2] == '\n' || text[len-2] == '\r')) len -= 2;
            else len--;
        }

        if (wrap_width <= 0 || len <= wrap_width) {
            fill_view_line(&vp->lines[v_idx++], text, len, l_idx, 0, false);
        } else {
            int curr_byte = 0;
            bool wrapped = false;
            while (curr_byte < len && v_idx < vp->rows) {
                int bytes_that_fit = 0;
                int cols_used = 0;
                while (curr_byte + bytes_that_fit < len) {
                    int clen = utf8_char_len(text + curr_byte + bytes_that_fit);
                    int cw = utf8_char_width(text + curr_byte + bytes_that_fit);
                    if (cols_used + cw > wrap_width && cols_used > 0) break;
                    cols_used += cw;
                    bytes_that_fit += clen;
                }
                
                fill_view_line(&vp->lines[v_idx++], text + curr_byte, bytes_that_fit, l_idx, curr_byte, wrapped);
                curr_byte += bytes_that_fit;
                wrapped = true;
            }
        }
        free(text);
        l_idx++;
    }
}
