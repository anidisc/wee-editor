/* includes */

#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
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

#define WEE_VERSION "0.84 Beta"
#define WEE_TAB_STOP 4
#define WEE_QUIT_TIMES 2

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
  PAGE_DOWN
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
};

enum editorMode {
  NORMAL_MODE,
  SELECTION_MODE
};

/* append buffer */

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
    if (read(STDIN_FILENO, &seq[1], 1) != 1) return '\x1b';
    if (seq[0] == '[') {
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
        }
      } else {
        switch (seq[1]) {
          case 'A': return ARROW_UP;
          case 'B': return ARROW_DOWN;
          case 'C': return ARROW_RIGHT;
          case 'D': return ARROW_LEFT;
          case 'H': return HOME_KEY;
          case 'F': return END_KEY;
        }
      }
    } else if (seq[0] == 'O') {
      switch (seq[1]) {
        case 'H': return HOME_KEY;
        case 'F': return END_KEY;
      }
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
  if (E.cx == 0) {
    editorInsertRow(E.cy, "", 0);
    E.cy++;
    E.cx = 0;
    return;
  }

  erow *row = &E.row[E.cy];
  int indent_len = 0;
  while (indent_len < row->size && isspace(row->chars[indent_len])) {
    indent_len++;
  }
  
  char *indent_str = malloc(indent_len + 1);
  if (indent_len > 0) memcpy(indent_str, row->chars, indent_len);
  indent_str[indent_len] = '\0';

  char* rest_of_line = &row->chars[E.cx];
  int rest_len = row->size - E.cx;
  char* new_line_content = malloc(indent_len + rest_len + 1);
  memcpy(new_line_content, indent_str, indent_len);
  memcpy(new_line_content + indent_len, rest_of_line, rest_len);
  new_line_content[indent_len + rest_len] = '\0';

  int old_cx = E.cx;
  editorInsertRow(E.cy + 1, new_line_content, indent_len + rest_len);

  row = &E.row[E.cy];
  row->size = old_cx;
  editorUpdateRow(row);

  E.cy++;
  E.cx = indent_len;

  free(indent_str);
  free(new_line_content);
}

/**
 * @brief Deletes the character before the cursor (like the Backspace key).
 *        If the cursor is at the beginning of a line, it joins the current line with the previous one.
 */
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

  // Ensure start is before end
  if (start_cy > end_cy || (start_cy == end_cy && start_cx > end_cx)) {
    int temp_cx = start_cx;
    int temp_cy = start_cy;
    start_cx = end_cx;
    start_cy = end_cy;
    end_cx = temp_cx;
    end_cy = temp_cy;
    editorSetStatusMessage("editorDelCharSelection: Swapped coords: start_cx=%d, start_cy=%d, end_cx=%d, end_cy=%d", start_cx, start_cy, end_cx, end_cy);
  }

  // If selection is empty (start_cx == end_cx and start_cy == end_cy), do nothing
  if (start_cy == end_cy && start_cx == end_cx) {
      E.selection_active = 0;
      editorSetStatusMessage("editorDelCharSelection: Empty selection.");
      return;
  }

  // Get the suffix of the end_row before any deletions
  char *suffix_of_end_row = NULL;
  int suffix_of_end_row_len = 0;
  if (end_cx < E.row[end_cy].size) {
      suffix_of_end_row_len = E.row[end_cy].size - end_cx;
      suffix_of_end_row = malloc(suffix_of_end_row_len + 1);
      memcpy(suffix_of_end_row, &E.row[end_cy].chars[end_cx], suffix_of_end_row_len);
      suffix_of_end_row[suffix_of_end_row_len] = '\0';
      editorSetStatusMessage("editorDelCharSelection: Suffix len: %d, Suffix: '%s'", suffix_of_end_row_len, suffix_of_end_row);
  }

  // Delete characters from start_cx to end of line in start_cy
  erow *start_row = &E.row[start_cy];
  start_row->size = start_cx;
  editorUpdateRow(start_row);
  editorSetStatusMessage("editorDelCharSelection: Start row truncated. New size: %d", start_row->size);


  // Delete full rows between start_cy and end_cy (exclusive of start_cy, inclusive of end_cy)
  // Iterate from end_cy down to start_cy + 1
  for (int i = end_cy; i > start_cy; i--) {
      editorDelRow(i);
      editorSetStatusMessage("editorDelCharSelection: Deleted row %d. Remaining rows: %d", i, E.numrows);
  }

  // Append the suffix of the original end_row to the modified start_row
  if (suffix_of_end_row_len > 0) {
      editorRowAppendString(start_row, suffix_of_end_row, suffix_of_end_row_len);
      free(suffix_of_end_row);
      editorSetStatusMessage("editorDelCharSelection: Suffix appended. New row size: %d", start_row->size);
  }

  E.cx = start_cx; // Move cursor to start of deleted selection
  E.cy = start_cy;

  E.selection_active = 0;
  E.dirty++; // Mark file as dirty
  editorSetStatusMessage("editorDelCharSelection: Done.");
}

