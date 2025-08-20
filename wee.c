/* includes */

#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>
#include "cJSON.h"

/* defines */

#define WEE_VERSION "0.91"
#define WEE_TAB_STOP 4
#define WEE_QUIT_TIMES 2
#define UNDO_BUFFER_SIZE 10

#define CTRL_KEY(k) ((k) & 0x1f)

enum editorKey {
  BACKSPACE = 127,
  ARROW_LEFT = 1000,
  ARROW_RIGHT,
  ARROW_UP,
  ARROW_DOWN,
  DEL_KEY,
  HOME_KEY,
  END_KEY,
  PAGE_UP,
  PAGE_DOWN,
  ALT_B,
  ALT_E,
  ALT_R,
  SHIFT_UP,
  SHIFT_DOWN,
  SHIFT_LEFT,
  SHIFT_RIGHT,
  SHIFT_TAB
};

enum editorHighlight {
  HL_NORMAL = 0,
  HL_COMMENT,
  HL_MLCOMMENT,
  HL_KEYWORD1,
  HL_KEYWORD2,
  HL_STRING,
  HL_NUMBER,
  HL_MATCH,
  HL_SELECTION
};

#define HL_HIGHLIGHT_NUMBERS (1<<0)
#define HL_HIGHLIGHT_STRINGS (1<<1)

/* data */

struct editorSyntax {
  char *language;
  char **filematch;
  char **keywords;
  char *singleline_comment_start;
  char *multiline_comment_start;
  char *multiline_comment_end;
  int flags;
};

typedef struct erow {
  int idx;
  int size;
  char *chars;
  int rsize;
  char *render;
  unsigned char *hl;
  int hl_open_comment;
} erow;

/* Struttura per salvare lo stato completo dell'editor */
struct EditorSnapshot {
    // Contenuto del file
    erow *rows;
    int numrows;
    
    // Posizione cursore
    int cx, cy;
    int rowoff, coloff;
    
    // Stato selezione
    int selection_active;
    int selection_start_cx, selection_start_cy;
    int selection_end_cx, selection_end_cy;
    
    // Metadati
    time_t timestamp;
    char *description;
    
    // Lista doppia per navigazione
    struct EditorSnapshot *prev;
    struct EditorSnapshot *next;
};

/* Sistema undo/redo basato su snapshot */
struct UndoSystem {
    struct EditorSnapshot *current;
    struct EditorSnapshot *head;
    int max_snapshots;
    int current_count;
    time_t last_snapshot_time;
};

struct editorConfig {
  int cx, cy;
  int rx;
  int rowoff;
  int coloff;
  int screenrows;
  int screencols;
  int numrows;
  erow *row;
  char *filename;
  char statusmsg[256];
  time_t statusmsg_time;
  struct termios orig_termios;
  int dirty;
  int linenumbers;
  char *clipboard;
  int clipboard_len;
  int hl_row;
  int hl_start;
  int hl_end;
  struct editorSyntax *syntax;
  int selection_start_cx;
  int selection_start_cy;
  int selection_end_cx;
  int selection_end_cy;
  int selection_active;
  int mode;
  struct UndoSystem undo_system;
};

enum editorMode {
  NORMAL_MODE,
  SELECTION_MODE
};

/* append buffer */

struct abuf {
  char *b;
  int len;
};

#define ABUF_INIT {NULL, 0}

struct editorConfig E;

/* prototypes */

void editorSetStatusMessage(const char *fmt, ...);
void editorRefreshScreen();
char *editorPrompt(char *prompt, void (*callback)(char *, int));
void editorMoveCursor(int key);
void editorSave();
char *editorFileBrowser(const char *initial_path);
int editorAskToSave();
void editorNewFile();
void editorDelChar();
void editorDelCharSelection();
void editorShowHelp();
void editorUpdateSyntax(erow *row);
void editorSelectSyntaxHighlight();
void editorFreeSyntax();
void editorUpdateSelectionSyntax();
void editorDeselectSelection();
void editorIndentSelection();
void editorUnindentSelection();
void editorMoveSelection(int key);
int editorCanMoveSelectionLeft();
int editorCanMoveSelectionRight();
int editorIsSelectionFullLines();
int editorCanMoveSelectionVertical();
void editorJumpToLine();
void editorUndo();
void editorRedo();
void editorCreateSnapshot(const char *description);
void editorFreeSnapshot(struct EditorSnapshot *snap);
void editorClearUndoSystem();
struct EditorSnapshot* editorCopyCurrentState(const char *description);
void editorRestoreSnapshot(struct EditorSnapshot *snap);
void editorSelectRowText();
void editorSelectInsideDelims();
int findMatchingRightInLine(erow *row, int start_idx, char open_ch, char close_ch);
int findNextQuoteInLine(erow *row, int start_idx, char quote);
void editorQuickSelectFullLine(int direction);
void editorQuickSelectChar(int direction);

/* Find & Replace */
int editorCountOccurrencesInRow(erow *row, const char *needle);
int editorRowReplaceAt(erow *row, int at, int del_len, const char *repl, int repl_len);
int editorRowReplaceAll(erow *row, const char *needle, const char *repl);
int editorReplaceAllInBuffer(const char *needle, const char *repl);

void abAppend(struct abuf *ab, const char *s, int len);


/* terminal */

/**
 * @brief Clears the screen, prints an error message, and exits the program.
 * @param s The error string to be printed with perror.
 */
void die(const char *s) {
  write(STDOUT_FILENO, "\x1b[2J", 4);
  write(STDOUT_FILENO, "\x1b[H", 3);
  perror(s);
  exit(1);
}

/**
 * @brief Restores the original terminal settings on program exit.
 */
void disableRawMode() {
  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_termios) == -1)
    die("tcsetattr");
}

/**
 * @brief Enables terminal "raw" mode.
 *        This disables character echoing, canonical input, and signals,
 *        allowing the program to handle inputs directly.
 */
void enableRawMode() {
  if (tcgetattr(STDIN_FILENO, &E.orig_termios) == -1) die("tcgetattr");
  atexit(disableRawMode);
  struct termios raw = E.orig_termios;
  raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
  raw.c_oflag &= ~(OPOST);
  raw.c_cflag |= (CS8);
  raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
  raw.c_cc[VMIN] = 0;
  raw.c_cc[VTIME] = 1;
  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) die("tcsetattr");
}

/**
 * @brief Reads a single keypress from the user.
 *        Handles escape sequences for special keys like arrows, Home, End, etc.
 * @return The code of the pressed key (a character or a value from the editorKey enum).
 */
int editorReadKey() {
  int nread;
  char c;
  while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
    if (nread == -1 && errno != EAGAIN) die("read");
  }

  if (c == '\x1b') {
    char seq[3];
    if (read(STDIN_FILENO, &seq[0], 1) != 1) return '\x1b';

    if (seq[0] == '[') {
      if (read(STDIN_FILENO, &seq[1], 1) != 1) return '\x1b';
      if (seq[1] >= '0' && seq[1] <= '9') {
        if (read(STDIN_FILENO, &seq[2], 1) != 1) return '\x1b';
        if (seq[2] == '~') {
          switch (seq[1]) {
            case '1': return HOME_KEY;
            case '3': return DEL_KEY;
            case '4': return END_KEY;
            case '5': return PAGE_UP;
            case '6': return PAGE_DOWN;
            case '7': return HOME_KEY;
            case '8': return END_KEY;
          }
        } else if (seq[1] == '1' && seq[2] == ';') {
          // Sequenze modificatori come Shift+frecce: ESC[1;2X
          char seq3[2];
          if (read(STDIN_FILENO, &seq3[0], 1) != 1) return '\x1b';
          if (read(STDIN_FILENO, &seq3[1], 1) != 1) return '\x1b';
          if (seq3[0] == '2') { // Shift modifier
            switch (seq3[1]) {
              case 'A': return SHIFT_UP;
              case 'B': return SHIFT_DOWN;
              case 'C': return SHIFT_RIGHT;
              case 'D': return SHIFT_LEFT;
            }
          }
        }
      } else {
        switch (seq[1]) {
          case 'A': return ARROW_UP;
          case 'B': return ARROW_DOWN;
          case 'C': return ARROW_RIGHT;
          case 'D': return ARROW_LEFT;
          case 'H': return HOME_KEY;
          case 'F': return END_KEY;
          case 'Z': return SHIFT_TAB; // Shift+Tab (ESC[Z)
        }
      }
    } else if (seq[0] == 'O') {
      if (read(STDIN_FILENO, &seq[1], 1) != 1) return '\x1b';
      switch (seq[1]) {
        case 'H': return HOME_KEY;
        case 'F': return END_KEY;
      }
    } else {
        if (seq[0] == 'b') return ALT_B;
        if (seq[0] == 'e') return ALT_E;
        if (seq[0] == 'r') return ALT_R;
    }

    return '\x1b';
  } else {
    return c;
  }
}

/**
 * @brief Gets the dimensions (rows and columns) of the terminal window.
 * @param rows Pointer to store the number of rows.
 * @param cols Pointer to store the number of columns.
 * @return 0 on success, -1 on error.
 */
int getWindowSize(int *rows, int *cols) {
  struct winsize ws;
  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
    return -1;
  }
  else {
    *cols = ws.ws_col;
    *rows = ws.ws_row;
    return 0;
  }
}

/* row operations */

/**
 * @brief Converts the cursor position (cx) from character index to render index (rx).
 *        Takes tab characters into account.
 * @param row The text row.
 * @param cx The cursor position based on characters.
 * @return The cursor position based on rendering.
 */
int editorRowCxToRx(erow *row, int cx) {
  int rx = 0;
  for (int j = 0; j < cx; j++) {
    if (row->chars[j] == '\t')
      rx += (WEE_TAB_STOP - 1) - (rx % WEE_TAB_STOP);
    rx++;
  }
  return rx;
}

/**
 * @brief Converts the cursor position (rx) from render index to character index (cx).
 *        This is the inverse operation of editorRowCxToRx.
 * @param row The text row.
 * @param rx The cursor position based on rendering.
 * @return The cursor position based on characters.
 */
int editorRowRxToCx(erow *row, int rx) {
  int cur_rx = 0;
  int cx;
  for (cx = 0; cx < row->size; cx++) {
    if (row->chars[cx] == '\t')
      cur_rx += (WEE_TAB_STOP - 1) - (cur_rx % WEE_TAB_STOP);
    cur_rx++;
    if (cur_rx > rx) return cx;
  }
  return cx;
}

/**
 * @brief Updates the rendering representation of a row.
 *        Replaces tab characters with spaces.
 * @param row The row to update.
 */
void editorUpdateRow(erow *row) {
  int tabs = 0;
  for (int j = 0; j < row->size; j++)
    if (row->chars[j] == '\t') tabs++;
  free(row->render);
  row->render = malloc(row->size + tabs * (WEE_TAB_STOP - 1) + 1);
  int idx = 0;
  for (int j = 0; j < row->size; j++) {
    if (row->chars[j] == '\t') {
      row->render[idx++] = ' ';
      while (idx % WEE_TAB_STOP != 0) row->render[idx++] = ' ';
    } else {
      row->render[idx++] = row->chars[j];
    }
  }
  row->render[idx] = '\0';
  row->rsize = idx;

  editorUpdateSyntax(row);
}

/**
 * @brief Inserts a new row of text into the editor at a specific position.
 * @param at The index at which to insert the new row.
 * @param s The text string to insert.
 * @param len The length of the string.
 */
void editorInsertRow(int at, char *s, size_t len) {
  if (at < 0 || at > E.numrows) return;

  E.row = realloc(E.row, sizeof(erow) * (E.numrows + 1));
  memmove(&E.row[at + 1], &E.row[at], sizeof(erow) * (E.numrows - at));

  for (int j = at + 1; j <= E.numrows; j++) E.row[j].idx++;

  E.row[at].idx = at;
  E.row[at].size = len;
  E.row[at].chars = malloc(len + 1);
  memcpy(E.row[at].chars, s, len);
  E.row[at].chars[len] = '\0';

  E.row[at].rsize = 0;
  E.row[at].render = NULL;
  E.row[at].hl = NULL;
  E.row[at].hl_open_comment = 0;
  editorUpdateRow(&E.row[at]);

  E.numrows++;
  E.dirty++;
}

/**
 * @brief Frees the memory allocated for a row.
 * @param row The row to free.
 */
void editorFreeRow(erow *row) {
  free(row->render);
  free(row->chars);
  free(row->hl);
}

/**
 * @brief Deletes a row from the editor.
 * @param at The index of the row to delete.
 */
void editorDelRow(int at) {
  if (at < 0 || at >= E.numrows) return;
  erow *row = &E.row[at];
  char *deleted_text = malloc(row->size + 2);
  memcpy(deleted_text, row->chars, row->size);
  deleted_text[row->size] = '\n';
  deleted_text[row->size + 1] = '\0';
  // Lo snapshot sarà gestito dal chiamante

  editorFreeRow(&E.row[at]);
  memmove(&E.row[at], &E.row[at + 1], sizeof(erow) * (E.numrows - at - 1));
  for (int j = at; j < E.numrows - 1; j++) E.row[j].idx--;
  E.numrows--;
  E.dirty++;
}

/**
 * @brief Inserts a character into a row at a specific position.
 * @param row The row to insert the character into.
 * @param at The index at which to insert the character.
 * @param c The character to insert.
 */
void editorRowInsertChar(erow *row, int at, int c) {
  if (at < 0 || at > row->size) at = row->size;
  row->chars = realloc(row->chars, row->size + 2);
  memmove(&row->chars[at + 1], &row->chars[at], row->size - at + 1);
  row->size++;
  row->chars[at] = c;
  editorUpdateRow(row);
  E.dirty++;
}

