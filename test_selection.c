#include <stdio.h>

// Simulated state for testing
struct {
    int cx, cy;
    int selection_start_cx, selection_start_cy;
    int selection_end_cx, selection_end_cy;
    int selection_active;
    int numrows;
    struct { int size; } row[10];
} E;

void printState(const char* action) {
    printf("%s: cx=%d, cy=%d, anchor=(%d,%d), cursor=(%d,%d), active=%d\n",
           action, E.cx, E.cy, 
           E.selection_start_cx, E.selection_start_cy,
           E.selection_end_cx, E.selection_end_cy,
           E.selection_active);
}

void mockEditorQuickSelectChar(int direction) {
    printf("\n=== mockEditorQuickSelectChar(direction=%d) ===\n", direction);
    
    // Initialize rows
    for (int i = 0; i < 10; i++) {
        E.row[i].size = 20; // Each row has 20 characters
    }
    E.numrows = 10;
    
    // If no selection is active, set anchor to current cursor position
    if (!E.selection_active) {
        E.selection_start_cx = E.cx;  // This becomes our anchor
        E.selection_start_cy = E.cy;
        E.selection_end_cx = E.cx;    // This will be our moving cursor
        E.selection_end_cy = E.cy;
        E.selection_active = 1;
        printf("INITIALIZED selection\n");
    }
    
    printState("BEFORE move");
    
    // Get current row for bounds checking
    int current_row_size = E.row[E.cy].size;
    
    // Move the cursor (selection end point) in the specified direction
    if (direction == -1) { // Left
        if (E.cx > 0) {
            E.cx--;
        } else if (E.cy > 0) {
            E.cy--;
            E.cx = E.row[E.cy].size;
        } else {
            printf("Cannot move left - at beginning of file\n");
            return;
        }
    } else { // Right
        // Get current row size AFTER potential line change
        int updated_row_size = E.row[E.cy].size;
        if (E.cx < updated_row_size) {
            E.cx++;
        } else if (E.cy < E.numrows - 1) {
            E.cy++;
            E.cx = 0;
        } else {
            printf("Cannot move right - at end of file\n");
            return;
        }
    }
    
    printState("AFTER move");
    
    // Update the selection end to the new cursor position
    E.selection_end_cx = E.cx;
    E.selection_end_cy = E.cy;
    
    printState("AFTER update end");
    
    // Check if anchor and cursor are at the same position (empty selection)
    if (E.selection_start_cy == E.selection_end_cy && 
        E.selection_start_cx == E.selection_end_cx) {
        printf("*** DESELECTION TRIGGERED! ***\n");
        E.selection_active = 0;
        return;
    }
    
    printState("FINAL");
}

int main() {
    // Test scenario: start at position (5, 2)
    E.cx = 5;
    E.cy = 2;
    E.selection_active = 0;
    
    printf("Starting position: cx=%d, cy=%d\n", E.cx, E.cy);
    
    // Move right (should create selection)
    mockEditorQuickSelectChar(1);
    
    // Move left (should return to original position and deselect)
    mockEditorQuickSelectChar(-1);
    
    printf("\nFinal result: selection_active = %d\n", E.selection_active);
    
    return 0;
}
