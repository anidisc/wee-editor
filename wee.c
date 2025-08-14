/*** includes ***/

#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

/*** defines ***/

#define WEE_VERSION "0.3.2"
#define WEE_TAB_STOP 8
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

/*** data ***/

typedef struct erow {
  int size;
  char *chars;
  int rsize;
  char *render;
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
  char statusmsg[80];
  time_t statusmsg_time;
  struct termios orig_termios;
  int dirty;
  int linenumbers; // New: 0 for disabled, 1 for enabled
  char *clipboard; // Clipboard content
  int clipboard_len; // Clipboard content length
  int hl_row;   // Row of the highlighted match
  int hl_start; // Start column (in render) of the highlighted match
  int hl_end;   // End column (in render) of the highlighted match
};

struct editorConfig E;

/*** prototypes ***/

void editorSetStatusMessage(const char *fmt, ...);
void editorRefreshScreen();
char *editorPrompt(char *prompt, void (*callback)(char *, int));
void editorMoveCursor(int key);
int editorGetTextCols(); // New: Get effective text columns
void editorSaveAs(); // Added for Save As functionality
void editorCopyLine();
void editorCutLine();
void editorPaste();
void editorOpenPrompt(); // New: Open file prompt
int editorAskToSave(); // New: Ask user to save unsaved changes
int editorRowRxToCx(erow *row, int rx); // New: Convert render index to char index

/*** terminal ***/

void die(const char *s) {
  write(STDOUT_FILENO, "\x1b[2J", 4);
  write(STDOUT_FILENO, "\x1b[H", 3);
  perror(s);
  exit(1);
}

void disableRawMode() {
  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_termios) == -1)
    die("tcsetattr");
}

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