/**
 * @brief Appends a string to the end of a row.
 * @param row The row to append the string to.
 * @param s The string to append.
 * @param len The length of the string.
 */
void editorRowAppendString(erow *row, char *s, size_t len) {
  row->chars = realloc(row->chars, row->size + len + 1);
  memcpy(&row->chars[row->size], s, len);
  row->size += len;
  row->chars[row->size] = '\0';
  editorUpdateRow(row);
  E.dirty++;
}

/**
 * @brief Deletes a character from a row at a specific position.
 * @param row The row to delete the character from.
 * @param at The index of the character to delete.
 */
void editorRowDelChar(erow *row, int at) {
  if (at < 0 || at >= row->size) return;
  memmove(&row->chars[at], &row->chars[at + 1], row->size - at);
  row->size--;
  editorUpdateRow(row);
  E.dirty++;
}

/* editor operations */

/**
 * @brief Inserts a character at the current cursor position.
 * @param c The character to insert.
 */
void editorInsertChar(int c) {
  if (E.cy == E.numrows) {
    editorInsertRow(E.numrows, "", 0);
  }
  editorRowInsertChar(&E.row[E.cy], E.cx, c);
  E.cx++;
  char closing_char = 0;
  switch (c) {
    case '(': closing_char = ')'; break;
    case '[': closing_char = ']'; break;
    case '{': closing_char = '}'; break;
    case '"': closing_char = '"'; break;
    case '\'': closing_char = '\''; break;
  }
  if (closing_char) {
    editorRowInsertChar(&E.row[E.cy], E.cx, closing_char);
  }
}

/**
 * @brief Inserts a newline at the cursor position.
 *        Maintains the indentation of the current line on the new line.
 */
void editorInsertNewline() {
  // Lo snapshot sarà gestito dal chiamante
  int prev_indent = 0;
  if (E.cy < E.numrows) {
    erow *prow = &E.row[E.cy];
    // Calcola l'indentazione della riga corrente (che diventerà quella precedente dopo lo split)
    while (prev_indent < prow->size && prow->chars[prev_indent] == ' ') prev_indent++;
  }

  if (E.cx == 0) {
    editorInsertRow(E.cy, "", 0);
  } else {
    erow *row = &E.row[E.cy];
    editorInsertRow(E.cy + 1, &row->chars[E.cx], row->size - E.cx);
    row = &E.row[E.cy];
    row->size = E.cx;
    editorUpdateRow(row);
  }
  E.cy++;
  E.cx = 0;

  // Inserisce l'indentazione nella nuova riga e posiziona il cursore
  if (E.cy < E.numrows && prev_indent > 0) {
    erow *nrow = &E.row[E.cy];
    for (int i = 0; i < prev_indent; i++) {
      editorRowInsertChar(nrow, i, ' ');
      E.cx++;
    }
  }
}

/**
 * @brief Deletes the character before the cursor (like the Backspace key).
 *        If the cursor is at the beginning of a line, it joins the current line with the previous one.
 */
char *editorGetSelection(int *len) {
    if (!E.selection_active) {
        *len = 0;
        return NULL;
    }

    int start_cx = E.selection_start_cx;
    int start_cy = E.selection_start_cy;
    int end_cx = E.selection_end_cx;
    int end_cy = E.selection_end_cy;

    if (start_cy > end_cy || (start_cy == end_cy && start_cx > end_cx)) {
        int temp_cx = start_cx;
        int temp_cy = start_cy;
        start_cx = end_cx;
        start_cy = end_cy;
        end_cx = temp_cx;
        end_cy = temp_cy;
    }

    struct abuf ab = ABUF_INIT;

    if (start_cy == end_cy) {
        int selection_len = end_cx - start_cx;
        abAppend(&ab, &E.row[start_cy].chars[start_cx], selection_len);
    } else {
        abAppend(&ab, &E.row[start_cy].chars[start_cx], E.row[start_cy].size - start_cx);
        abAppend(&ab, "\n", 1);
        for (int i = start_cy + 1; i < end_cy; i++) {
            abAppend(&ab, E.row[i].chars, E.row[i].size);
            abAppend(&ab, "\n", 1);
        }
        abAppend(&ab, E.row[end_cy].chars, end_cx);
    }

    *len = ab.len;
    abAppend(&ab, "", 1);
    return ab.b;
}

void editorDelCharSelection() {
  if (!E.selection_active) {
    editorSetStatusMessage("editorDelCharSelection: Selection not active.");
    return;
  }

  int start_cx = E.selection_start_cx;
  int start_cy = E.selection_start_cy;
  int end_cx = E.selection_end_cx;
  int end_cy = E.selection_end_cy;

  editorSetStatusMessage("editorDelCharSelection: start_cx=%d, start_cy=%d, end_cx=%d, end_cy=%d", start_cx, start_cy, end_cx, end_cy);

  if (start_cy > end_cy || (start_cy == end_cy && start_cx > end_cx)) {
    int temp_cx = start_cx;
    int temp_cy = start_cy;
    start_cx = end_cx;
    start_cy = end_cy;
    end_cx = temp_cx;
    end_cy = temp_cy;
    editorSetStatusMessage("editorDelCharSelection: Swapped coords: start_cx=%d, start_cy=%d, end_cx=%d, end_cy=%d", start_cx, start_cy, end_cx, end_cy);
  }

  if (start_cy == end_cy && start_cx == end_cx) {
      E.selection_active = 0;
      editorSetStatusMessage("editorDelCharSelection: Empty selection.");
      return;
  }

  int deleted_len;
  char *deleted_text = editorGetSelection(&deleted_len);
  // Lo snapshot sarà gestito dal chiamante
  if (deleted_text) {
      free(deleted_text);
  }

  char *suffix_of_end_row = NULL;
  int suffix_of_end_row_len = 0;
  if (end_cx < E.row[end_cy].size) {
      suffix_of_end_row_len = E.row[end_cy].size - end_cx;
      suffix_of_end_row = malloc(suffix_of_end_row_len + 1);
      memcpy(suffix_of_end_row, &E.row[end_cy].chars[end_cx], suffix_of_end_row_len);
      suffix_of_end_row[suffix_of_end_row_len] = '\0';
      editorSetStatusMessage("editorDelCharSelection: Suffix len: %d, Suffix: '%s'", suffix_of_end_row_len, suffix_of_end_row);
  }

  erow *start_row = &E.row[start_cy];
  start_row->size = start_cx;
  editorUpdateRow(start_row);
  editorSetStatusMessage("editorDelCharSelection: Start row truncated. New size: %d", start_row->size);

  for (int i = end_cy; i > start_cy; i--) {
      editorDelRow(i);
      editorSetStatusMessage("editorDelCharSelection: Deleted row %d. Remaining rows: %d", i, E.numrows);
  }

  if (suffix_of_end_row_len > 0) {
      editorRowAppendString(start_row, suffix_of_end_row, suffix_of_end_row_len);
      free(suffix_of_end_row);
      editorSetStatusMessage("editorDelCharSelection: Suffix appended. New row size: %d", start_row->size);
  }

  E.cx = start_cx;
  E.cy = start_cy;

  E.selection_active = 0;
  E.dirty++;
  editorSetStatusMessage("editorDelCharSelection: Done.");
}

void editorDelChar() {
  if (E.cy == E.numrows) return;
  if (E.cx == 0 && E.cy == 0) return;
  erow *row = &E.row[E.cy];
  if (E.cx > 0) {
    editorRowDelChar(row, E.cx - 1);
    E.cx--;
  }
  else {
    E.cx = E.row[E.cy - 1].size;
    editorRowAppendString(&E.row[E.cy - 1], row->chars, row->size);
    editorDelRow(E.cy);
    E.cy--;
  }
}

/* file i/o */

/**
 * @brief Converts all text rows of the editor into a single string.
 *        The rows are separated by newline characters.
 * @param buflen Pointer to store the total length of the string.
 * @return A pointer to the dynamically allocated string.
 */
char *editorRowsToString(int *buflen) {
  int totlen = 0;
  for (int j = 0; j < E.numrows; j++)
    totlen += E.row[j].size + 1;
  *buflen = totlen;
  char *buf = malloc(totlen);
  char *p = buf;
  for (int j = 0; j < E.numrows; j++) {
    memcpy(p, E.row[j].chars, E.row[j].size);
    p += E.row[j].size;
    *p = '\n';
    p++;
  }
  return buf;
}

/**
 * @brief Asks the user if they want to save unsaved changes.
 *        Waits for an input (Ctrl-S, ESC, Ctrl-D) to decide the action.
 * @return 1 if it can proceed (saved or discarded), 0 if the operation is canceled.
 */
int editorAskToSave() {
  if (!E.dirty) return 1;
  editorSetStatusMessage(
      "WARNING! File has unsaved changes. "
      "Press Ctrl-S to save, ESC to cancel, or Ctrl-D to discard.");
  editorRefreshScreen();
  while (1) {
    int c = editorReadKey();
    if (c == CTRL_KEY('s')) {
      editorSave();
      return !E.dirty;
    } else if (c == '\x1b') {
      editorSetStatusMessage("Save aborted.");
      return 0;
    } else if (c == CTRL_KEY('d')) {
      editorSetStatusMessage("Changes discarded.");
      return 1;
    }
  }
}

/**
 * @brief Opens a file and loads its content into the editor.
 * @param filename The path of the file to open.
 */
void editorOpen(char *filename) {
  FILE *fp = fopen(filename, "r");
  if (!fp && errno != ENOENT) {
    editorSetStatusMessage("Error: Could not open file %s: %s", filename, strerror(errno));
    return;
  }

  if (!editorAskToSave()) {
      if (fp) fclose(fp);
      return;
  }

  for (int i = 0; i < E.numrows; i++) editorFreeRow(&E.row[i]);
  free(E.row);
  E.row = NULL;
  E.numrows = 0;
  E.cx = 0; E.cy = 0; E.rowoff = 0; E.coloff = 0;

  free(E.filename);
  E.filename = strdup(filename);

  editorFreeSyntax();
  editorSelectSyntaxHighlight();
  
  editorClearUndoSystem();

  if (fp) {
    char *line = NULL;
    size_t linecap = 0;
    ssize_t linelen;
    while ((linelen = getline(&line, &linecap, fp)) != -1) {
      while (linelen > 0 &&
             (line[linelen - 1] == '\n' || line[linelen - 1] == '\r'))
        linelen--;
      editorInsertRow(E.numrows, line, linelen);
    }
    free(line);
    fclose(fp);
    E.dirty = 0;
    editorSetStatusMessage("%s opened.", filename);
  } else {
    E.dirty = 0;
    editorSetStatusMessage("New file: %s", filename);
  }
}

/**
 * @brief Saves the current content of the editor to the file.
 *        If the file has no name, it prompts the user for one.
 */
void editorSave() {
  if (E.filename == NULL) {
    E.filename = editorPrompt("Save as: %s (ESC to cancel)", NULL);
    if (E.filename == NULL) {
      editorSetStatusMessage("Save aborted");
      return;
    }
  }
  int len;
  char *buf = editorRowsToString(&len);
  int fd = open(E.filename, O_RDWR | O_CREAT, 0644);
  if (fd != -1) {
    if (ftruncate(fd, len) != -1 && write(fd, buf, len) == len) {
      close(fd);
      free(buf);
      E.dirty = 0;
      editorSetStatusMessage("%d bytes written to disk", len);
      return;
    }
    close(fd);
  }
  free(buf);
  editorSetStatusMessage("Can't save! I/O error: %s", strerror(errno));
}

/**
 * @brief Saves the current content with a new file name.
 */
void editorSaveAs() {
  char *new_filename = editorPrompt("Save as: %s (ESC to cancel)", NULL);
  if (new_filename == NULL) {
    editorSetStatusMessage("Save As aborted");
    return;
  }
  free(E.filename);
  E.filename = new_filename;
  editorSave();
}

/**
 * @brief Copies the current line to the internal clipboard.
 */
void editorCopySelection() {
  if (!E.selection_active) return;

  int start_cx = E.selection_start_cx;
  int start_cy = E.selection_start_cy;
  int end_cx = E.selection_end_cx;
  int end_cy = E.selection_end_cy;

  // Ensure start is before end
  if (start_cy > end_cy || (start_cy == end_cy && start_cx > end_cx)) {
    int temp_cx = start_cx;
    int temp_cy = start_cy;
    start_cx = end_cx;
    start_cy = end_cy;
    end_cx = temp_cx;
    end_cy = temp_cy;
  }

  free(E.clipboard);
  E.clipboard = NULL;
  E.clipboard_len = 0;

  if (start_cy == end_cy) {
    // Single line selection: copy exact characters
    int len = end_cx - start_cx;
    E.clipboard = malloc(len + 1);
    memcpy(E.clipboard, &E.row[start_cy].chars[start_cx], len);
    E.clipboard[len] = '\0';
    E.clipboard_len = len;
  } else {
    // Multi-line selection: copy full lines, including newlines
    // Copy first line from start_cx to end of line
    int len_first_line = E.row[start_cy].size - start_cx;
    E.clipboard = malloc(len_first_line + 1);
    memcpy(E.clipboard, &E.row[start_cy].chars[start_cx], len_first_line);
    E.clipboard_len = len_first_line;

    // Add newline after the first line
    E.clipboard = realloc(E.clipboard, E.clipboard_len + 1);
    E.clipboard[E.clipboard_len] = '\n';
    E.clipboard_len++;

    // Copy middle lines (full lines)
    for (int i = start_cy + 1; i < end_cy; i++) {
      E.clipboard = realloc(E.clipboard, E.clipboard_len + E.row[i].size + 1);
      memcpy(&E.clipboard[E.clipboard_len], E.row[i].chars, E.row[i].size);
      E.clipboard_len += E.row[i].size;
      E.clipboard[E.clipboard_len] = '\n';
      E.clipboard_len++;
    }

    // Copy last line from beginning of line to end_cx
    int len_last_line = end_cx;
    E.clipboard = realloc(E.clipboard, E.clipboard_len + len_last_line + 1);
    memcpy(&E.clipboard[E.clipboard_len], E.row[end_cy].chars, len_last_line);
    E.clipboard_len += len_last_line;
    E.clipboard[E.clipboard_len] = '\0'; // No newline after the last line
  }
  editorDeselectSelection(); // Deselect after copy and update highlighting
  editorRefreshScreen(); // Force immediate visual update

  editorSetStatusMessage("Selection copied.");
}

