#include "undo.h"
#include <stdlib.h>
#include <string.h>

UndoStack* undo_create() {
    return calloc(1, sizeof(UndoStack));
}

void undo_destroy(UndoStack *s) {
    if (!s) return;
    Action *curr = s->head;
    while (curr) {
        Action *next = curr->next;
        free(curr->data);
        free(curr);
        curr = next;
    }
    free(s);
}

void undo_push(UndoStack *s, ActionType type, size_t offset, const char *data, size_t len) {
    // Clear redo path
    if (s->current && s->current->next) {
        Action *to_del = s->current->next;
        while (to_del) {
            Action *next = to_del->next;
            free(to_del->data);
            free(to_del);
            to_del = next;
        }
        s->current->next = NULL;
    }

    Action *a = calloc(1, sizeof(Action));
    a->type = type;
    a->offset = offset;
    a->len = len;
    if (data) {
        a->data = malloc(len);
        memcpy(a->data, data, len);
    }

    if (!s->head) {
        s->head = s->current = a;
    } else {
        a->prev = s->current;
        s->current->next = a;
        s->current = a;
    }
}

Action* undo_pop(UndoStack *s) {
    if (!s->current) return NULL;
    Action *a = s->current;
    s->current = s->current->prev;
    return a;
}

Action* redo_pop(UndoStack *s) {
    if (!s->current) {
        if (s->head) {
            s->current = s->head;
            return s->head;
        }
        return NULL;
    }
    if (!s->current->next) return NULL;
    s->current = s->current->next;
    return s->current;
}