int editorReadKey() {
  int nread;
  char c;
  nread = read(STDIN_FILENO, &c, 1);
  if (nread != 1) {
    if (nread == -1 && errno != EAGAIN) die("read");
    return -1;
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

int getWindowSize(int *rows, int *cols) {
  struct winsize ws;
  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
    return -1;
  } else {
    *cols = ws.ws_col;
    *rows = ws.ws_row;
    return 0;
  }
}

/*** row operations ***/

int editorRowCxToRx(erow *row, int cx) {
  int rx = 0;
  int j;
  for (j = 0; j < cx; j++) {
    if (row->chars[j] == '\t')
      rx += (WEE_TAB_STOP - 1) - (rx % WEE_TAB_STOP);
    rx++;
  }
  return rx;
}

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

void editorUpdateRow(erow *row) {
  int tabs = 0;
  int j;
  for (j = 0; j < row->size; j++)
    if (row->chars[j] == '\t') tabs++;
  free(row->render);
  row->render = malloc(row->size + tabs * (WEE_TAB_STOP - 1) + 1);
  int idx = 0;
  for (j = 0; j < row->size; j++) {
    if (row->chars[j] == '\t') {
      row->render[idx++] = ' ';
      while (idx % WEE_TAB_STOP != 0) row->render[idx++] = ' ';
    } else {
      row->render[idx++] = row->chars[j];
    }
  }
  row->render[idx] = '\0';
  row->rsize = idx;
}

void editorInsertRow(int at, char *s, size_t len) {
  if (at < 0 || at > E.numrows) return;
  E.row = realloc(E.row, sizeof(erow) * (E.numrows + 1));
  memmove(&E.row[at + 1], &E.row[at], sizeof(erow) * (E.numrows - at));
  E.row[at].size = len;
  E.row[at].chars = malloc(len + 1);
  memcpy(E.row[at].chars, s, len);
  E.row[at].chars[len] = '\0';
  E.row[at].rsize = 0;
  E.row[at].render = NULL;
  editorUpdateRow(&E.row[at]);
  E.numrows++;
  E.dirty++;
}

void editorFreeRow(erow *row) {
  free(row->render);
  free(row->chars);
}

void editorDelRow(int at) {
  if (at < 0 || at >= E.numrows) return;
  editorFreeRow(&E.row[at]);
  memmove(&E.row[at], &E.row[at + 1], sizeof(erow) * (E.numrows - at - 1));
  E.numrows--;
  E.dirty++;
}

void editorRowInsertChar(erow *row, int at, int c) {
  if (at < 0 || at > row->size) at = row->size;
  row->chars = realloc(row->chars, row->size + 2);
  memmove(&row->chars[at + 1], &row->chars[at], row->size - at + 1);
  row->size++;
  row->chars[at] = c;
  editorUpdateRow(row);
  E.dirty++;
}

void editorRowAppendString(erow *row, char *s, size_t len) {
  row->chars = realloc(row->chars, row->size + len + 1);
  memcpy(&row->chars[row->size], s, len);
  row->size += len;
  row->chars[row->size] = '\0';
  editorUpdateRow(row);
  E.dirty++;
}

void editorRowDelChar(erow *row, int at) {
  if (at < 0 || at >= row->size) return;
  memmove(&row->chars[at], &row->chars[at + 1], row->size - at);
  row->size--;
  editorUpdateRow(row);
  E.dirty++;
}

/*** editor operations ***/

void editorInsertChar(int c) {
  if (E.cy == E.numrows) {
    editorInsertRow(E.numrows, "", 0);
  }
  editorRowInsertChar(&E.row[E.cy], E.cx, c);
  E.cx++;
}

void editorInsertNewline() {
  if (E.cx == 0) {
    editorInsertRow(E.cy, "", 0);
  } else {
    erow *row = &E.row[E.cy];
    editorInsertRow(E.cy + 1, &row->chars[E.cx], row->size - E.cx);
    row = &E.row[E.cy];
    row->size = E.cx;
    row->chars[row->size] = '\0';
    editorUpdateRow(row);
  }
  E.cy++;
  E.cx = 0;
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

/*** file i/o ***/

char *editorRowsToString(int *buflen) {
  int totlen = 0;
  int j;
  for (j = 0; j < E.numrows; j++)
    totlen += E.row[j].size + 1;
  *buflen = totlen;
  char *buf = malloc(totlen);
  char *p = buf;
  for (j = 0; j < E.numrows; j++) {
    memcpy(p, E.row[j].chars, E.row[j].size);
    p += E.row[j].size;
    *p = '\n';
    p++;
  }
  return buf;
}

void editorOpen(char *filename) {
  char *new_filename_dup = strdup(filename);
  FILE *fp = fopen(new_filename_dup, "r");
  if (!fp) {
    editorSetStatusMessage("Error: Could not open file %s: %s", filename, strerror(errno));
    free(new_filename_dup);
    return; // Do not clear screen, just return
  }

  if (!editorAskToSave()) {
    fclose(fp);
    free(new_filename_dup);
    return;
  }

  // If we reach here, file was successfully opened. Now clear current content.
  for (int i = 0; i < E.numrows; i++) {
    editorFreeRow(&E.row[i]);
  }
  if (E.row) {
    free(E.row);
  }
  E.row = NULL;
  E.numrows = 0;
  E.cx = 0;
  E.cy = 0;
  E.rowoff = 0;
  E.coloff = 0;

  // Update E.filename
  if (E.filename) {
      free(E.filename);
  }
  E.filename = new_filename_dup;

  char *line = NULL;
  size_t linecap = 0;
  ssize_t linelen;
  while ((linelen = getline(&line, &linecap, fp)) != -1) {
    while (linelen > 0 && (line[linelen - 1] == '\n' ||
                           line[linelen - 1] == '\r'))
      linelen--;
    editorInsertRow(E.numrows, line, linelen);
  }
  free(line);
  fclose(fp);
  E.dirty = 0;
  editorSetStatusMessage("%s opened.", filename);
}

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
    if (ftruncate(fd, len) != -1) {
      if (write(fd, buf, len) == len) {
        close(fd);
        free(buf);
        E.dirty = 0;
        editorSetStatusMessage("%d bytes written to disk", len);
        return;
      }
    }
    close(fd);
  }
  free(buf);
  editorSetStatusMessage("Can't save! I/O error: %s", strerror(errno));
}

void editorSaveAs() {
  char *new_filename = editorPrompt("Save as: %s (ESC to cancel)", NULL);
  if (new_filename == NULL) {
    editorSetStatusMessage("Save As aborted");
    return;
  }
  if (E.filename) {
    free(E.filename);
  }
  E.filename = new_filename;
  editorSave();
}

void editorCopyLine() {
  if (E.cy >= E.numrows) return; // Nothing to copy if no line
  erow *row = &E.row[E.cy];

  if (E.clipboard) {
    free(E.clipboard);
    E.clipboard = NULL;
    E.clipboard_len = 0;
  }

  E.clipboard_len = row->size;
  E.clipboard = malloc(E.clipboard_len + 1);
  if (!E.clipboard) die("malloc");
  memcpy(E.clipboard, row->chars, E.clipboard_len);
  E.clipboard[E.clipboard_len] = '\0';

  editorSetStatusMessage("Line copied.");
}