void editorCopyLine() {

  if (E.cy >= E.numrows) return;
  erow *row = &E.row[E.cy];
  free(E.clipboard);
  E.clipboard_len = row->size;
  E.clipboard = malloc(E.clipboard_len + 1);
  memcpy(E.clipboard, row->chars, E.clipboard_len);
  E.clipboard[E.clipboard_len] = '\0';
  editorSetStatusMessage("Line copied.");
}

/**
 * @brief Cuts the current line (copies and deletes).
 */
void editorCutSelection() {
  if (!E.selection_active) return;
  
  // First copy the selection WITHOUT deselecting
  int start_cx = E.selection_start_cx;
  int start_cy = E.selection_start_cy;
  int end_cx = E.selection_end_cx;
  int end_cy = E.selection_end_cy;

  // Ensure start is before end
  if (start_cy > end_cy || (start_cy == end_cy && start_cx > end_cx)) {
    int temp_cx = start_cx;
    int temp_cy = start_cy;
    start_cx = end_cx;
    start_cy = end_cy;
    end_cx = temp_cx;
    end_cy = temp_cy;
  }

  free(E.clipboard);
  E.clipboard = NULL;
  E.clipboard_len = 0;

  if (start_cy == end_cy) {
    // Single line selection: copy exact characters
    int len = end_cx - start_cx;
    E.clipboard = malloc(len + 1);
    memcpy(E.clipboard, &E.row[start_cy].chars[start_cx], len);
    E.clipboard[len] = '\0';
    E.clipboard_len = len;
  } else {
    // Multi-line selection: copy full lines, including newlines
    // Copy first line from start_cx to end of line
    int len_first_line = E.row[start_cy].size - start_cx;
    E.clipboard = malloc(len_first_line + 1);
    memcpy(E.clipboard, &E.row[start_cy].chars[start_cx], len_first_line);
    E.clipboard_len = len_first_line;

    // Add newline after the first line
    E.clipboard = realloc(E.clipboard, E.clipboard_len + 1);
    E.clipboard[E.clipboard_len] = '\n';
    E.clipboard_len++;

    // Copy middle lines (full lines)
    for (int i = start_cy + 1; i < end_cy; i++) {
      E.clipboard = realloc(E.clipboard, E.clipboard_len + E.row[i].size + 1);
      memcpy(&E.clipboard[E.clipboard_len], E.row[i].chars, E.row[i].size);
      E.clipboard_len += E.row[i].size;
      E.clipboard[E.clipboard_len] = '\n';
      E.clipboard_len++;
    }

    // Copy last line from beginning of line to end_cx
    int len_last_line = end_cx;
    E.clipboard = realloc(E.clipboard, E.clipboard_len + len_last_line + 1);
    memcpy(&E.clipboard[E.clipboard_len], E.row[end_cy].chars, len_last_line);
    E.clipboard_len += len_last_line;
    E.clipboard[E.clipboard_len] = '\0'; // No newline after the last line
  }
  
  // Now delete the selection (E.selection_active is still 1)
  editorDelCharSelection();
  editorSetStatusMessage("Selection cut.");
}

void editorCutLine() {
  if (E.cy >= E.numrows) return;
  editorCopyLine();
  editorDelRow(E.cy);
  if (E.cy >= E.numrows && E.numrows > 0) {
    E.cy = E.numrows - 1;
    E.cx = E.row[E.cy].size;
  } else if (E.numrows == 0) {
    E.cy = 0;
    E.cx = 0;
  }
  editorSetStatusMessage("Line cut.");
}

/**
 * @brief Pastes the clipboard content at the cursor position.
 */
void editorInsertRawNewline() {
  editorInsertRow(E.cy + 1, "", 0); // Insert empty row below current
  E.cy++; // Move cursor to new row
  E.cx = 0; // Move cursor to beginning of new row
}

void editorPaste() {
  if (!E.clipboard) return;

  if (E.selection_active) {
    editorDelCharSelection();
  }

  // Lo snapshot sarà gestito dal chiamante

  int paste_start_cx = E.cx;
  int paste_start_cy = E.cy;

  for (int i = 0; i < E.clipboard_len; i++) {
    if (E.clipboard[i] == '\n') {
        if (E.cx == 0) {
            editorInsertRow(E.cy, "", 0);
        } else {
            erow *row = &E.row[E.cy];
            editorInsertRow(E.cy + 1, &row->chars[E.cx], row->size - E.cx);
            row = &E.row[E.cy];
            row->size = E.cx;
            editorUpdateRow(row);
        }
        E.cy++;
        E.cx = 0;
    } else {
      if (E.cy == E.numrows) {
        editorInsertRow(E.numrows, "", 0);
      }
      editorRowInsertChar(&E.row[E.cy], E.cx, E.clipboard[i]);
      E.cx++;
    }
  }

  E.selection_start_cx = paste_start_cx;
  E.selection_start_cy = paste_start_cy;
  E.selection_end_cx = E.cx;
  E.selection_end_cy = E.cy;
  E.selection_active = 1;
  E.mode = SELECTION_MODE;

  editorSetStatusMessage("Pasted and selected.");
}

/**
 * @brief Creates a new empty file, discarding the current one (after asking to save).
 */
void editorNewFile() {
    if (E.dirty && editorAskToSave() == 0) {
        editorSetStatusMessage("New file aborted.");
        return;
    }

    for (int i = 0; i < E.numrows; i++) editorFreeRow(&E.row[i]);
    free(E.row);
    E.row = NULL;
    E.numrows = 0;
    E.cx = 0; E.cy = 0; E.rowoff = 0; E.coloff = 0;

    editorFreeSyntax();
    free(E.filename);
    E.filename = NULL;
    E.dirty = 0;

    editorClearUndoSystem();

    editorSetStatusMessage("New empty file. Ctrl-S to save.");
}

int is_separator(int c) {
  return isspace(c) || c == '\0' || strchr(",.()+-/*=~%<>[];", c) != NULL;
}

int editorSyntaxToColor(int hl) {
  switch (hl) {
    case HL_COMMENT:
    case HL_MLCOMMENT: return 36;
    case HL_KEYWORD1: return 33;
    case HL_KEYWORD2: return 32;
    case HL_STRING: return 35;
    case HL_NUMBER: return 31;
    case HL_MATCH: return 34;
    case HL_SELECTION: return 7; /* Inverse video */
    default: return 37;
  }
}

void editorUpdateSyntax(erow *row) {
  row->hl = realloc(row->hl, row->rsize);
  memset(row->hl, HL_NORMAL, row->rsize);

  if (E.syntax == NULL) return;

  char **keywords = E.syntax->keywords;

  char *scs = E.syntax->singleline_comment_start;
  char *mcs = E.syntax->multiline_comment_start;
  char *mce = E.syntax->multiline_comment_end;

  int scs_len = scs ? strlen(scs) : 0;
  int mcs_len = mcs ? strlen(mcs) : 0;
  int mce_len = mce ? strlen(mce) : 0;

  int prev_sep = 1;
  int in_string = 0;
  int in_comment = (row->hl_open_comment);

  int i = 0;
  while (i < row->rsize) {
    char c = row->render[i];
    unsigned char prev_hl = (i > 0) ? row->hl[i - 1] : HL_NORMAL;

    if (scs_len && !in_string && !in_comment) {
      if (!strncmp(&row->render[i], scs, scs_len)) {
        memset(&row->hl[i], HL_COMMENT, row->rsize - i);
        break;
      }
    }

    if (mcs_len && mce_len && !in_string) {
      if (in_comment) {
        row->hl[i] = HL_MLCOMMENT;
        if (!strncmp(&row->render[i], mce, mce_len)) {
          memset(&row->hl[i], HL_MLCOMMENT, mce_len);
          i += mce_len;
          in_comment = 0;
          prev_sep = 1;
          continue;
        } else {
          i++;
          continue;
        }
      } else if (!strncmp(&row->render[i], mcs, mcs_len)) {
        memset(&row->hl[i], HL_MLCOMMENT, mcs_len);
        i += mcs_len;
        in_comment = 1;
        continue;
      }
    }

    if (E.syntax->flags & HL_HIGHLIGHT_STRINGS) {
      if (in_string) {
        row->hl[i] = HL_STRING;
        if (c == '\\' && i + 1 < row->rsize) {
          row->hl[i + 1] = HL_STRING;
          i += 2;
          continue;
        }
        if (c == in_string) in_string = 0;
        i++;
        prev_sep = 1;
        continue;
      } else {
        if (c == '"' || c == '\'') {
          in_string = c;
          row->hl[i] = HL_STRING;
          i++;
          continue;
        }
      }
    }

    if (E.syntax->flags & HL_HIGHLIGHT_NUMBERS) {
      if ((isdigit(c) && (prev_sep || prev_hl == HL_NUMBER)) ||
          (c == '.' && prev_hl == HL_NUMBER)) {
        row->hl[i] = HL_NUMBER;
        i++;
        prev_sep = 0;
        continue;
      }
    }

    if (prev_sep) {
      int j;
      for (j = 0; keywords[j]; j++) {
        int klen = strlen(keywords[j]);
        int kw2 = keywords[j][klen - 1] == '|';
        if (kw2) klen--;

        if (!strncmp(&row->render[i], keywords[j], klen) &&
            is_separator(row->render[i + klen])) {
          memset(&row->hl[i], kw2 ? HL_KEYWORD2 : HL_KEYWORD1, klen);
          i += klen;
          break;
        }
      }
      if (keywords[j] != NULL) {
        prev_sep = 0;
        continue;
      }
    }

    prev_sep = is_separator(c);
    i++;
  }

  int changed = (row->hl_open_comment != in_comment);
  row->hl_open_comment = in_comment;
  if (changed && row->idx + 1 < E.numrows)
    editorUpdateSyntax(&E.row[row->idx + 1]);
}

void editorFreeSyntax() {
    if (E.syntax == NULL) return;

    free(E.syntax->language);

    if (E.syntax->keywords) {
        for (int i = 0; E.syntax->keywords[i]; i++) {
            free(E.syntax->keywords[i]);
        }
        free(E.syntax->keywords);
    }

    free(E.syntax->singleline_comment_start);
    free(E.syntax->multiline_comment_start);
    free(E.syntax->multiline_comment_end);
    free(E.syntax);
    E.syntax = NULL;
}

/**
 * @brief Updates syntax highlighting for rows that were part of a selection
 *        to remove HL_SELECTION highlighting.
 */
void editorUpdateSelectionSyntax() {
  if (E.numrows == 0) return; 
  
  int start_cy = E.selection_start_cy;
  int end_cy = E.selection_end_cy;
  
  // Ensure start is before end for iteration
  if (start_cy > end_cy) {
    int temp_cy = start_cy;
    start_cy = end_cy;
    end_cy = temp_cy;
  }
  
  // Clamp to valid range
  if (start_cy < 0) start_cy = 0;
  if (end_cy >= E.numrows) end_cy = E.numrows - 1;
  
  // Update syntax highlighting for all affected rows
  for (int i = start_cy; i <= end_cy; i++) {
    editorUpdateSyntax(&E.row[i]);
  }
}

/**
 * @brief Deselects the current selection and updates visual highlighting
 */
void editorDeselectSelection() {
  if (E.selection_active) {
    // Store selection coordinates before deactivating
    int start_cy = E.selection_start_cy;
    int end_cy = E.selection_end_cy;

    // Deactivate selection first
    E.selection_active = 0;

    // Normalize coordinates for proper iteration
    if (start_cy > end_cy) {
        int temp = start_cy;
        start_cy = end_cy;
        end_cy = temp;
    }

    // Update syntax for all previously selected rows
    for (int i = start_cy; i <= end_cy; i++) {
        if (i < E.numrows) { // Check bounds
            editorUpdateSyntax(&E.row[i]);
        }
    }
  }
}