void editorDelChar() {
  if (E.cy == E.numrows) return;
  if (E.cx == 0 && E.cy == 0) return;
  erow *row = &E.row[E.cy];
  if (E.cx > 0) {
    editorRowDelChar(row, E.cx - 1);
    E.cx--;
  } else {
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
 *        If there are unsaved changes, it asks the user what to do.
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
  E.selection_active = 0; // Deselect after copy

  // Explicitly reset HL_SELECTION for affected rows
  // int start_cy = E.selection_start_cy;
  // int end_cy = E.selection_end_cy;

  // Ensure start is before end for iteration
  if (start_cy > end_cy) {
    int temp_cy = start_cy;
    start_cy = end_cy;
    end_cy = temp_cy;
  }

  for (int i = start_cy; i <= end_cy; i++) {
    editorUpdateSyntax(&E.row[i]); // Re-evaluate syntax highlighting for the row
  }

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

  // If there's an active selection, delete it first
  if (E.selection_active) {
    editorDelCharSelection();
  }

  // Insert clipboard content
  for (int i = 0; i < E.clipboard_len; i++) {
    if (E.clipboard[i] == '\n') {
      editorInsertRawNewline(); // Use raw newline insertion
    } else {
      editorInsertChar(E.clipboard[i]);
    }
  }
  editorSetStatusMessage("Pasted.");
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
    E.selection_active = 0;
    return;
  } else if (key == ARROW_RIGHT || key == ARROW_DOWN) {
    direction = 1;
  } else if (key == ARROW_LEFT || key == ARROW_UP) {
    direction = -1;
  } else {
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
      E.selection_active = 0;
      last_match = -1;
  }
}

void editorFind() {
  int saved_cx = E.cx, saved_cy = E.cy;
  int saved_coloff = E.coloff, saved_rowoff = E.rowoff;

  char *query = editorPrompt("Search: %s (Use ESC/Arrows/Enter)", editorFindCallback);

  if (query) {
    free(query);
  } else {
    E.cx = saved_cx; E.cy = saved_cy;
    E.coloff = saved_coloff; E.rowoff = saved_rowoff;
  }
  // Ensure selection is cleared and screen is refreshed when prompt is closed
  E.selection_active = 0;
  editorRefreshScreen();
}

/* append buffer */

struct abuf {
  char *b;
  int len;
};

#define ABUF_INIT {NULL, 0}

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
 */