void editorCutLine() {
  if (E.cy >= E.numrows) return; // Nothing to cut if no line

  editorCopyLine(); // Copy the line to clipboard

  editorDelRow(E.cy); // Delete the current line

  // Adjust cursor if we cut the last line
  if (E.cy >= E.numrows && E.numrows > 0) {
    E.cy = E.numrows - 1;
    E.cx = E.row[E.cy].size;
  } else if (E.numrows == 0) {
    E.cy = 0;
    E.cx = 0;
  }

  editorSetStatusMessage("Line cut.");
}

void editorPaste() {
  if (!E.clipboard) return; // Nothing to paste

  if (E.cy == E.numrows) {
    editorInsertRow(E.numrows, "", 0); // Insert a new line if at end of file
  }

  // Insert clipboard content into the current line
  erow *row = &E.row[E.cy];
  // This assumes clipboard content is a single line. If it contains newlines,
  // this would need to be more complex (e.g., splitting and inserting rows).
  // For now, it inserts the content into the current line.
  for (int i = 0; i < E.clipboard_len; i++) {
    editorRowInsertChar(row, E.cx + i, E.clipboard[i]);
  }
  E.cx += E.clipboard_len; // Move cursor

  editorSetStatusMessage("Pasted.");
}

void editorOpenPrompt() {
  char *filename = editorPrompt("Open file: %s (ESC to cancel)", NULL);
  if (filename == NULL) {
    editorSetStatusMessage("Open aborted.");
    return;
  }
  editorOpen(filename);
  free(filename); // editorOpen makes a copy
}

int editorAskToSave() {
  if (!E.dirty) return 1; // No unsaved changes, safe to proceed

  editorSetStatusMessage("WARNING! File has unsaved changes. Press Ctrl-S to save, ESC to cancel, or Ctrl-D to discard.");
  editorRefreshScreen(); // Refresh to show the warning

  while (1) { // Loop until a valid choice is made
    int c = editorReadKey();
    if (c == -1) { // Timeout, refresh and wait again
      editorRefreshScreen();
      continue;
    }

    if (c == CTRL_KEY('s')) {
      editorSave();
      return !E.dirty; // Return 1 if saved successfully (dirty is now 0), 0 otherwise
    } else if (c == CTRL_KEY('d')) {
      E.dirty = 0; // Discard changes
      editorSetStatusMessage("Changes discarded.");
      return 1;
    } else if (c == '\x1b') {
      editorSetStatusMessage("Operation aborted.");
      return 0;
    } else {
      // Invalid choice, keep prompting
      editorSetStatusMessage("WARNING! File has unsaved changes. Press Ctrl-S to save, ESC to cancel, or Ctrl-D to discard. (Invalid choice)");
      editorRefreshScreen();
    }
  }
}

/*** find ***/

void editorFindCallback(char *query, int key) {
  static int last_match = -1;
  static int direction = 1;
  static int saved_hl_row = -1; // To restore highlighting after search

  // Clear highlighting from previous search
  if (saved_hl_row != -1) {
    E.hl_row = -1;
    saved_hl_row = -1;
  }

  if (key == '\r' || key == '\x1b') {
    last_match = -1;
    direction = 1;
    E.hl_row = -1; // Clear highlighting
    return;
  } else if (key == ARROW_RIGHT || key == ARROW_DOWN) {
    direction = 1;
  } else if (key == ARROW_LEFT || key == ARROW_UP) {
    direction = -1;
  } else {
    last_match = -1; // New search query, reset
    direction = 1;
  }

  if (last_match == -1) {
    direction = 1;
    last_match = E.cy; // Start search from current cursor position
  }

  int current = last_match;
  int i;
  for (i = 0; i < E.numrows; i++) {
    current += direction;
    if (current == -1) current = E.numrows - 1;
    else if (current == E.numrows) current = 0;

    erow *row = &E.row[current];
    char *match = strstr(row->render, query);
    if (match) {
      last_match = current;
      E.cy = current;
      E.cx = editorRowRxToCx(row, match - row->render); // Convert render index to char index
      E.rowoff = E.numrows; // Scroll to bottom to ensure visibility

      E.hl_row = current;
      E.hl_start = match - row->render;
      E.hl_end = E.hl_start + strlen(query);
      saved_hl_row = current; // Save for clearing later

      editorSetStatusMessage("Search result: %s", query);
      break;
    }
  }
}