void editorSelectSyntaxHighlight() {
  E.syntax = NULL;
  if (E.filename == NULL) return;

  char *ext = strrchr(E.filename, '.');

  DIR *d = opendir("syntax");
  if (!d) return;

  struct dirent *dir;
  while ((dir = readdir(d)) != NULL) {
    if (dir->d_name[0] == '.') continue;

    char filepath[1024];
    snprintf(filepath, sizeof(filepath), "syntax/%s", dir->d_name);

    FILE *fp = fopen(filepath, "r");
    if (!fp) continue;

    fseek(fp, 0, SEEK_END);
    long fsize = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    char *json_string = malloc(fsize + 1);
    fread(json_string, 1, fsize, fp);
    fclose(fp);

    json_string[fsize] = 0;

    cJSON *json = cJSON_Parse(json_string);
    free(json_string);

    if (!json) continue;

    cJSON *filematch = cJSON_GetObjectItem(json, "filematch");
    if (!cJSON_IsArray(filematch)) {
      cJSON_Delete(json);
      continue;
    }

    cJSON *fm;
    cJSON_ArrayForEach(fm, filematch) {
      if (cJSON_IsString(fm) && (fm->valuestring != NULL)) {
        if (ext && !strcmp(ext, fm->valuestring)) {
          E.syntax = malloc(sizeof(struct editorSyntax));
          cJSON *lang = cJSON_GetObjectItem(json, "language");
          E.syntax->language = cJSON_IsString(lang) ? strdup(lang->valuestring) : NULL;

          cJSON *kw = cJSON_GetObjectItem(json, "keywords");
          if (cJSON_IsArray(kw)) {
            int n = cJSON_GetArraySize(kw);
            E.syntax->keywords = malloc(sizeof(char*) * (n + 1));
            int i = 0;
            cJSON *k;
            cJSON_ArrayForEach(k, kw) {
              E.syntax->keywords[i++] = strdup(k->valuestring);
            }
            E.syntax->keywords[i] = NULL;
          } else {
            E.syntax->keywords = NULL;
          }

          cJSON *scs = cJSON_GetObjectItem(json, "singleline_comment_start");
          E.syntax->singleline_comment_start = cJSON_IsString(scs) ? strdup(scs->valuestring) : NULL;

          cJSON *mcs = cJSON_GetObjectItem(json, "multiline_comment_start");
          E.syntax->multiline_comment_start = cJSON_IsString(mcs) ? strdup(mcs->valuestring) : NULL;

          cJSON *mce = cJSON_GetObjectItem(json, "multiline_comment_end");
          E.syntax->multiline_comment_end = cJSON_IsString(mce) ? strdup(mce->valuestring) : NULL;

          cJSON *flags = cJSON_GetObjectItem(json, "flags");
          E.syntax->flags = cJSON_IsNumber(flags) ? flags->valueint : 0;

          cJSON_Delete(json);
          closedir(d);
          return;
        }
      }
    }
    cJSON_Delete(json);
  }
  closedir(d);
}

/* find */

/**
 * @brief Callback function for the search. It is called by editorPrompt.
 *        Searches for the `query` text in the buffer and moves to the next/previous match.
 * @param query The string to search for.
 * @param key The key pressed by the user (to navigate between results).
 */
void editorFindCallback(char *query, int key) {
  static int last_match = -1;
  static int direction = 1;

  // If a previous match was selected, redraw its syntax to remove the highlight
  if (last_match != -1) {
    editorUpdateSyntax(&E.row[last_match]);
  }

  if (key == '\r' || key == '\x1b') {
    last_match = -1;
    direction = 1;
    editorDeselectSelection();
    return;
  } else if (key == ARROW_RIGHT || key == ARROW_DOWN) {
    direction = 1;
  } else if (key == ARROW_LEFT || key == ARROW_UP) {
    direction = -1;
  }
  else if (key == CTRL_KEY('r')) {
    // Trigger Replace-All flow using current query
    if (!query || strlen(query) == 0) {
      editorSetStatusMessage("Enter a search term first, then press Ctrl-R to replace.");
      return;
    }

    // Ask for replacement text
    char *repl = editorPrompt("Replace with: %s (ESC to cancel)", NULL);
    if (!repl) {
      editorSetStatusMessage("Replace cancelled.");
      return;
    }

    // Count occurrences first
    int total = 0;
    for (int i = 0; i < E.numrows; i++) {
      total += editorCountOccurrencesInRow(&E.row[i], query);
    }

    if (total == 0) {
      editorSetStatusMessage("No occurrences of '%s' found.", query);
      free(repl);
      return;
    }

    editorSetStatusMessage("Replace all %d whole-word occurrence(s) of '%s' with '%s'? (y/a)", total, query, repl);
    editorRefreshScreen();
    int confirm = editorReadKey();
    if (confirm == 'y' || confirm == 'Y') {
      editorCreateSnapshot("Replace all");
      int replaced = editorReplaceAllInBuffer(query, repl);
      editorDeselectSelection();
      editorSetStatusMessage("Replaced %d occurrence(s). Press ESC to close search.", replaced);
    } else {
      editorSetStatusMessage("Replace aborted.");
    }
    free(repl);
    return;
  } else { // Any other key resets search
    last_match = -1;
    direction = 1;
  }

  if (last_match == -1) direction = 1;
  int current = last_match;
  if (current == -1) current = E.cy;

  int found = 0;
  for (int i = 0; i < E.numrows; i++) {
    current += direction;
    if (current == -1) current = E.numrows - 1;
    else if (current == E.numrows) current = 0;

    erow *row = &E.row[current];
    char *match = strstr(row->render, query);
    if (match) {
      last_match = current;
      E.cy = current;
      E.cx = editorRowRxToCx(row, match - row->render);
      E.rowoff = E.numrows;

      // Activate selection for the found match
      E.selection_active = 1;
      E.selection_start_cy = current;
      E.selection_end_cy = current;
      E.selection_start_cx = E.cx;
      E.selection_end_cx = E.cx + strlen(query);
      found = 1;
      break;
    }
  }

  // If no match was found from this action, deactivate selection
  if (!found) {
      editorDeselectSelection();
      last_match = -1;
  }
}

void editorFind() {
  int saved_cx = E.cx, saved_cy = E.cy;
  int saved_coloff = E.coloff, saved_rowoff = E.rowoff;

  char *query = editorPrompt("Search: %s (Use ESC/Arrows/Enter)", editorFindCallback);

  if (query) {
    free(query);
  }
  else {
    E.cx = saved_cx; E.cy = saved_cy;
    E.coloff = saved_coloff; E.rowoff = saved_rowoff;
  }
  // Ensure selection is cleared and screen is refreshed when prompt is closed
  editorDeselectSelection();
  editorRefreshScreen();
}

/* append buffer */

/**
 * @brief Appends a string to a dynamic output buffer (append buffer).
 * @param ab The buffer to append to.
 * @param s The string to append.
 * @param len The length of the string.
 */
void abAppend(struct abuf *ab, const char *s, int len) {
  char *new = realloc(ab->b, ab->len + len);
  if (new == NULL) return;
  memcpy(&new[ab->len], s, len);
  ab->b = new;
  ab->len += len;
}

/**
 * @brief Frees the memory of an append buffer.
 * @param ab The buffer to free.
 */
void abFree(struct abuf *ab) { free(ab->b); }

/* output */

/**
 * @brief Calculates the number of columns available for text,
 *        considering the space occupied by line numbers if active.
 * @return The number of columns for the text.
 */
int editorGetTextCols() {
    int text_cols = E.screencols;
    if (E.linenumbers) {
        int max_linenum_digits = 1;
        if (E.numrows > 0) {
            int temp_num = E.numrows;
            while (temp_num /= 10) max_linenum_digits++;
        }
        int linenum_width = max_linenum_digits + 1;
        if (linenum_width < 4) linenum_width = 4;
        text_cols -= linenum_width;
    }
    return text_cols;
}

/**
 * @brief Manages vertical and horizontal scrolling
 *        to keep the cursor visible on the screen.
 */
void editorScroll() {
  E.rx = 0;
  if (E.cy < E.numrows) E.rx = editorRowCxToRx(&E.row[E.cy], E.cx);
  if (E.cy < E.rowoff) E.rowoff = E.cy;
  if (E.cy >= E.rowoff + E.screenrows) E.rowoff = E.cy - E.screenrows + 1;
  int text_cols = editorGetTextCols();
  if (E.rx < E.coloff) E.coloff = E.rx;
  if (E.rx >= E.coloff + text_cols) E.coloff = E.rx - text_cols + 1;
}

/**
 * @brief Draws the visible text rows on the screen.
 * @param ab The append buffer to add the text to be drawn to.
 * @param linenum_width The width of the line number column.
 */
void editorDrawRows(struct abuf *ab) {
    int linenum_width = 0;
    if (E.linenumbers) {
        int max_linenum_digits = (E.numrows > 0) ? (int)floor(log10(E.numrows)) + 1 : 1;
        linenum_width = max_linenum_digits + 1;
        if (linenum_width < 4) linenum_width = 4;
    }

    for (int y = 0; y < E.screenrows; y++) {
        int filerow = y + E.rowoff;
        if (filerow >= E.numrows) {
            // Draw welcome message or tildes for empty lines
            if (E.numrows == 0 && y == E.screenrows / 3) {
                char welcome[80];
                int welcomelen = snprintf(welcome, sizeof(welcome), "Wee editor -- version %s", WEE_VERSION);
                int text_cols = editorGetTextCols();
                if (welcomelen > text_cols) welcomelen = text_cols;
                int padding = (text_cols - welcomelen) / 2;
                if (padding) { abAppend(ab, "~", 1); padding--; }
                while (padding--) abAppend(ab, " ", 1);
                abAppend(ab, welcome, welcomelen);
            } else {
                abAppend(ab, "~", 1);
            }
        } else {
            // Draw line numbers
            if (E.linenumbers) {
                char linenum_buf[16];
                snprintf(linenum_buf, sizeof(linenum_buf), "%*d ", linenum_width - 1, filerow + 1); 
                abAppend(ab, "\x1b[36m", 5); // Cyan color for line numbers
                abAppend(ab, linenum_buf, strlen(linenum_buf));
                abAppend(ab, "\x1b[m", 3);
            }

            erow *row = &E.row[filerow];
            int len = row->rsize - E.coloff;
            if (len < 0) len = 0;
            if (len > E.screencols - linenum_width) len = E.screencols - linenum_width;
            
            char *c = &row->render[E.coloff];
            unsigned char *hl = &row->hl[E.coloff];
            int current_color = -1;

            // Normalize selection coordinates for drawing this row
            int local_start_cx = -1, local_end_cx = -1;
            if (E.selection_active) {
                int norm_start_cx = E.selection_start_cx, norm_start_cy = E.selection_start_cy;
                int norm_end_cx = E.selection_end_cx, norm_end_cy = E.selection_end_cy;

                if (norm_start_cy > norm_end_cy || (norm_start_cy == norm_end_cy && norm_start_cx > norm_end_cx)) {
                    int temp_cx = norm_start_cx, temp_cy = norm_start_cy;
                    norm_start_cx = norm_end_cx; norm_start_cy = norm_end_cy;
                    norm_end_cx = temp_cx; norm_end_cy = temp_cy;
                }

                if (filerow >= norm_start_cy && filerow <= norm_end_cy) {
                    local_start_cx = (filerow == norm_start_cy) ? norm_start_cx : 0;
                    local_end_cx = (filerow == norm_end_cy) ? norm_end_cx : row->size;
                }
            }
            
            for (int j = 0; j < len; j++) {
                int current_char_cx = editorRowRxToCx(row, E.coloff + j);
                int is_selected = (E.selection_active && local_start_cx != -1 &&
                                   current_char_cx >= local_start_cx && current_char_cx < local_end_cx);

                if (is_selected) {
                    if (current_color != 7) {
                        abAppend(ab, "\x1b[7m", 4); // Inverse video
                        current_color = 7;
                    }
                } else {
                    if (current_color == 7) {
                        abAppend(ab, "\x1b[27m", 5); // Reset inverse video
                    }
                    int color = editorSyntaxToColor(hl[j]);
                    if (color != current_color) {
                        char buf[16];
                        snprintf(buf, sizeof(buf), "\x1b[%dm", color);
                        abAppend(ab, buf, strlen(buf));
                        current_color = color;
                    }
                }
                
                // Render character
                abAppend(ab, &c[j], 1);
            }
            
            // Reset colors at the end of the line
            if (current_color == 7) abAppend(ab, "\x1b[27m", 5);
            abAppend(ab, "\x1b[39m", 5);
        }
        abAppend(ab, "\x1b[K", 3);
        abAppend(ab, "\r\n", 2);
    }
}

/**
 * @brief Draws the status bar at the bottom of the screen.
 * @param ab The append buffer to add the text to be drawn to.
 */
void editorDrawStatusBar(struct abuf *ab) {
  // Status bar senza inversione globale: evidenziamo solo [nomefile]
  char *basename = E.filename ? strrchr(E.filename, '/') : NULL;
  if (basename) {
    basename++;
  } else {
    basename = E.filename;
  }

  const char *name = basename ? basename : "No Name";
  char status[256];
  int len = 0;

  // Spazio iniziale per separare dal bordo
  abAppend(ab, " ", 1);
  len += 1;

  // Blocco [nomefile] con sfondo cyan e testo nero per leggibilità
  // \x1b[30;46m = fg nero, bg cyan
  abAppend(ab, "\x1b[30;46m", 9);
  abAppend(ab, "[", 1);
  abAppend(ab, name, strlen(name));
  abAppend(ab, "]", 1);
  abAppend(ab, "\x1b[m", 3); // reset
  len += 2 + strlen(name);

  // Informazioni aggiuntive a sinistra
  int len2 = snprintf(status, sizeof(status), " - %d lines %s", E.numrows, E.dirty ? "(modified)" : "");
  abAppend(ab, status, len2);
  len += len2;

  // Stato a destra
  char rstatus[80];
  int rlen = snprintf(rstatus, sizeof(rstatus), "%s | %d/%d", E.syntax ? E.syntax->language : "no ft", E.cy + 1, E.numrows);
  
  while (len < E.screencols) {
    if (E.screencols - len == rlen) {
      abAppend(ab, rstatus, rlen);
      break;
    } else {
      abAppend(ab, " ", 1);
      len++;
    }
  }
  abAppend(ab, "\x1b[m", 3);
  abAppend(ab, "\r\n", 2);
}

/**
 * @brief Draws the message bar, below the status bar.
 * @param ab The append buffer to add the text to be drawn to.
 */
void editorDrawMessageBar(struct abuf *ab) {
  abAppend(ab, "\x1b[K", 3);
  int msglen = strlen(E.statusmsg);
  if (msglen > E.screencols) msglen = E.screencols;
  if (msglen && time(NULL) - E.statusmsg_time < 5)
    abAppend(ab, E.statusmsg, msglen);
}

