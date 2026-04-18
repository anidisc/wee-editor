#ifndef UNDO_H
#define UNDO_H

#include <stddef.h>
#include <stdbool.h>

typedef enum {
    ACTION_INSERT,
    ACTION_DELETE
} ActionType;

typedef struct Action {
    ActionType type;
    size_t offset;
    char *data;
    size_t len;
    struct Action *next;
    struct Action *prev;
} Action;

typedef struct {
    Action *head;
    Action *current;
} UndoStack;

UndoStack* undo_create();
void undo_destroy(UndoStack *s);
void undo_push(UndoStack *s, ActionType type, size_t offset, const char *data, size_t len);
Action* undo_pop(UndoStack *s);
Action* redo_pop(UndoStack *s);

#endif