void editorFind() {
  int saved_cx = E.cx;
  int saved_cy = E.cy;
  int saved_coloff = E.coloff;
  int saved_rowoff = E.rowoff;

  char *query = editorPrompt("Search: %s (Use ESC/Arrows/Enter)", editorFindCallback);
  if (query) {
    free(query);
    // If query is not NULL, it means Enter was pressed and a match was found
    // Do not restore saved cursor position, keep the found position
  } else {
    // If query is NULL, it means ESC was pressed or no match found
    E.cx = saved_cx;
    E.cy = saved_cy;
    E.coloff = saved_coloff;
    E.rowoff = saved_rowoff;
  }
  E.hl_row = -1; // Clear highlighting after search
}

/*** append buffer ***/

struct abuf {
  char *b;
  int len;
};

#define ABUF_INIT {NULL, 0}

void abAppend(struct abuf *ab, const char *s, int len) {
  char *new = realloc(ab->b, ab->len + len);
  if (new == NULL) return;
  memcpy(&new[ab->len], s, len);
  ab->b = new;
  ab->len += len;
}

void abFree(struct abuf *ab) {
  free(ab->b);
}

/*** output ***/

int editorGetTextCols() {
    int text_cols = E.screencols;
    if (E.linenumbers) {
        int max_linenum_digits = 1;
        if (E.numrows > 0) {
            int temp_num = E.numrows;
            while (temp_num /= 10) {
                max_linenum_digits++;
            }
        }
        int linenum_width = max_linenum_digits + 1; // +1 for space separator
        if (linenum_width < 4) linenum_width = 4; // Minimum width for small files
        text_cols -= linenum_width;
    }
    return text_cols;
}

void editorScroll() {
  E.rx = 0;
  if (E.cy < E.numrows) {
    E.rx = editorRowCxToRx(&E.row[E.cy], E.cx);
  }
  if (E.cy < E.rowoff) {
    E.rowoff = E.cy;
  }
  if (E.cy >= E.rowoff + E.screenrows) {
    E.rowoff = E.cy - E.screenrows + 1;
  }
  int text_cols = editorGetTextCols(); // Use effective text columns
  if (E.rx < E.coloff) {
    E.coloff = E.rx;
  }
  if (E.rx >= E.coloff + text_cols) { // Use effective text columns
    E.coloff = E.rx - text_cols + 1;
  }
}

void editorDrawRows(struct abuf *ab) {
  int y;
  int linenum_width = 0;
  if (E.linenumbers) {
    // Calculate width needed for line numbers
    int max_linenum_digits = 1;
    if (E.numrows > 0) {
        int temp_num = E.numrows;
        while (temp_num /= 10) {
            max_linenum_digits++;
        }
    }
    linenum_width = max_linenum_digits + 1; // +1 for space separator
    if (linenum_width < 4) linenum_width = 4; // Minimum width for small files
  }

  for (y = 0; y < E.screenrows; y++) {
    int filerow = y + E.rowoff;

    if (filerow < E.numrows) { // This block handles actual file content
      if (E.linenumbers) {
        char linenum_buf[16];
        int len = snprintf(linenum_buf, sizeof(linenum_buf), "%*d ", linenum_width - 1, filerow + 1);
        abAppend(ab, "\x1b[36m", 5); // Cyan color for line numbers
        abAppend(ab, linenum_buf, len);
        abAppend(ab, "\x1b[m", 3); // Reset color
      }
      int len = E.row[filerow].rsize - E.coloff;
      if (len < 0) len = 0;
      if (len > E.screencols - linenum_width) len = E.screencols - linenum_width;
      // Highlighting
      if (filerow == E.hl_row) {
        char *c = &E.row[filerow].render[E.coloff];
        int highlight_len = E.hl_end - E.hl_start;

        // Calculate visible part of highlight
        int visible_hl_start = E.hl_start;
        int visible_hl_end = E.hl_end;

        if (visible_hl_start < E.coloff) visible_hl_start = E.coloff;
        if (visible_hl_end > E.coloff + len) visible_hl_end = E.coloff + len;

        if (visible_hl_start < visible_hl_end) {
            // Text before highlight
            abAppend(ab, c, visible_hl_start - E.coloff);
            // Highlighted text
            abAppend(ab, "\x1b[7m", 4); // Inverted colors
            abAppend(ab, &E.row[filerow].render[visible_hl_start], visible_hl_end - visible_hl_start);
            abAppend(ab, "\x1b[m", 3); // Reset colors
            // Text after highlight
            abAppend(ab, &E.row[filerow].render[visible_hl_end], (E.coloff + len) - visible_hl_end);
        } else {
            abAppend(ab, c, len);
        }
      } else {
        abAppend(ab, &E.row[filerow].render[E.coloff], len);
      }
    } else { // This block handles empty lines (~) or welcome message
      if (E.linenumbers) { // Print spaces for line number column if line numbers are enabled
        char spaces[16];
        memset(spaces, ' ', linenum_width);
        abAppend(ab, spaces, linenum_width);
      }
      if (E.numrows == 0 && y == E.screenrows / 3) {
        char welcome[80];
        int welcomelen = snprintf(welcome, sizeof(welcome),
          "Wee editor -- version %s", WEE_VERSION);
        if (welcomelen > E.screencols - linenum_width) welcomelen = E.screencols - linenum_width;
        int padding = (E.screencols - linenum_width - welcomelen) / 2;
        if (padding) {
          abAppend(ab, "~", 1);
          padding--;
        }
        while (padding--) abAppend(ab, " ", 1);
        abAppend(ab, welcome, welcomelen);
      } else {
        abAppend(ab, "~", 1);
      }
    }
    abAppend(ab, "\x1b[K", 3);
    abAppend(ab, "\r\n", 2);
  }
}