/**
 * @brief Refreshes the entire screen, redrawing all components.
 */
void editorRefreshScreen() {
  editorScroll();
  struct abuf ab = ABUF_INIT;
  abAppend(&ab, "\x1b[?25l", 6);
  abAppend(&ab, "\x1b[H", 3);
  editorDrawRows(&ab);
  editorDrawStatusBar(&ab);
  editorDrawMessageBar(&ab);
  char buf[32];
  int linenum_width = 0;
  if (E.linenumbers) {
      int max_linenum_digits = 1;
      if (E.numrows > 0) {
          int temp_num = E.numrows;
          while (temp_num /= 10) max_linenum_digits++;
      }
      linenum_width = max_linenum_digits + 1;
      if (linenum_width < 4) linenum_width = 4;
  }
  snprintf(buf, sizeof(buf), "\x1b[%d;%dH", (E.cy - E.rowoff) + 1,
           (E.rx - E.coloff) + 1 + linenum_width);
  abAppend(&ab, buf, strlen(buf));
  abAppend(&ab, "\x1b[?25h", 6);
  write(STDOUT_FILENO, ab.b, ab.len);
  abFree(&ab);
}

/**
 * @brief Sets a message to be displayed in the message bar.
 * @param fmt The format string (like in printf).
 * @param ... Variable arguments for the format string.
 */
void editorSetStatusMessage(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(E.statusmsg, sizeof(E.statusmsg), fmt, ap);
  va_end(ap);
  E.statusmsg_time = time(NULL);
}

/* input */

/**
 * @brief Displays a prompt in the message bar and waits for user input.
 * @param prompt The prompt string to display.
 * @param callback An optional function to call on each keypress.
 * @return The string entered by the user (must be freed by the caller), or NULL if canceled.
 */
char *editorPrompt(char *prompt, void (*callback)(char *, int)) {
  size_t bufsize = 128;
  char *buf = malloc(bufsize);
  size_t buflen = 0;
  buf[0] = '\0';
  while (1) {
    editorSetStatusMessage(prompt, buf);
    editorRefreshScreen();
    int c = editorReadKey();
    if (c == DEL_KEY || c == CTRL_KEY('h') || c == BACKSPACE) {
      if (buflen != 0) buf[--buflen] = '\0';
    } else if (c == '\x1b') {
      editorSetStatusMessage("");
      if (callback) callback(buf, c);
      free(buf);
      return NULL;
    } else if (c == '\r') {
      if (buflen != 0) {
        editorSetStatusMessage("");
        if (callback) callback(buf, c);
        return buf;
      }
    } else if (!iscntrl(c) && c < 128) {
      if (buflen == bufsize - 1) {
        bufsize *= 2;
        buf = realloc(buf, bufsize);
      }
      buf[buflen++] = c;
      buf[buflen] = '\0';
    }
    if (callback) callback(buf, c);
  }
}

/**
 * @brief Moves the cursor based on the pressed key (arrows).
 * @param key The pressed key (e.g., ARROW_UP).
 */
void editorMoveCursor(int key) {
  erow *row = (E.cy >= E.numrows) ? NULL : &E.row[E.cy];
  switch (key) {
    case ARROW_LEFT:
      if (E.cx != 0) E.cx--;
      else if (E.cy > 0) { E.cy--; E.cx = E.row[E.cy].size; }
      break;
    case ARROW_RIGHT:
      if (row && E.cx < row->size) E.cx++;
      else if (row && E.cx == row->size) { E.cy++; E.cx = 0; }
      break;
    case ARROW_UP:
      if (E.cy != 0) E.cy--;
      break;
    case ARROW_DOWN:
      if (E.cy < E.numrows) E.cy++;
      break;
  }
  row = (E.cy >= E.numrows) ? NULL : &E.row[E.cy];
  int rowlen = row ? row->size : 0;
  if (E.cx > rowlen) E.cx = rowlen;
}

/**
 * @brief Processes a single keypress from the user and calls the corresponding function.
 */
void editorProcessKeypress() {
  static int quit_times = WEE_QUIT_TIMES;
  int c = editorReadKey();

  if (E.mode == SELECTION_MODE) {
    switch (c) {
      case '\x1b': // ESC - Cancel selection
        editorDeselectSelection();
        E.mode = NORMAL_MODE;
        editorSetStatusMessage("Selection cancelled.");
        editorRefreshScreen();
        break;
      case '\t':
        editorIndentSelection();
        break;
      case BACKSPACE:
        editorUnindentSelection();
        break;
      case DEL_KEY: // Delete selection
        editorCreateSnapshot("Delete selection");
        editorDelCharSelection();
        E.mode = NORMAL_MODE;
        editorSetStatusMessage("Selection deleted.");
        editorRefreshScreen(); // Add this
        break;
      case ARROW_UP:
      case ARROW_DOWN:
      case ARROW_LEFT:
      case ARROW_RIGHT:
        editorMoveSelection(c);
        break;
      case CTRL_KEY('w'): // Copy selection
        editorCopySelection(); // This already sets E.selection_active = 0
        E.mode = NORMAL_MODE;
        // editorSetStatusMessage("Selection copied."); // Redundant, editorCopySelection sets it
        editorRefreshScreen(); // Add this
        break;
      case CTRL_KEY('k'): // Cut selection
        editorSetStatusMessage("Mode: SELECTION_MODE. Cutting selection.");
        editorCreateSnapshot("Cut selection");
        editorCutSelection();
        E.mode = NORMAL_MODE;
        editorSetStatusMessage("Selection cut.");
        editorRefreshScreen(); // Add this
        break;
      case SHIFT_TAB:
        editorSelectInsideDelims();
        break;
      default:
        if (!iscntrl(c) && c < 128) { // Check if it's a printable ASCII character
          editorCreateSnapshot("Replace selection");
          editorDelCharSelection(); // Delete the selected text (sets E.selection_active = 0)
          editorInsertChar(c);      // Insert the new character
          E.mode = NORMAL_MODE;     // Exit selection mode
          editorSetStatusMessage(""); // Clear status message
          editorRefreshScreen();    // Refresh screen
        }
        break;
    }
  } else { // NORMAL_MODE
    switch (c) {
      case '\r': 
        editorCreateSnapshot("Insert newline");
        editorInsertNewline(); 
        break;
      case '\t':
        for (int i = 0; i < 4; i++) editorInsertChar(' ');
        break;
      case CTRL_KEY('q'):
        if (E.dirty && quit_times > 0) {
          editorSetStatusMessage("WARNING!!! File has unsaved changes. "
                                 "Press Ctrl-Q %d more times to quit.",
                                 quit_times);
          quit_times--;
          return;
        }
        write(STDOUT_FILENO, "\x1b[2J", 4);
        write(STDOUT_FILENO, "\x1b[H", 3);
        exit(0);
        break;
      case CTRL_KEY('s'): editorSave(); break;
      case CTRL_KEY('y'): editorSaveAs(); break;
      case CTRL_KEY('w'):
        editorCopyLine(); // Only copy line in normal mode
        break;
      case CTRL_KEY('k'):
        if (E.selection_active) { // If a selection is active, cut the selection
          editorSetStatusMessage("Mode: NORMAL_MODE. Selection active. Cutting selection.");
          editorCreateSnapshot("Cut selection");
          editorCutSelection();
          E.mode = NORMAL_MODE; // Exit selection mode after cutting
          editorSetStatusMessage("Selection cut.");
        } else { // Otherwise, cut the current line
          editorSetStatusMessage("Mode: NORMAL_MODE. No selection active. Cutting line.");
          editorCreateSnapshot("Cut line");
          editorCutLine();
        }
        break;
      case CTRL_KEY('u'): 
        editorCreateSnapshot("Paste");
        editorPaste(); 
        break;
      case CTRL_KEY('n'): E.linenumbers = !E.linenumbers; break;
      case CTRL_KEY('t'): editorNewFile(); break;
      case CTRL_KEY('g'): editorShowHelp(); break;
      case CTRL_KEY('f'): editorFind(); break;
      case CTRL_KEY('j'): editorJumpToLine(); break;
      case CTRL_KEY('z'): editorUndo(); break;
      case CTRL_KEY('r'): editorRedo(); break;
      case HOME_KEY:
      case ALT_B:
        E.cx = 0;
        break;
      case END_KEY:
      case ALT_E:
        if (E.cy < E.numrows)
          E.cx = E.row[E.cy].size;
        break;
      case ALT_R:
        editorSelectRowText();
        break;
      case BACKSPACE:
      case CTRL_KEY('h'):
      case DEL_KEY: {
        // Smart outdent su BACKSPACE quando il cursore è sul primo carattere non-spazio
        if (c != DEL_KEY && E.cy < E.numrows) {
          erow *row = &E.row[E.cy];
          int first_ns = 0;
          while (first_ns < row->size && row->chars[first_ns] == ' ') first_ns++;
          if (E.cx == first_ns && first_ns > 0) {
            int target = (first_ns - 1) / WEE_TAB_STOP * WEE_TAB_STOP; // tab stop precedente
            int to_delete = first_ns - target;
            // Elimina 'to_delete' spazi dall'inizio della riga
            for (int i = 0; i < to_delete; i++) {
              if (row->size > 0 && row->chars[0] == ' ') {
                editorRowDelChar(row, 0);
              }
            }
            E.cx = target;
            E.dirty++;
            break; // Non eseguire la cancellazione standard del carattere
          }
        }
        editorCreateSnapshot("Delete character");
        if (c == DEL_KEY) editorMoveCursor(ARROW_RIGHT);
        editorDelChar();
        break;
      }
      case PAGE_UP:
      case PAGE_DOWN:
        {
          if (c == PAGE_UP) E.cy = E.rowoff;
          else if (c == PAGE_DOWN) {
            E.cy = E.rowoff + E.screenrows - 1;
            if (E.cy > E.numrows) E.cy = E.numrows;
          }
          int times = E.screenrows;
          while (times--) editorMoveCursor(c == PAGE_UP ? ARROW_UP : ARROW_DOWN);
        } break;
      case ARROW_UP:
      case ARROW_DOWN:
      case ARROW_LEFT:
      case ARROW_RIGHT:
        editorMoveCursor(c);
        break;
      case SHIFT_UP:
        editorQuickSelectFullLine(-1);
        break;
      case SHIFT_DOWN:
        editorQuickSelectFullLine(1);
        break;
      case SHIFT_LEFT:
        editorQuickSelectChar(-1);
        break;
      case SHIFT_RIGHT:
        editorQuickSelectChar(1);
        break;
      case SHIFT_TAB:
        editorSelectInsideDelims();
        break;
      case CTRL_KEY('o'): {
        char *path = editorFileBrowser(".");
        if (path) {
          editorOpen(path);
          free(path);
        }
        break;
      }
      case CTRL_KEY('l'):
        break;
      case '\x1b': // ESC key
        if (E.selection_active) {
          // Se c'è una selezione attiva, entra in SELECTION_MODE
          E.mode = SELECTION_MODE;
          editorSetStatusMessage("Entered SELECTION_MODE. Selection ready for operations.");
        }
        // Se non c'è selezione attiva, ESC non fa nulla in NORMAL_MODE
        break;
      case CTRL_KEY('b'):
        E.selection_start_cx = E.cx;
        E.selection_start_cy = E.cy;
        E.selection_end_cx = E.cx; // Initialize end to start
        E.selection_end_cy = E.cy; // Initialize end to start
        E.selection_active = 1;
        editorSetStatusMessage("Selection start set");
        break;
      case CTRL_KEY('e'):
        E.selection_end_cx = E.cx;
        E.selection_end_cy = E.cy;
        editorSetStatusMessage("Selection end set. Entering SELECTION_MODE.");
        E.mode = SELECTION_MODE; // Enter selection mode
        break;
      case CTRL_KEY('a'): // Select all text
        if (E.numrows > 0) {
          E.selection_start_cx = 0;
          E.selection_start_cy = 0;
          E.selection_end_cx = E.row[E.numrows - 1].size;
          E.selection_end_cy = E.numrows - 1;
          E.selection_active = 1;
          E.mode = SELECTION_MODE;
          editorSetStatusMessage("All text selected.");
        } else {
          editorSetStatusMessage("No text to select.");
        }
        break;
      default:
        if (E.selection_active && !iscntrl(c) && c < 128) { // If a selection is active (Ctrl+B pressed, but not Ctrl+E) and a printable char is typed
          editorSetStatusMessage("Selection cancelled (typed character). Deleting selection.");
          editorCreateSnapshot("Replace selection");
          editorDelCharSelection(); // Delete the selected text
          editorInsertChar(c);      // Insert the new character
          E.mode = NORMAL_MODE;     // Exit selection mode
        } else if (E.selection_active) { // If a selection is active and a non-printable char is typed (e.g., arrow keys)
            // Do nothing, allow cursor movement within selection mode
        } else { // No selection active, insert character normally
            // Crea snapshot per il typing (raggruppato per tempo)
            static time_t last_typing_time = 0;
            time_t now = time(NULL);
            if (now - last_typing_time > 2) { // Nuova sessione di typing
                editorCreateSnapshot("Typing");
            }
            last_typing_time = now;
            editorInsertChar(c);
        }
        break;
    }
  }
  quit_times = WEE_QUIT_TIMES;
}

void editorIndentSelection() {
  if (!E.selection_active) return;
  int start_cy = E.selection_start_cy;
  int end_cy = E.selection_end_cy;

  if (start_cy > end_cy) {
    int temp = start_cy;
    start_cy = end_cy;
    end_cy = temp;
  }

  for (int i = start_cy; i <= end_cy; i++) {
    erow *row = &E.row[i];
    for (int j = 0; j < WEE_TAB_STOP; j++) {
      editorRowInsertChar(row, 0, ' ');
    }
  }

  E.selection_start_cx += WEE_TAB_STOP;
  E.selection_end_cx += WEE_TAB_STOP;

  E.dirty++;
}

