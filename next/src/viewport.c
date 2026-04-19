#include "viewport.h"
#include "utf8.h"
#include "highlight.h"
#include <stdlib.h>
#include <string.h>

Viewport* vp_create(int rows) {
    Viewport *vp = calloc(1, sizeof(Viewport));
    vp->rows = rows;
    vp->lines = calloc(rows, sizeof(ViewLine));
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
}

void vp_sync(Viewport *vp, PieceTable *pt, LineIndex *li, int rowoff) {
    vp->rowoff = rowoff;
    int line_count = li_get_line_count(li);

    for (int i = 0; i < vp->rows; i++) {
        int filerow = i + rowoff;
        ViewLine *vl = &vp->lines[i];
        clear_view_line(vl);

        if (filerow >= line_count) continue;

        size_t start = li_get_offset(li, filerow);
        size_t end = (filerow + 1 < line_count) ? li_get_offset(li, filerow + 1) : pt->total_length;
        int len = (int)(end - start);
        
        if (len > 0) {
            char *text = pt_get_text(pt, start, len);
            // Remove trailing newline for rendering
            if (text[len-1] == '\n' || text[len-1] == '\r') {
                if (len > 1 && (text[len-2] == '\n' || text[len-2] == '\r')) len -= 2;
                else len--;
            }

            vl->chars = malloc(len + 1);
            memcpy(vl->chars, text, len);
            vl->chars[len] = '\0';
            vl->len = len;

            vl->hl = malloc(len);

            vl->cx_to_rx = malloc(sizeof(int) * (len + 1));
            int rx = 0;
            for (int j = 0; j < len; ) {
                vl->cx_to_rx[j] = rx;
                int clen = utf8_char_len(vl->chars + j);
                rx += utf8_char_width(vl->chars + j);
                for (int k = 1; k < clen && (j + k) <= len; k++) {
                    vl->cx_to_rx[j + k] = rx; // Multi-byte part points to next column
                }
                j += clen;
            }
            vl->cx_to_rx[len] = rx;
            vl->visual_len = rx;

            free(text);
        } else {
            vl->cx_to_rx = malloc(sizeof(int));
            vl->cx_to_rx[0] = 0;
        }
    }
}