void editorDrawStatusBar(struct abuf *ab) {
  abAppend(ab, "\x1b[7m", 4);
  char rstatus[80];
  int rlen = snprintf(rstatus, sizeof(rstatus), "%d/%d", E.cy + 1, E.numrows);

  const char *filename = E.filename ? E.filename : "[No Name]";
  int filename_len = strnlen(filename, 20);

  char info[80];
  int info_len = snprintf(info, sizeof(info), " - %d lines %s", E.numrows, E.dirty ? "(modified)" : "");

  int len = filename_len + info_len;

  // Append filename in Bold Red
  abAppend(ab, "\x1b[1;31m", 7);
  abAppend(ab, filename, filename_len);
  abAppend(ab, "\x1b[m", 3);
  abAppend(ab, "\x1b[7m", 4);

  // Append the rest of the info
  abAppend(ab, info, info_len);

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

void editorDrawMessageBar(struct abuf *ab) {
  abAppend(ab, "\x1b[K", 3);
  int msglen = strlen(E.statusmsg);
  if (msglen > E.screencols) msglen = E.screencols;
  if (msglen && time(NULL) - E.statusmsg_time < 5)
    abAppend(ab, E.statusmsg, msglen);
}

void editorRefreshScreen() {
  editorScroll();
  struct abuf ab = ABUF_INIT;
  abAppend(&ab, "\x1b[?25l", 6);
  abAppend(&ab, "\x1b[H", 3);
  editorDrawRows(&ab);
  editorDrawStatusBar(&ab);
  editorDrawMessageBar(&ab);
  int linenum_width = 0;
  if (E.linenumbers) {
    int max_linenum_digits = 1;
    if (E.numrows > 0) {
        int temp_num = E.numrows;
        while (temp_num /= 10) {
            max_linenum_digits++;
        }
    }
    linenum_width = max_linenum_digits + 1;
    if (linenum_width < 4) linenum_width = 4;
  }

  char buf[32];
  snprintf(buf, sizeof(buf), "\x1b[%d;%dH", (E.cy - E.rowoff) + 1,
                                            (E.rx - E.coloff) + 1 + linenum_width);
  abAppend(&ab, buf, strlen(buf));
  abAppend(&ab, "\x1b[?25h", 6);
  write(STDOUT_FILENO, ab.b, ab.len);
  abFree(&ab);
}

void editorSetStatusMessage(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(E.statusmsg, sizeof(E.statusmsg), fmt, ap);
  va_end(ap);
  E.statusmsg_time = time(NULL);
}

/*** input ***/

char *editorPrompt(char *prompt, void (*callback)(char *, int)) {
  size_t bufsize = 128;
  char *buf = malloc(bufsize);
  size_t buflen = 0;
  buf[0] = '\0';
  while (1) {
    editorSetStatusMessage(prompt, buf);
    editorRefreshScreen();
    int c = editorReadKey();
    if (c == -1) continue;
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

void editorMoveCursor(int key) {
  erow *row = (E.cy >= E.numrows) ? NULL : &E.row[E.cy];
  switch (key) {
    case ARROW_LEFT:
      if (E.cx != 0) {
        E.cx--;
      } else if (E.cy > 0) {
        E.cy--;
        E.cx = E.row[E.cy].size;
      }
      break;
    case ARROW_RIGHT:
      if (row && E.cx < row->size) {
        E.cx++;
      } else if (row && E.cx == row->size) {
        E.cy++;
        E.cx = 0;
      }
      break;
    case ARROW_UP:
      if (E.cy != 0) {
        E.cy--;
      }
      break;
    case ARROW_DOWN:
      if (E.cy < E.numrows) {
        E.cy++;
      }
      break;
  }
  row = (E.cy >= E.numrows) ? NULL : &E.row[E.cy];
  int rowlen = row ? row->size : 0;
  if (E.cx > rowlen) {
    E.cx = rowlen;
  }
}

void editorProcessKeypress() {
  static int quit_times = WEE_QUIT_TIMES;
  int c = editorReadKey();
  if (c == -1) return;
  switch (c) {
    case '\r':
      editorInsertNewline();
      break;
    case CTRL_KEY('q'):
      if (!editorAskToSave()) {
        return; // User aborted or save failed
      }
      write(STDOUT_FILENO, "\x1b[2J", 4);
      write(STDOUT_FILENO, "\x1b[H", 3);
      exit(0);
      break;
    case CTRL_KEY('s'):
      editorSave();
      break;
    case CTRL_KEY('y'): // Save As
      editorSaveAs();
      break;
    case CTRL_KEY('w'): // Copy
      editorCopyLine();
      break;
    case CTRL_KEY('k'): // Cut
      editorCutLine();
      break;
    case CTRL_KEY('u'): // Paste
      editorPaste();
      break;
    case CTRL_KEY('o'): // Open file
      editorOpenPrompt();
      break;
    case HOME_KEY:
      E.cx = 0;
      break;
    case END_KEY:
       if (E.cy < E.numrows)
        E.cx = E.row[E.cy].size;
      break;
    case CTRL_KEY('f'):
      editorFind();
      break;
    case CTRL_KEY('n'): // Toggle line numbers
      E.linenumbers = !E.linenumbers;
      break;
    case BACKSPACE:
    case CTRL_KEY('h'):
    case DEL_KEY:
      if (c == DEL_KEY) editorMoveCursor(ARROW_RIGHT);
      editorDelChar();
      break;
    case PAGE_UP:
    case PAGE_DOWN:
      {
        if (c == PAGE_UP) {
          E.cy = E.rowoff;
        } else if (c == PAGE_DOWN) {
          E.cy = E.rowoff + E.screenrows - 1;
          if (E.cy > E.numrows) E.cy = E.numrows;
        }
        int times = E.screenrows;
        while (times--)
          editorMoveCursor(c == PAGE_UP ? ARROW_UP : ARROW_DOWN);
      }
      break;
    case ARROW_UP:
    case ARROW_DOWN:
    case ARROW_LEFT:
    case ARROW_RIGHT:
      editorMoveCursor(c);
      break;
    case CTRL_KEY('l'):
    case '\x1b':
      break;
    default:
      editorInsertChar(c);
      break;
  }
  quit_times = WEE_QUIT_TIMES;
}

/*** init ***/

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
  E.linenumbers = 0; // Initialize line numbers to disabled
  E.clipboard = NULL; // Initialize clipboard
  E.clipboard_len = 0; // Initialize clipboard length
  E.hl_row = -1; // Initialize highlight
  E.hl_start = -1;
  E.hl_end = -1;
  if (getWindowSize(&E.screenrows, &E.screencols) == -1) die("getWindowSize");
  E.screenrows -= 2;
}

int main(int argc, char *argv[]) {
  enableRawMode();
  initEditor();
  if (argc >= 2) {
    editorOpen(argv[1]);
  }
  editorSetStatusMessage("HELP: Ctrl-S = save | Ctrl-Y = save as | Ctrl-O = open | Ctrl-Q = quit | Ctrl-F = find | Ctrl-N = toggle line numbers | Ctrl-W = copy | Ctrl-K = cut | Ctrl-U = paste");
  while (1) {
    editorRefreshScreen();
    editorProcessKeypress();
  }
  return 0;
}