void editorUnindentSelection() {
  if (!E.selection_active) return;
  int start_cy = E.selection_start_cy;
  int end_cy = E.selection_end_cy;

  if (start_cy > end_cy) {
    int temp = start_cy;
    start_cy = end_cy;
    end_cy = temp;
  }

  for (int i = start_cy; i <= end_cy; i++) {
    erow *row = &E.row[i];
    int chars_deleted_count = 0;
    for (int j = 0; j < WEE_TAB_STOP; j++) {
      if (row->size > 0 && row->chars[0] == ' ') {
        editorRowDelChar(row, 0);
        chars_deleted_count++;
      } else {
        break;
      }
    }
    if (i == E.selection_start_cy) {
        E.selection_start_cx -= chars_deleted_count;
        if (E.selection_start_cx < 0) E.selection_start_cx = 0;
    }
    if (i == E.selection_end_cy) {
        E.selection_end_cx -= chars_deleted_count;
        if (E.selection_end_cx < 0) E.selection_end_cx = 0;
    }
  }
  E.dirty++;
}

void editorMoveSelectionLeft() {
  if (!E.selection_active) return;
  
  int start_cx = E.selection_start_cx;
  int start_cy = E.selection_start_cy;
  int end_cx = E.selection_end_cx;
  int end_cy = E.selection_end_cy;

  // Normalizza le coordinate
  if (start_cy > end_cy || (start_cy == end_cy && start_cx > end_cx)) {
    int temp_cx = start_cx;
    int temp_cy = start_cy;
    start_cx = end_cx;
    start_cy = end_cy;
    end_cx = temp_cx;
    end_cy = temp_cy;
  }

  // Per selezioni su una sola riga
  if (start_cy == end_cy) {
    erow *row = &E.row[start_cy];
    // Rimuovi uno spazio prima della selezione, se presente
    if (start_cx > 0 && row->chars[start_cx - 1] == ' ') {
      editorRowDelChar(row, start_cx - 1);
      
      // Aggiorna le coordinate della selezione
      E.selection_start_cx--;
      E.selection_end_cx--;
    }
  } else {
    // Per selezioni su più righe: rimuovi spazio prima di ogni parte della selezione
    for (int i = start_cy; i <= end_cy; i++) {
      erow *row = &E.row[i];
      if (i == start_cy) {
        // Prima riga: rimuovi spazio prima di start_cx se presente
        if (start_cx > 0 && row->chars[start_cx - 1] == ' ') {
          editorRowDelChar(row, start_cx - 1);
          E.selection_start_cx--;
        }
      } else {
        // Altre righe: rimuovi spazio all'inizio se presente
        if (row->size > 0 && row->chars[0] == ' ') {
          editorRowDelChar(row, 0);
          if (i == end_cy) {
            E.selection_end_cx--;
            if (E.selection_end_cx < 0) E.selection_end_cx = 0;
          }
        }
      }
    }
  }
  
  E.dirty++;
}

void editorMoveSelectionRight() {
  if (!E.selection_active) return;
  
  int start_cx = E.selection_start_cx;
  int start_cy = E.selection_start_cy;
  int end_cx = E.selection_end_cx;
  int end_cy = E.selection_end_cy;

  // Normalizza le coordinate
  if (start_cy > end_cy || (start_cy == end_cy && start_cx > end_cx)) {
    int temp_cx = start_cx;
    int temp_cy = start_cy;
    start_cx = end_cx;
    start_cy = end_cy;
    end_cx = temp_cx;
    end_cy = temp_cy;
  }

  // Per selezioni su una sola riga
  if (start_cy == end_cy) {
    erow *row = &E.row[start_cy];
    // Inserisci uno spazio prima della selezione
    editorRowInsertChar(row, start_cx, ' ');
    
    // Aggiorna le coordinate della selezione
    E.selection_start_cx++;
    E.selection_end_cx++;
  } else {
    // Per selezioni su più righe: inserisci spazio all'inizio di ogni riga
    for (int i = start_cy; i <= end_cy; i++) {
      erow *row = &E.row[i];
      if (i == start_cy) {
        // Prima riga: inserisci spazio alla posizione start_cx
        editorRowInsertChar(row, start_cx, ' ');
        E.selection_start_cx++;
      } else if (i == end_cy) {
        // Ultima riga: inserisci spazio all'inizio
        editorRowInsertChar(row, 0, ' ');
        E.selection_end_cx++;
      } else {
        // Righe intermedie: inserisci spazio all'inizio
        editorRowInsertChar(row, 0, ' ');
      }
    }
  }
  
  E.dirty++;
}

/**
 * @brief Verifica se la selezione può essere spostata a sinistra
 *        Movimento a sinistra = rimuovere spazio prima della selezione
 * @return 1 se ci sono spazi da rimuovere prima della selezione
 */
int editorCanMoveSelectionLeft() {
  if (!E.selection_active) return 0;
  
  int start_cx = E.selection_start_cx;
  int start_cy = E.selection_start_cy;
  int end_cx = E.selection_end_cx;
  int end_cy = E.selection_end_cy;

  // Normalizza le coordinate
  if (start_cy > end_cy || (start_cy == end_cy && start_cx > end_cx)) {
    int temp_cx = start_cx;
    int temp_cy = start_cy;
    start_cx = end_cx;
    start_cy = end_cy;
    end_cx = temp_cx;
    end_cy = temp_cy;
  }

  // Per selezioni su una sola riga
  if (start_cy == end_cy) {
    erow *row = &E.row[start_cy];
    // Controlla se c'è uno spazio prima della selezione
    return (start_cx > 0 && row->chars[start_cx - 1] == ' ');
  } else {
    // Per selezioni su più righe: controlla che TUTTE le righe abbiano spazi da rimuovere
    for (int i = start_cy; i <= end_cy; i++) {
      erow *row = &E.row[i];
      if (i == start_cy) {
        // Prima riga: controlla spazio prima di start_cx
        if (!(start_cx > 0 && row->chars[start_cx - 1] == ' ')) {
          return 0; // Non tutte hanno spazi
        }
      } else {
        // Altre righe: controlla spazio all'inizio
        if (!(row->size > 0 && row->chars[0] == ' ')) {
          return 0; // Non tutte hanno spazi
        }
      }
    }
    return 1; // Tutte hanno spazi
  }
}

/**
 * @brief Verifica se la selezione può essere spostata a destra
 *        Movimento a destra = inserire spazio prima della selezione (sempre possibile)
 * @return 1 sempre se selezione attiva
 */
int editorCanMoveSelectionRight() {
  return E.selection_active;
}

/**
 * @brief Verifica se la selezione comprende righe complete
 *        Per il movimento verticale, la selezione deve coprire righe intere
 * @return 1 se la selezione comprende righe complete, 0 altrimenti
 */
int editorIsSelectionFullLines() {
  if (!E.selection_active) return 0;
  
  int start_cx = E.selection_start_cx;
  int start_cy = E.selection_start_cy;
  int end_cx = E.selection_end_cx;
  int end_cy = E.selection_end_cy;

  // Normalizza le coordinate
  if (start_cy > end_cy || (start_cy == end_cy && start_cx > end_cx)) {
    int temp_cx = start_cx;
    int temp_cy = start_cy;
    start_cx = end_cx;
    start_cy = end_cy;
    end_cx = temp_cx;
    end_cy = temp_cy;
  }

  // Se selezione su una sola riga
  if (start_cy == end_cy) {
    // Deve selezionare tutta la riga
    return (start_cx == 0 && end_cx == E.row[start_cy].size);
  }

  // Se selezione su più righe
  // Prima riga: deve iniziare da 0
  if (start_cx != 0) return 0;
  
  // Ultima riga: deve finire alla fine della riga
  if (end_cx != E.row[end_cy].size) return 0;
  
  return 1;
}

/**
 * @brief Verifica se la selezione può essere spostata verticalmente
 *        Combina i controlli per le righe complete e i limiti
 * @return 1 se può essere spostata verticalmente, 0 altrimenti
 */
int editorCanMoveSelectionVertical() {
  return editorIsSelectionFullLines();
}

void editorMoveSelection(int key) {
  switch (key) {
    case ARROW_LEFT: {
        if (!editorCanMoveSelectionLeft()) {
            editorSetStatusMessage("Cannot move selection left - not enough spaces");
            return;
        }
        editorMoveSelectionLeft();
        editorSetStatusMessage("Selection moved left");
      break;
    }
    case ARROW_RIGHT: {
        if (!editorCanMoveSelectionRight()) {
            editorSetStatusMessage("Cannot move selection right");
            return;
        }
        editorMoveSelectionRight();
        editorSetStatusMessage("Selection moved right");
      break;
    }
    case ARROW_UP: {
      if (!editorCanMoveSelectionVertical()) {
          editorSetStatusMessage("Cannot move selection up - selection must be full lines");
          return;
      }
      
      // Get normalized coordinates for bounds checking
      int norm_start_cy = E.selection_start_cy;
      int norm_end_cy = E.selection_end_cy;
      if (norm_start_cy > norm_end_cy) {
          int temp = norm_start_cy;
          norm_start_cy = norm_end_cy;
          norm_end_cy = temp;
      }
      
      if (norm_start_cy == 0) {
          editorSetStatusMessage("Cannot move selection up - already at top");
          return;
      }
      
      // Safety checks
      if (norm_end_cy >= E.numrows || norm_start_cy < 0) return;

      int sel_height = norm_end_cy - norm_start_cy + 1;

      // Store selection temporarily
      erow *temp_selected_block = malloc(sizeof(erow) * sel_height);
      if (!temp_selected_block) {
        editorSetStatusMessage("Memory allocation failed");
        return;
      }
      
      for (int i = 0; i < sel_height; i++) {
        temp_selected_block[i] = E.row[norm_start_cy + i];
      }

      // Move the line above the selection to where the selection ends
      E.row[norm_end_cy] = E.row[norm_start_cy - 1];

      // Move the selection up one position
      for (int i = 0; i < sel_height; i++) {
        E.row[norm_start_cy - 1 + i] = temp_selected_block[i];
      }
      
      free(temp_selected_block);

      // Update row indices and rendering
      for (int i = norm_start_cy - 1; i <= norm_end_cy; i++) {
        E.row[i].idx = i;
        editorUpdateRow(&E.row[i]);
      }

      // Update BOTH anchor and cursor coordinates (they both move up by 1)
      E.selection_start_cy--;
      E.selection_end_cy--;

      // Move the actual cursor up too
      if (E.cy > 0) E.cy--;

      E.dirty++;
      editorSetStatusMessage("Selection moved up");
      break;
    }
    case ARROW_DOWN: {
      if (!editorCanMoveSelectionVertical()) {
          editorSetStatusMessage("Cannot move selection down - selection must be full lines");
          return;
      }
      
      // Get normalized coordinates for bounds checking
      int norm_start_cy = E.selection_start_cy;
      int norm_end_cy = E.selection_end_cy;
      if (norm_start_cy > norm_end_cy) {
          int temp = norm_start_cy;
          norm_start_cy = norm_end_cy;
          norm_end_cy = temp;
      }
      
      if (norm_end_cy >= E.numrows - 1) {
          editorSetStatusMessage("Cannot move selection down - already at bottom");
          return;
      }
      
      // Safety checks
      if (norm_start_cy < 0 || norm_end_cy + 1 >= E.numrows) return;

      int sel_height = norm_end_cy - norm_start_cy + 1;

      // Store selection temporarily
      erow *temp_selected_block = malloc(sizeof(erow) * sel_height);
      if (!temp_selected_block) {
        editorSetStatusMessage("Memory allocation failed");
        return;
      }
      
      for (int i = 0; i < sel_height; i++) {
        temp_selected_block[i] = E.row[norm_start_cy + i];
      }

      // Move the line below the selection to where the selection starts
      E.row[norm_start_cy] = E.row[norm_end_cy + 1];

      // Move the selection down one position
      for (int i = 0; i < sel_height; i++) {
        E.row[norm_start_cy + 1 + i] = temp_selected_block[i];
      }
      
      free(temp_selected_block);

      // Update row indices and rendering
      for (int i = norm_start_cy; i <= norm_end_cy + 1; i++) {
        E.row[i].idx = i;
        editorUpdateRow(&E.row[i]);
      }

      // Update BOTH anchor and cursor coordinates (they both move down by 1)
      E.selection_start_cy++;
      E.selection_end_cy++;

      // Move the actual cursor down too
      if (E.cy < E.numrows - 1) E.cy++;

      E.dirty++;
      editorSetStatusMessage("Selection moved down");
      break;
    }
  }
}

void editorJumpToLine() {
  char *line_str = editorPrompt("Go to line: %s (ESC to cancel)", NULL);
  if (line_str == NULL) {
    editorSetStatusMessage("Jump cancelled.");
    return;
  }

  int target_line = atoi(line_str);
  free(line_str);

  if (target_line <= 0 || target_line > E.numrows) {
    editorSetStatusMessage("Invalid line number: %d. Total lines: %d.", target_line, E.numrows);
    return;
  }

  E.cy = target_line - 1;
  E.cx = 0;

  editorScroll();
  editorSetStatusMessage("Jumped to line %d.", target_line);
}

/* Nuovo sistema Undo/Redo basato su snapshot */

/**
 * @brief Crea una copia profonda di una riga
 */
