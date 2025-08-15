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

/* defines */

#define WEE_VERSION "0.7.3"
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

/* data */

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
  int linenumbers;
  char *clipboard;
  int clipboard_len;
  int hl_row;
  int hl_start;
  int hl_end;
};

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
void editorShowHelp();

/* terminal */

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

/* row operations */

int editorRowCxToRx(erow *row, int cx) {
  int rx = 0;
  for (int j = 0; j < cx; j++) {
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

/* editor operations */

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

void editorPaste() {
  if (!E.clipboard) return;
  if (E.cy == E.numrows) {
    editorInsertRow(E.numrows, "", 0);
  }
  erow *row = &E.row[E.cy];
  for (int i = 0; i < E.clipboard_len; i++) {
    editorRowInsertChar(row, E.cx + i, E.clipboard[i]);
  }
  E.cx += E.clipboard_len;
  editorSetStatusMessage("Pasted.");
}

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

    free(E.filename);
    E.filename = NULL;
    E.dirty = 0;

    editorSetStatusMessage("New empty file. Ctrl-S to save.");
}

/* find */
void editorFindCallback(char *query, int key) {
  static int last_match = -1;
  static int direction = 1;

  if (key == '\r' || key == '\x1b') {
    last_match = -1;
    direction = 1;
    E.hl_row = -1;
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
      E.hl_row = current;
      E.hl_start = match - row->render;
      E.hl_end = E.hl_start + strlen(query);
      break;
    }
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
  E.hl_row = -1;
}

/* append buffer */

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

void abFree(struct abuf *ab) { free(ab->b); }

/* output */

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

void editorScroll() {
  E.rx = 0;
  if (E.cy < E.numrows) E.rx = editorRowCxToRx(&E.row[E.cy], E.cx);
  if (E.cy < E.rowoff) E.rowoff = E.cy;
  if (E.cy >= E.rowoff + E.screenrows) E.rowoff = E.cy - E.screenrows + 1;
  int text_cols = editorGetTextCols();
  if (E.rx < E.coloff) E.coloff = E.rx;
  if (E.rx >= E.coloff + text_cols) E.coloff = E.rx - text_cols + 1;
}

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
      if (filerow == E.hl_row) {
          int start = E.hl_start > E.coloff ? E.hl_start - E.coloff : 0;
          int end = E.hl_end > E.coloff ? E.hl_end - E.coloff : 0;
          if (end > len) end = len;
          if (start < len && start < end) {
              abAppend(ab, c, start);
              abAppend(ab, "\x1b[7m", 4);
              abAppend(ab, &c[start], end - start);
              abAppend(ab, "\x1b[m", 3);
              abAppend(ab, &c[end], len - end);
          } else {
              abAppend(ab, c, len);
          }
      } else {
          abAppend(ab, c, len);
      }
    }
    abAppend(ab, "\x1b[K", 3);
    abAppend(ab, "\r\n", 2);
  }
}

void editorDrawStatusBar(struct abuf *ab) {
  abAppend(ab, "\x1b[7m", 4);
  char status[80], rstatus[80];
  int len = snprintf(status, sizeof(status), "%.20s - %d lines %s",
                    E.filename ? E.filename : "[No Name]", E.numrows,
                    E.dirty ? "(modified)" : "");
  int rlen = snprintf(rstatus, sizeof(rstatus), "%d/%d", E.cy + 1, E.numrows);
  if (len > E.screencols) len = E.screencols;
  abAppend(ab, status, len);
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

void editorSetStatusMessage(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(E.statusmsg, sizeof(E.statusmsg), fmt, ap);
  va_end(ap);
  E.statusmsg_time = time(NULL);
}

/* input */

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

void editorProcessKeypress() {
  static int quit_times = WEE_QUIT_TIMES;
  int c = editorReadKey();
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
    case CTRL_KEY('w'): editorCopyLine(); break;
    case CTRL_KEY('k'): editorCutLine(); break;
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
    case PAGE_DOWN: {
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
      break;
    default:
      editorInsertChar(c);
      break;
  }
  quit_times = WEE_QUIT_TIMES;
}

/* file browser */

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
            free(path);
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
void editorShowHelp() {
    const char *help_lines[] = {
        "Wee Editor - Help",
        "",
        "Version: " WEE_VERSION,
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

  if (getWindowSize(&E.screenrows, &E.screencols) == -1) die("getWindowSize");
  E.screenrows -= 2;
}

int main(int argc, char *argv[]) {
  enableRawMode();
  initEditor();
  if (argc >= 2) {
    editorOpen(argv[1]);
  }
  editorSetStatusMessage(
      "HELP: C-S=save | C-Y=save as | C-T=new | C-O=open | C-Q=quit | C-F=find | C-N=lines | C-W=copy | C-K=cut | C-U=paste | C-G=help");
  while (1) {
    editorRefreshScreen();
    editorProcessKeypress();
  }
  return 0;
}