void editorDrawRows(struct abuf *ab) {
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

  for (int y = 0; y < E.screenrows; y++) {
    int filerow = y + E.rowoff;
    if (filerow >= E.numrows) {
      if (E.numrows == 0 && y == E.screenrows / 3) {
        char welcome[80];
        int welcomelen = snprintf(welcome, sizeof(welcome), "Wee editor -- version %s", WEE_VERSION);
        int text_cols = editorGetTextCols();
        if (welcomelen > text_cols) welcomelen = text_cols;
        int padding = (text_cols - welcomelen) / 2;
        if (padding) {
          abAppend(ab, "~", 1);
          padding--;
        }
        while (padding--) abAppend(ab, " ", 1);
        abAppend(ab, welcome, welcomelen);
      } else if (E.numrows == 0 && y == E.screenrows / 3 + 1) {
        char author[80];
        int authorlen = snprintf(author, sizeof(author), "by anidisc");
        int text_cols = editorGetTextCols();
        if (authorlen > text_cols) authorlen = text_cols;
        int padding = (text_cols - authorlen) / 2;
        if (padding) {
          abAppend(ab, "~", 1);
          padding--;
        }
        while (padding--) abAppend(ab, " ", 1);
        abAppend(ab, author, authorlen);
      } else if (E.numrows == 0 && y == E.screenrows / 3 + 2) {
        char site[80];
        int sitelen = snprintf(site, sizeof(site), "wee.anidisc.it");
        int text_cols = editorGetTextCols();
        if (sitelen > text_cols) sitelen = text_cols;
        int padding = (text_cols - sitelen) / 2;
        if (padding) {
          abAppend(ab, "~", 1);
          padding--;
        }
        while (padding--) abAppend(ab, " ", 1);
        abAppend(ab, site, sitelen);
      } else {
        abAppend(ab, "~", 1);
      }
    } else {
      if (E.linenumbers) {
        char linenum_buf[16];
        int len = snprintf(linenum_buf, sizeof(linenum_buf), "%*d ", linenum_width - 1, filerow + 1); 
        abAppend(ab, "\x1b[36m", 5);
        abAppend(ab, linenum_buf, len);
        abAppend(ab, "\x1b[m", 3);
      }
      int len = E.row[filerow].rsize - E.coloff;
      if (len < 0) len = 0;
      if (len > E.screencols - linenum_width) len = E.screencols - linenum_width;
      char *c = &E.row[filerow].render[E.coloff];
      unsigned char *hl = &E.row[filerow].hl[E.coloff];
      int current_color = -1;

      // Local variables for selection coordinates
      int local_sel_start_cx = E.selection_start_cx;
      int local_sel_start_cy = E.selection_start_cy;
      int local_sel_end_cx = E.selection_end_cx;
      int local_sel_end_cy = E.selection_end_cy;

      if (E.selection_active) {
          // Ensure start is before end for drawing purposes
          // Ensure start is before end for drawing purposes (already done in editorDelCharSelection and editorCopySelection, but good to be safe)
          if (local_sel_start_cy > local_sel_end_cy || (local_sel_start_cy == local_sel_end_cy && local_sel_start_cx > local_sel_end_cx)) {
              int temp_cy = local_sel_start_cy;
              int temp_cx = local_sel_start_cx;
              local_sel_start_cy = local_sel_end_cy;
              local_sel_start_cx = local_sel_end_cx;
              local_sel_end_cy = temp_cy;
              local_sel_end_cx = temp_cx;
          }

          int sel_start_rx = 0;
          int sel_end_rx = 0;

          if (filerow < local_sel_start_cy || filerow > local_sel_end_cy) {
              // Current row is outside the selection range, no highlighting
              sel_start_rx = -1; // Indicate no selection for this row
              sel_end_rx = -1;
          } else {
              // Current row is within the selection range
              if (filerow == local_sel_start_cy) {
                  sel_start_rx = editorRowCxToRx(&E.row[filerow], local_sel_start_cx);
              } else {
                  sel_start_rx = 0; // Start from the beginning of the row
              }

              if (filerow == local_sel_end_cy) {
                  sel_end_rx = editorRowCxToRx(&E.row[filerow], local_sel_end_cx);
              } else {
                  sel_end_rx = E.row[filerow].rsize; // Go to the end of the row
              }
          }

          for (int j = 0; j < len; j++) {
            int current_render_idx = E.coloff + j; // This is the render index of the character being drawn
            // Apply selection highlighting
            if (sel_start_rx != -1) { // Only highlight if this row is part of the selection
                // Special case for single character selection
                if (local_sel_start_cy == local_sel_end_cy && local_sel_start_cx == local_sel_end_cx) {
                    if (current_render_idx == sel_start_rx) {
                        hl[j] = HL_SELECTION;
                    }
                } else {
                    if (current_render_idx >= sel_start_rx && current_render_idx < sel_end_rx) {
                        hl[j] = HL_SELECTION;
                    }
                }
            }
          }
      }

      for (int j = 0; j < len; j++) {
        if (filerow == E.hl_row) {
            int start = E.hl_start > E.coloff ? E.hl_start - E.coloff : 0;
            int end = E.hl_end > E.coloff ? E.hl_end - E.coloff : 0;
            if (end > len) end = len;
            if (j >= start && j < end) {
                hl[j] = HL_MATCH;
            }
        }

        int color = editorSyntaxToColor(hl[j]);
        // Only consider HL_SELECTION if selection is active
        int is_selection = (E.selection_active && hl[j] == HL_SELECTION);

        if (is_selection) {
            if (current_color != 7) { // If not already in inverse video
                abAppend(ab, "\x1b[7m", 4); // Set inverse video
                current_color = 7;
            }
        } else {
            if (current_color == 7) { // If was in inverse video, reset it
                abAppend(ab, "\x1b[27m", 5); // Reset inverse video
                current_color = -1; // Reset current_color to force re-evaluation of normal color
            }
            if (color != current_color) {
                current_color = color;
                char buf[16];
                int clen = snprintf(buf, sizeof(buf), "\x1b[%dm", color);
                abAppend(ab, buf, clen);
            }
        }

        if (iscntrl(c[j])) { // Control characters
          char sym = (c[j] <= 26) ? '@' + c[j] : '?';
          abAppend(ab, &sym, 1);
        } else { // Normal characters
          abAppend(ab, &c[j], 1);
        }
      }
      // Ensure inverse video is reset at the end of the line
      if (current_color == 7) {
          abAppend(ab, "\x1b[27m", 5);
      }
      abAppend(ab, "\x1b[39m", 5); // Reset foreground color
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
  abAppend(ab, "\x1b[7m", 4);
  
  char *basename = E.filename ? strrchr(E.filename, '/') : NULL;
  if (basename) {
    basename = basename + 1;
  } else {
    basename = E.filename;
  }

  char status[256];
  int len = 0;

  // File name part
  abAppend(ab, "\x1b[37;44m", 7);
  abAppend(ab, "[", 1);
  abAppend(ab, basename ? basename : "No Name", strlen(basename ? basename : "No Name"));
  abAppend(ab, "]", 1);
  abAppend(ab, "\x1b[m", 3);
  abAppend(ab, "\x1b[7m", 4);

  len = 2 + strlen(basename ? basename : "No Name");

  // Other info
  int len2 = snprintf(status, sizeof(status), " - %d lines %s", E.numrows, E.dirty ? "(modified)" : "");
  abAppend(ab, status, len2);
  len += len2;

  char rstatus[80];
  int rlen = snprintf(rstatus, sizeof(rstatus), "%s | %d/%d", E.syntax ? E.syntax->language : "no ft", E.cy + 1, E.numrows);
  
  while (len < E.screencols) {
    if (E.screencols - len == rlen) {
      abAppend(ab, rstatus, rlen);
      len += rlen;
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
        editorUpdateSelectionSyntax(); // Update syntax highlighting to remove selection
        E.selection_active = 0;
        E.mode = NORMAL_MODE;
        editorSetStatusMessage("Selection cancelled.");
        break;
      case DEL_KEY: // Delete selection
        editorDelCharSelection();
        E.mode = NORMAL_MODE;
        editorSetStatusMessage("Selection deleted.");
        editorRefreshScreen(); // Add this
        break;
      case CTRL_KEY('w'): // Copy selection
        editorCopySelection(); // This already sets E.selection_active = 0
        E.mode = NORMAL_MODE;
        // editorSetStatusMessage("Selection copied."); // Redundant, editorCopySelection sets it
        editorRefreshScreen(); // Add this
        break;
      case CTRL_KEY('k'): // Cut selection
        editorSetStatusMessage("Mode: SELECTION_MODE. Cutting selection.");
        editorCutSelection();
        E.mode = NORMAL_MODE;
        editorSetStatusMessage("Selection cut.");
        editorRefreshScreen(); // Add this
        break;
      default:
        if (!iscntrl(c) && c < 128) { // Check if it's a printable ASCII character
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
      case '\r': editorInsertNewline(); break;
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
          editorCutSelection();
          E.mode = NORMAL_MODE; // Exit selection mode after cutting
          editorSetStatusMessage("Selection cut.");
        } else { // Otherwise, cut the current line
          editorSetStatusMessage("Mode: NORMAL_MODE. No selection active. Cutting line.");
          editorCutLine();
        }
        break;
      case CTRL_KEY('u'): editorPaste(); break;
      case CTRL_KEY('n'): E.linenumbers = !E.linenumbers; break;
      case CTRL_KEY('t'): editorNewFile(); break;
      case CTRL_KEY('g'): editorShowHelp(); break;
      case CTRL_KEY('f'): editorFind(); break;
      case BACKSPACE:
      case CTRL_KEY('h'):
      case DEL_KEY:
        if (c == DEL_KEY) editorMoveCursor(ARROW_RIGHT);
        editorDelChar();
        break;
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
      case CTRL_KEY('o'): {
        char *path = editorFileBrowser(".");
        if (path) {
          editorOpen(path);
          free(path);
        }
        break;
      }
      case CTRL_KEY('l'):
      case '\x1b':
        // This is now handled in SELECTION_MODE
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
          editorDelCharSelection(); // Delete the selected text
          editorInsertChar(c);      // Insert the new character
          E.mode = NORMAL_MODE;     // Exit selection mode
        } else if (E.selection_active) { // If a selection is active and a non-printable char is typed (e.g., arrow keys)
            // Do nothing, allow cursor movement within selection mode
        } else { // No selection active, insert character normally
            editorInsertChar(c);
        }
        break;
    }
  }
  quit_times = WEE_QUIT_TIMES;
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
    int scroll_offset = 0;

    while (1) {
        // Populate file list
        DIR *d = opendir(path);
        if (!d) {
            editorSetStatusMessage("Cannot open directory: %s", strerror(errno));
            return NULL;
        }

        struct dirent *dir;
        num_items = 0;
        items = malloc(sizeof(char *));
        
        while ((dir = readdir(d)) != NULL) {
            if (strcmp(dir->d_name, ".") == 0) continue;
            items = realloc(items, sizeof(char *) * (num_items + 1));
            char full_path[1024];
            snprintf(full_path, sizeof(full_path), "%s/%s", path, dir->d_name);
            items[num_items] = strdup(full_path);
            num_items++;
        }
        closedir(d);
        qsort(items, num_items, sizeof(char *), file_compare);

        // Browser loop
        int browser_done = 0;
        while (!browser_done) {
            // Draw browser
            struct abuf ab = ABUF_INIT;
            abAppend(&ab, "\x1b[?25l", 6);
            abAppend(&ab, "\x1b[H", 3);
            
            char header[1024];
            int header_len = snprintf(header, sizeof(header), "Open File: %.80s", path);
            if (header_len > E.screencols) header_len = E.screencols;
            abAppend(&ab, "\x1b[7m", 4);
            abAppend(&ab, header, header_len);
            for (int i = header_len; i < E.screencols; i++) abAppend(&ab, " ", 1);
            abAppend(&ab, "\x1b[m", 3);
            abAppend(&ab, "\r\n", 2);

            for (int i = 0; i < E.screenrows -1; i++) {
                int item_idx = i + scroll_offset;
                if (item_idx < num_items) {
                    char *item_name = strrchr(items[item_idx], '/');
                    if (item_name) item_name++; else item_name = items[item_idx];

                    struct stat st;
                    char display_name[256];
                    stat(items[item_idx], &st);
                    snprintf(display_name, sizeof(display_name), "%s%s", item_name, S_ISDIR(st.st_mode) ? "/" : "");

                    if (item_idx == selected) abAppend(&ab, "\x1b[7m", 4);
                    abAppend(&ab, display_name, strlen(display_name));
                    if (item_idx == selected) abAppend(&ab, "\x1b[m", 3);
                }
                abAppend(&ab, "\x1b[K", 3);
                abAppend(&ab, "\r\n", 2);
            }
            write(STDOUT_FILENO, ab.b, ab.len);
            abFree(&ab);

            // Process input
            int c = editorReadKey();
            switch (c) {
                case '\x1b': // ESC
                    for(int i=0; i<num_items; i++) free(items[i]);
                    free(items);
                    free(path);
                    editorSetStatusMessage("");
                    return NULL;
                case '\r': { // ENTER
                    struct stat st;
                    if (stat(items[selected], &st) == 0) {
                        if (S_ISDIR(st.st_mode)) {
                            free(path);
                            path = realpath(items[selected], NULL);
                            for(int i=0; i<num_items; i++) free(items[i]);
                            free(items);
                            selected = 0;
                            scroll_offset = 0;
                            browser_done = 1; // Exit inner loop to repopulate
                        } else {
                            char *result = strdup(items[selected]);
                            for(int i=0; i<num_items; i++) free(items[i]);
                            free(items);
                            free(path);
                            return result;
                        }
                    }
                } break;
                case ARROW_UP:
                    if (selected > 0) selected--;
                    if (selected < scroll_offset) scroll_offset = selected;
                    break;
                case ARROW_DOWN:
                    if (selected < num_items - 1) selected++;
                    if (selected >= scroll_offset + E.screenrows -1) scroll_offset++;
                    break;
                case PAGE_UP:
                    selected -= E.screenrows -1;
                    if (selected < 0) selected = 0;
                    scroll_offset = selected;
                    break;
                case PAGE_DOWN:
                    selected += E.screenrows -1;
                    if (selected >= num_items) selected = num_items -1;
                    scroll_offset = selected - (E.screenrows - 2);
                    if (scroll_offset < 0) scroll_offset = 0;
                    break;
            }
        }
    }
}

/* help screen */
/**
 * @brief Displays a help screen with the main commands.
 */
void editorShowHelp() {
    const char *help_lines[] = {
        "Wee Editor - Help",
        "",
        "Version: " WEE_VERSION,
        "Author: anidisc",
        "Website: wee.anidisc.it",
        "",
        "--- Keybindings ---",
        "Ctrl-S: Save file",
        "Ctrl-Y: Save As...",
        "Ctrl-O: Open file (File Browser)",
        "Ctrl-T: New empty file",
        "Ctrl-Q: Quit",
        "",
        "Ctrl-F: Find text",
        "Ctrl-N: Toggle line numbers",
        "",
        "Ctrl-A: Select all text",
        "Ctrl-W: Copy line",
        "Ctrl-K: Cut line",
        "Ctrl-U: Paste line",
        "",
        "Ctrl-G: Show this help screen",
        "",
        "Press ESC, Q, or Ctrl-G to close this screen.",
        NULL
    };

    struct abuf ab = ABUF_INIT;
    abAppend(&ab, "\x1b[2J", 4);
    abAppend(&ab, "\x1b[H", 3);

    for (int i = 0; help_lines[i] != NULL; i++) {
        int padding = (E.screencols - strlen(help_lines[i])) / 2;
        if (padding < 0) padding = 0;
        while (padding--) abAppend(&ab, " ", 1);
        abAppend(&ab, help_lines[i], strlen(help_lines[i]));
        abAppend(&ab, "\r\n", 2);
    }

    write(STDOUT_FILENO, ab.b, ab.len);
    abFree(&ab);

    while (1) {
        int c = editorReadKey();
        if (c == '\x1b' || c == 'q' || c == CTRL_KEY('g')) {
            break;
        }
    }
}


/* init */

/**
 * @brief Initializes the global state of the editor.
 */
void initEditor() {
  E.cx = 0; E.cy = 0; E.rx = 0;
  E.rowoff = 0; E.coloff = 0;
  E.numrows = 0; E.row = NULL;
  E.filename = NULL;
  E.statusmsg[0] = '\0';
  E.statusmsg_time = 0;
  E.dirty = 0;
  E.linenumbers = 0;
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
  E.mode = NORMAL_MODE; // Initialize to normal mode

  if (getWindowSize(&E.screenrows, &E.screencols) == -1) die("getWindowSize");
  E.screenrows -= 2;
}

/**
 * @brief Main function of the program.
 * @param argc Number of command line arguments.
 * @param argv Vector of command line argument strings.
 * @return 0 on success.
 */
int main(int argc, char *argv[]) {
  enableRawMode();
  initEditor();
  if (argc >= 2) {
    editorOpen(argv[1]);
  }
  editorSetStatusMessage(
      "HELP: Ctrl-S Save | Ctrl-Q Quit | Ctrl-F Find | Ctrl-G Help");
  while (1) {
    editorRefreshScreen();
    editorProcessKeypress();
  }
  return 0;
}