erow* editorCopyRow(erow *src) {
    erow *dst = malloc(sizeof(erow));
    dst->idx = src->idx;
    dst->size = src->size;
    dst->chars = malloc(src->size + 1);
    memcpy(dst->chars, src->chars, src->size + 1);
    
    dst->rsize = src->rsize;
    dst->render = malloc(src->rsize + 1);
    memcpy(dst->render, src->render, src->rsize + 1);
    
    dst->hl = malloc(src->rsize);
    memcpy(dst->hl, src->hl, src->rsize);
    
    dst->hl_open_comment = src->hl_open_comment;
    return dst;
}

/**
 * @brief Crea uno snapshot dello stato attuale dell'editor
 */
struct EditorSnapshot* editorCopyCurrentState(const char *description) {
    struct EditorSnapshot *snap = malloc(sizeof(struct EditorSnapshot));
    
    // Copia il contenuto del file
    snap->numrows = E.numrows;
    if (E.numrows > 0) {
        snap->rows = malloc(sizeof(erow) * E.numrows);
        for (int i = 0; i < E.numrows; i++) {
            snap->rows[i] = *editorCopyRow(&E.row[i]);
        }
    } else {
        snap->rows = NULL;
    }
    
    // Copia posizione cursore
    snap->cx = E.cx;
    snap->cy = E.cy;
    snap->rowoff = E.rowoff;
    snap->coloff = E.coloff;
    
    // Copia stato selezione
    snap->selection_active = E.selection_active;
    snap->selection_start_cx = E.selection_start_cx;
    snap->selection_start_cy = E.selection_start_cy;
    snap->selection_end_cx = E.selection_end_cx;
    snap->selection_end_cy = E.selection_end_cy;
    
    // Metadati
    snap->timestamp = time(NULL);
    snap->description = strdup(description);
    snap->prev = NULL;
    snap->next = NULL;
    
    return snap;
}

/**
 * @brief Libera la memoria di uno snapshot
 */
void editorFreeSnapshot(struct EditorSnapshot *snap) {
    if (!snap) return;
    
    if (snap->rows) {
        for (int i = 0; i < snap->numrows; i++) {
            editorFreeRow(&snap->rows[i]);
        }
        free(snap->rows);
    }
    
    free(snap->description);
    free(snap);
}

/**
 * @brief Ripristina lo stato dell'editor da uno snapshot
 */
void editorRestoreSnapshot(struct EditorSnapshot *snap) {
    if (!snap) return;
    
    // Libera il contenuto attuale
    for (int i = 0; i < E.numrows; i++) {
        editorFreeRow(&E.row[i]);
    }
    free(E.row);
    
    // Ripristina il contenuto
    E.numrows = snap->numrows;
    if (snap->numrows > 0) {
        E.row = malloc(sizeof(erow) * snap->numrows);
        for (int i = 0; i < snap->numrows; i++) {
            E.row[i] = *editorCopyRow(&snap->rows[i]);
        }
    } else {
        E.row = NULL;
    }
    
    // Ripristina posizione cursore
    E.cx = snap->cx;
    E.cy = snap->cy;
    E.rowoff = snap->rowoff;
    E.coloff = snap->coloff;
    
    // Ripristina stato selezione
    E.selection_active = snap->selection_active;
    E.selection_start_cx = snap->selection_start_cx;
    E.selection_start_cy = snap->selection_start_cy;
    E.selection_end_cx = snap->selection_end_cx;
    E.selection_end_cy = snap->selection_end_cy;
    
    E.dirty++;
}

/**
 * @brief Crea uno snapshot prima di un'operazione
 */
void editorCreateSnapshot(const char *description) {
    time_t now = time(NULL);
    
    // Evita snapshot troppo frequenti (meno di 1 secondo)
    if (now - E.undo_system.last_snapshot_time < 1 && E.undo_system.current) {
        return;
    }
    
    struct EditorSnapshot *snap = editorCopyCurrentState(description);
    
    if (E.undo_system.current) {
        // Rimuovi tutto ciò che segue il punto attuale (per gestire il branching)
        struct EditorSnapshot *next = E.undo_system.current->next;
        while (next) {
            struct EditorSnapshot *temp = next->next;
            editorFreeSnapshot(next);
            next = temp;
            E.undo_system.current_count--;
        }
        
        // Aggiungi il nuovo snapshot
        E.undo_system.current->next = snap;
        snap->prev = E.undo_system.current;
    } else {
        // Primo snapshot
        E.undo_system.head = snap;
    }
    
    E.undo_system.current = snap;
    E.undo_system.current_count++;
    E.undo_system.last_snapshot_time = now;
    
    // Gestisci il limite di snapshot
    if (E.undo_system.current_count > E.undo_system.max_snapshots) {
        // Rimuovi il primo snapshot
        struct EditorSnapshot *old_head = E.undo_system.head;
        E.undo_system.head = old_head->next;
        if (E.undo_system.head) {
            E.undo_system.head->prev = NULL;
        }
        editorFreeSnapshot(old_head);
        E.undo_system.current_count--;
    }
}

/**
 * @brief Pulisce tutto il sistema undo
 */
void editorClearUndoSystem() {
    struct EditorSnapshot *current = E.undo_system.head;
    while (current) {
        struct EditorSnapshot *next = current->next;
        editorFreeSnapshot(current);
        current = next;
    }
    
    E.undo_system.head = NULL;
    E.undo_system.current = NULL;
    E.undo_system.current_count = 0;
    E.undo_system.last_snapshot_time = 0;
}

/**
 * @brief Funzione Undo - torna al snapshot precedente
 */
void editorUndo() {
    if (!E.undo_system.current || !E.undo_system.current->prev) {
        editorSetStatusMessage("Nothing to undo");
        return;
    }
    
    E.undo_system.current = E.undo_system.current->prev;
    editorRestoreSnapshot(E.undo_system.current);
    editorSetStatusMessage("Undo: %s", E.undo_system.current->description);
}

/**
 * @brief Funzione Redo - va al snapshot successivo
 */
void editorRedo() {
    if (!E.undo_system.current || !E.undo_system.current->next) {
        editorSetStatusMessage("Nothing to redo");
        return;
    }
    
    E.undo_system.current = E.undo_system.current->next;
    editorRestoreSnapshot(E.undo_system.current);
    editorSetStatusMessage("Redo: %s", E.undo_system.current->description);
}

/**
 * @brief Seleziona automaticamente il testo della riga corrente
 *        dalla prima lettera all'ultima, ignorando spazi iniziali e finali
 */
void editorSelectRowText() {
    if (E.cy >= E.numrows) {
        editorSetStatusMessage("No line to select");
        return;
    }
    
    erow *row = &E.row[E.cy];
    
    // Se la riga è vuota
    if (row->size == 0) {
        editorSetStatusMessage("Empty line - nothing to select");
        return;
    }
    
    // Trova la prima lettera (non spazio)
    int start_cx = 0;
    while (start_cx < row->size && isspace(row->chars[start_cx])) {
        start_cx++;
    }
    
    // Se tutta la riga è fatta di spazi
    if (start_cx >= row->size) {
        editorSetStatusMessage("Line contains only whitespace - nothing to select");
        return;
    }
    
    // Trova l'ultima lettera (non spazio)
    int end_cx = row->size - 1;
    while (end_cx >= 0 && isspace(row->chars[end_cx])) {
        end_cx--;
    }
    
    // end_cx ora punta all'ultimo carattere non-spazio
    // Per la selezione, vogliamo che punti DOPO l'ultimo carattere
    end_cx++;
    
    // Imposta la selezione
    E.selection_start_cx = start_cx;
    E.selection_start_cy = E.cy;
    E.selection_end_cx = end_cx;
    E.selection_end_cy = E.cy;
    E.selection_active = 1;
    E.mode = SELECTION_MODE;
    
    // Muovi il cursore all'inizio della selezione
    E.cx = start_cx;
    
editorSetStatusMessage("Row text selected (chars %d-%d)", start_cx, end_cx - 1);
}

/* Helpers for delimiter matching on a single line */
int findMatchingRightInLine(erow *row, int start_idx, char open_ch, char close_ch) {
    int n = row->size;
    int depth = 1;
    for (int i = start_idx + 1; i < n; i++) {
        char ch = row->chars[i];
        if (ch == open_ch) depth++;
        else if (ch == close_ch) {
            depth--;
            if (depth == 0) return i;
        }
    }
    return -1;
}

int findNextQuoteInLine(erow *row, int start_idx, char quote) {
    int n = row->size;
    int escaped = 0;
    for (int i = start_idx + 1; i < n; i++) {
        char ch = row->chars[i];
        if (!escaped && ch == '\\') { escaped = 1; continue; }
        if (!escaped && ch == quote) return i;
        escaped = 0;
    }
    return -1;
}

/* Select inside surrounding delimiters on the same line: (), [], {}, < > , ' ', " " */
void editorSelectInsideDelims() {
    if (E.cy >= E.numrows) {
        editorSetStatusMessage("No line to operate on");
        return;
    }
    erow *row = &E.row[E.cy];
    int n = row->size;
    if (n == 0) { editorSetStatusMessage("Empty line"); return; }

    int cx = E.cx;

    // Scan leftwards for ANY opener/quote, pick the first valid pair enclosing the cursor
    for (int left = cx - 1; left >= 0; left--) {
        char ch = row->chars[left];
        int right = -1;
        char open_ch = 0, close_ch = 0;
        int is_quote = 0;
        switch (ch) {
            case '(': open_ch='('; close_ch=')'; break;
            case '[': open_ch='['; close_ch=']'; break;
            case '{': open_ch='{'; close_ch='}'; break;
            case '<': open_ch='<'; close_ch='>'; break;
            case '"': is_quote=1; open_ch='"'; close_ch='"'; break;
            case '\'': is_quote=1; open_ch='\''; close_ch='\''; break;
            default: continue;
        }
        if (is_quote) {
            right = findNextQuoteInLine(row, left, close_ch);
        } else {
            right = findMatchingRightInLine(row, left, open_ch, close_ch);
        }
        if (right < 0) continue;
        // Cursor must be within (left, right]
        if (!(left < cx && cx <= right)) continue;
        if (right - left <= 1) continue;

        // Select inside
        E.selection_start_cx = left + 1;
        E.selection_start_cy = E.cy;
        E.selection_end_cx = right;
        E.selection_end_cy = E.cy;
        E.selection_active = 1;
        E.mode = SELECTION_MODE;
        editorSetStatusMessage("Selected inside %c%c", open_ch, close_ch);
        return;
    }

    editorSetStatusMessage("No surrounding delimiters found");
}

/**
 * @brief Simplified line selection for Shift+Up/Down
 *        Uses anchor-cursor model: anchor line stays fixed, cursor line moves
 * @param direction -1 for up, +1 for down
 */
void editorQuickSelectFullLine(int direction) {
    if (E.cy >= E.numrows) {
        editorSetStatusMessage("No line to select");
        return;
    }
    
    // If no selection is active, set anchor to current line
    if (!E.selection_active) {
        // Set anchor line (selection_start) to current line - FULL LINE
        E.selection_start_cx = 0;                    // Start of line
        E.selection_start_cy = E.cy;                 // Current line becomes anchor
        E.selection_end_cx = E.row[E.cy].size;       // End of line
        E.selection_end_cy = E.cy;                   // Current line (will be our moving cursor)
        E.selection_active = 1;
    }
    
    // Move cursor line in the specified direction
    if (direction == -1) { // Up
        if (E.cy > 0) {
            E.cy--;
            E.cx = 0;  // Move to start of line
        } else {
            editorSetStatusMessage("Cannot move up - at beginning of file");
            return;
        }
    } else { // Down
        if (E.cy < E.numrows - 1) {
            E.cy++;
            E.cx = 0;  // Move to start of line
        } else {
            editorSetStatusMessage("Cannot move down - at end of file");
            return;
        }
    }
    
    // Update selection end to new cursor line - FULL LINE
    E.selection_end_cx = E.row[E.cy].size;  // End of new line
    E.selection_end_cy = E.cy;              // New line
    
    // Check if anchor and cursor are on the same line (empty selection)
    if (E.selection_start_cy == E.selection_end_cy) {
        editorDeselectSelection();
        E.mode = NORMAL_MODE;
        editorSetStatusMessage("Selection cleared");
        return;
    }
    
    // Show selection info (normalize for display only)
    int start_line = E.selection_start_cy;
    int end_line = E.selection_end_cy;
    
    if (start_line > end_line) {
        int temp = start_line;
        start_line = end_line;
        end_line = temp;
    }
    
    editorSetStatusMessage("Selected: lines %d-%d", start_line + 1, end_line + 1);
}

/**
 * @brief Simplified character selection for Shift+Left/Right using the anchor-cursor model.
 * @param direction -1 for left, +1 for right
 */
void editorQuickSelectChar(int direction) {
    // If there are no rows, do nothing.
    if (E.cy >= E.numrows) {
        editorSetStatusMessage("No text to select");
        return;
    }

    // If no selection is active, set the anchor (anc1) to the current cursor position.
    if (!E.selection_active) {
        E.selection_start_cx = E.cx;
        E.selection_start_cy = E.cy;
        E.selection_active = 1;
    }

    // Move the actual editor cursor based on the direction.
    if (direction == -1) { // Move left
        if (E.cx > 0) {
            E.cx--;
        } else if (E.cy > 0) {
            E.cy--;
            E.cx = E.row[E.cy].size;
        }
    } else { // Move right
        erow *row = &E.row[E.cy];
        if (E.cx < row->size) {
            E.cx++;
        } else if (E.cy < E.numrows - 1) {
            E.cy++;
            E.cx = 0;
        }
    }

    // The cursor (anc2) always follows the editor's cursor.
    E.selection_end_cx = E.cx;
    E.selection_end_cy = E.cy;

    // If the anchor and the cursor are at the same position, the selection is empty.
    if (E.selection_start_cy == E.selection_end_cy && E.selection_start_cx == E.selection_end_cx) {
        editorDeselectSelection();
        E.mode = NORMAL_MODE;
        editorSetStatusMessage("Selection cleared");
    } else {
        editorSetStatusMessage("Selection active");
    }
}
/*
 * Find \u0026 Replace helpers
 */

static int editorIsWordBoundaryInRow(erow *row, int pos, int len) {
  // Check left boundary
  if (pos > 0) {
    char left = row->chars[pos - 1];
    if (!is_separator(left)) return 0;
  }
  // Check right boundary
  int rpos = pos + len;
  if (rpos < row->size) {
    char right = row->chars[rpos];
    if (!is_separator(right)) return 0;
  }
  return 1;
}

int editorCountOccurrencesInRow(erow *row, const char *needle) {
  if (!needle || !*needle) return 0;
  int count = 0;
  size_t nlen = strlen(needle);
  if (nlen == 0) return 0;
  int search_from = 0;
  while (search_from <= row->size - (int)nlen) {
    char *m = strstr(row->chars + search_from, needle);
    if (!m) break;
    int at = (int)(m - row->chars);
    if (editorIsWordBoundaryInRow(row, at, (int)nlen)) {
      count++;
      search_from = at + (int)nlen;
    } else {
      search_from = at + 1;
    }
  }
  return count;
}

int editorRowReplaceAt(erow *row, int at, int del_len, const char *repl, int repl_len) {
  if (at < 0 || at > row->size || del_len < 0 || at + del_len > row->size) return 0;
  int new_size = row->size - del_len + repl_len;
  char *new_chars = malloc(new_size + 1);
  if (!new_chars) return 0;

  // Copy before match
  memcpy(new_chars, row->chars, at);
  // Copy replacement
  if (repl_len > 0) memcpy(new_chars + at, repl, repl_len);
  // Copy after match
  memcpy(new_chars + at + repl_len, row->chars + at + del_len, row->size - (at + del_len));

  new_chars[new_size] = '\0';
  free(row->chars);
  row->chars = new_chars;
  row->size = new_size;
  editorUpdateRow(row);
  E.dirty++;
  return 1;
}

int editorRowReplaceAll(erow *row, const char *needle, const char *repl) {
  if (!needle || !*needle) return 0;
  int replaced = 0;
  size_t nlen = strlen(needle);
  size_t rlen = repl ? strlen(repl) : 0;
  int search_from = 0;
  while (search_from <= row->size - (int)nlen) {
    char *m = strstr(row->chars + search_from, needle);
    if (!m) break;
    int at = (int)(m - row->chars);
    if (!editorIsWordBoundaryInRow(row, at, (int)nlen)) {
      search_from = at + 1; // skip this non-whole-word occurrence
      continue;
    }
    editorRowReplaceAt(row, at, (int)nlen, repl, (int)rlen);
    replaced++;
    search_from = at + (int)rlen;
  }
  return replaced;
}

int editorReplaceAllInBuffer(const char *needle, const char *repl) {
  int total = 0;
  for (int i = 0; i < E.numrows; i++) {
    total += editorRowReplaceAll(&E.row[i], needle, repl);
  }
  return total;
}

/* file browser */

/**
 * @brief Comparison function for qsort. Sorts files and directories.
 *        Directories come before files, then sorting is alphabetical (case-insensitive).
 * @param a Pointer to the first element.
 * @param b Pointer to the second element.
 * @return <0 if a<b, 0 if a==b, >0 if a>b.
 */
int file_compare(const void *a, const void *b) {
    const char *a_str = *(const char **)a;
    const char *b_str = *(const char **)b;
    struct stat st_a, st_b;
    stat(a_str, &st_a);
    stat(b_str, &st_b);
    if (S_ISDIR(st_a.st_mode) && !S_ISDIR(st_b.st_mode)) return -1;
    if (!S_ISDIR(st_a.st_mode) && S_ISDIR(st_b.st_mode)) return 1;
    return strcasecmp(a_str, b_str);
}

/**
 * @brief Displays a simple file browser to open files.
 * @param initial_path The initial path to start browsing from.
 * @return The full path of the selected file (to be freed), or NULL if canceled.
 */
char *editorFileBrowser(const char *initial_path) {
    char *path = realpath(initial_path, NULL);
    if (!path) {
        editorSetStatusMessage("Cannot open directory: %s", strerror(errno));
        return NULL;
    }

    int num_items = 0;
    char **items = NULL;
    int selected = 0;
    int offset = 0;

    while (1) {
        num_items = 0;
        items = NULL;

        DIR *dir = opendir(path);
        if (!dir) {
            editorSetStatusMessage("Cannot open directory: %s", strerror(errno));
            free(path);
            return NULL;
        }

        struct dirent *entry;
        while ((entry = readdir(dir)) != NULL) {
            if (strcmp(entry->d_name, ".") == 0) continue;
            items = realloc(items, sizeof(char *) * (num_items + 1));
            char full_path[1024];
            snprintf(full_path, sizeof(full_path), "%s/%s", path, entry->d_name);
            items[num_items++] = strdup(full_path);
        }
        closedir(dir);

        qsort(items, num_items, sizeof(char *), file_compare);

        struct abuf ab = ABUF_INIT;
        abAppend(&ab, "\x1b[?25l", 6);
        abAppend(&ab, "\x1b[2J", 4);
        abAppend(&ab, "\x1b[H", 3);

        char header[1024];
        snprintf(header, sizeof(header), "File Browser: %s", path);
        int header_len = strlen(header);
        if (header_len > E.screencols) header_len = E.screencols;
        abAppend(&ab, header, header_len);
        for (int i = header_len; i < E.screencols; i++) abAppend(&ab, " ", 1);
        abAppend(&ab, "\x1b[m", 3);
        abAppend(&ab, "\r\n", 2);

        int display_rows = E.screenrows - 2;
        if (selected >= offset + display_rows) offset = selected - display_rows + 1;
        if (selected < offset) offset = selected;

        for (int i = 0; i < display_rows; i++) {
            int index = i + offset;
            if (index >= num_items) break;

            char *item_path = items[index];
            char *item_name = strrchr(item_path, '/');
            item_name = item_name ? item_name + 1 : item_path;

            struct stat st;
            stat(item_path, &st);

            char display_str[256];
            snprintf(display_str, sizeof(display_str), "%s%s", item_name, S_ISDIR(st.st_mode) ? "/" : "");

            int len = strlen(display_str);
            if (len > E.screencols) len = E.screencols;

            if (index == selected) abAppend(&ab, "\x1b[7m", 4);
            abAppend(&ab, display_str, len);
            if (index == selected) abAppend(&ab, "\x1b[m", 3);
            abAppend(&ab, "\x1b[K", 3);
            abAppend(&ab, "\r\n", 2);
        }

        write(STDOUT_FILENO, ab.b, ab.len);
        abFree(&ab);

        int c = editorReadKey();

        // Get selected_path before freeing items
        char *current_selected_path = NULL;
        if (c == '\r' && selected < num_items) {
            current_selected_path = realpath(items[selected], NULL);
        }

        for (int i = 0; i < num_items; i++) free(items[i]);
        free(items);

        switch (c) {
            case '\r': {
                if (current_selected_path) {
                    struct stat st;
                    stat(current_selected_path, &st);
                    if (S_ISDIR(st.st_mode)) {
                        free(path);
                        path = current_selected_path;
                        selected = 0;
                        offset = 0;
                    } else {
                        free(path);
                        return current_selected_path;
                    }
                } else {
                    // Handle case where realpath failed or selected was out of bounds
                    editorSetStatusMessage("Error: Could not resolve path.");
                }
                break;
            }
            case ARROW_UP:
                if (selected > 0) selected--;
                break;
            case ARROW_DOWN:
                if (selected < num_items - 1) selected++;
                break;
            case '\x1b':
                free(path);
                return NULL;
        }
    }
}

void editorShowHelp() {
    // Create a temporary buffer to hold the help text
    const char *help_text[] = {
        "Wee Editor Help",
        "",
        "-- Normal Mode --",
        "Ctrl-S: Save",
        "Ctrl-Y: Save As",
        "Ctrl-Q: Quit",
        "Ctrl-F: Find",
        "Ctrl-O: Open File Browser",
        "Ctrl-N: Toggle Line Numbers",
        "Ctrl-T: New File",
        "Ctrl-G: Show this Help",
        "",
        "Ctrl-J: Jump to Line",
        "Ctrl-Z: Undo",
        "Ctrl-R: Redo",
        "",
        "Ctrl-W: Copy Line",
        "Ctrl-K: Cut Line",
        "Ctrl-U: Paste",
        "Ctrl-B: Start Selection",
        "Ctrl-E: End Selection & Enter Selection Mode",
        "Ctrl-A: Select All",
        "Alt-R : Select Row",
        "Shift-Arrows : Rapid Selection / Press ESC to enter in SEL. MODE",
        "Shift-Tab :    Select text between brachets",
        "-- Selection Mode --",
        "ESC (in Sel. Mode): Cancel Selection",
        "Ctrl-W (in Sel. Mode): Copy Selection",
        "Ctrl-K (in Sel. Mode): Cut Selection",
        "DEL (in Sel. Mode): Delete Selection",
        "Arrows (in Sel. Mode): Move Selection (Up/Down/Left/Right)",
        NULL
    };

    struct abuf ab = ABUF_INIT;
    abAppend(&ab, "\x1b[2J", 4); // Clear screen
    abAppend(&ab, "\x1b[H", 3);  // Go to home

    int y = 0;
    for (y = 0; help_text[y] != NULL; y++) {
        abAppend(&ab, help_text[y], strlen(help_text[y]));
        abAppend(&ab, "\r\n", 2);
    }

    // Add a prompt to press any key to continue
    const char *prompt = "Press any key to continue...";
    int padding = (E.screencols - strlen(prompt)) / 2;
    for (int i = 0; i < padding; i++) abAppend(&ab, " ", 1);
    abAppend(&ab, prompt, strlen(prompt));

    write(STDOUT_FILENO, ab.b, ab.len);
    abFree(&ab);

    // Wait for a keypress before returning to the editor
    editorReadKey();
}


/* init */

/**
 * @brief Initializes the editor state.
 */
void initEditor() {
  E.cx = 0;
  E.cy = 0;
  E.rx = 0;
  E.rowoff = 0;
  E.coloff = 0;
  E.numrows = 0;
  E.row = NULL;
  E.filename = NULL;
  E.statusmsg[0] = '\0';
  E.statusmsg_time = 0;
  E.dirty = 0;
  E.linenumbers = 1;
  E.clipboard = NULL;
  E.clipboard_len = 0;
  E.hl_row = -1;
  E.hl_start = -1;
  E.hl_end = -1;
  E.syntax = NULL;
  E.selection_start_cx = -1;
  E.selection_start_cy = -1;
  E.selection_end_cx = -1;
  E.selection_end_cy = -1;
  E.selection_active = 0;
  E.mode = NORMAL_MODE;

  // Inizializza sistema undo
  E.undo_system.head = NULL;
  E.undo_system.current = NULL;
  E.undo_system.max_snapshots = 50;
  E.undo_system.current_count = 0;
  E.undo_system.last_snapshot_time = 0;

  if (getWindowSize(&E.screenrows, &E.screencols) == -1) die("getWindowSize");
  E.screenrows -= 2;
}

void printHelp() {
    const char *help_text[] = {
        "Wee Editor Help",
        "",
        "Usage: wee [options] [filename]",
        "",
        "Options:",
        "  --version, -v    Print version and exit.",
        "  --help, -h       Print this help message and exit.",
        "",
        "Keybindings:",
        "  Ctrl-S: Save",
        "  Ctrl-Y: Save As",
        "  Ctrl-Q: Quit",
        "  Ctrl-F: Find",
        "  Ctrl-O: Open File Browser",
        "  Ctrl-N: Toggle Line Numbers",
        "  Ctrl-T: New File",
        "  Ctrl-G: Show this Help",
        "",
        "  Ctrl-J: Jump to Line",
        "  Ctrl-Z: Undo",
        "  Ctrl-R: Redo",
        "",
        "  Ctrl-B: Start Selection",
        "  Ctrl-E: End Selection & Enter Selection Mode",
        "  Ctrl-A: Select All",
        "  ESC (in Sel. Mode): Cancel Selection",
        "  Ctrl-W (in Sel. Mode): Copy Selection",
        "  Ctrl-K (in Sel. Mode): Cut Selection",
        "  DEL (in Sel. Mode): Delete Selection",
        "  Arrows (in Sel. Mode): Move Selection (Up/Down/Left/Right)",
        "",
        "  Ctrl-W: Copy Line",
        "  Ctrl-K: Cut Line",
        "  Ctrl-U: Paste",
        NULL
    };
    for (int i = 0; help_text[i] != NULL; i++) {
        printf("%s\n", help_text[i]);
    }
}

/**
 * @brief Main function of the program.
 * @param argc Number of command-line arguments.
 * @param argv Array of command-line arguments.
 * @return 0 on success, 1 on error.
 */
int main(int argc, char *argv[]) {
  if (argc == 2) {
    if (strcmp(argv[1], "--version") == 0 || strcmp(argv[1], "-v") == 0) {
      printf("Wee Editor -  by anidisc 'wee.anidisc.it '  -- version %s\n", WEE_VERSION);
      return 0;
    }
    if (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0) {
      printHelp();
      return 0;
    }
  }

  enableRawMode();
  initEditor();
  if (argc >= 2) {
    editorOpen(argv[1]);
  } else {
    editorSetStatusMessage("HELP: Ctrl-G = show help | Ctrl-S = save | Ctrl-Q = quit | Ctrl-O = open file");
  }

  while (1) {
    editorRefreshScreen();
    editorProcessKeypress();
  }
  return 0;
}

