// https://github.com/JaDogg/WKilo
/*** includes ***/
#define _CRT_SECURE_NO_WARNINGS
#include <time.h>
#include "myfs.h"
#include <sstream>
#include <type_traits>
#include <cassert>
#include <algorithm>
#include <stdexcept>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <utility>
#include "config.h"
/*** defines ***/

#define CTRL_KEY(k) ((k) & 0x1f)

#if defined(_MSC_VER) // MSVC
#define STRDUP _strdup
#else
#define STRDUP strdup
#endif

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

enum editorHighlight : uint8_t {
	HL_NORMAL = 0,
	HL_COMMENT,
	HL_MLCOMMENT,
	HL_KEYWORD1,
	HL_KEYWORD2,
	HL_STRING,
	HL_NUMBER,
	HL_MATCH
};

#define HL_HIGHLIGHT_NUMBERS (1 << 0)
#define HL_HIGHLIGHT_STRINGS (1 << 1)

/*** data ***/

struct editorSyntax {
	const char* filetype;
	const char** filematch;
	const char** keywords;
	const char* singleline_comment_start;
	const char* multiline_comment_start;
	const char* multiline_comment_end;
	int flags;
};

typedef struct erow {
	int idx;
	int size;
	int rsize;
	char* chars;
	char* render;
	editorHighlight* hl;
	bool hl_open_comment;
} erow;

struct editorConfig {
	int cx, cy;
	int rx;
	int rowoff;
	int coloff;
	int screenrows;
	int screencols;
	int numrows;
	erow* row;
	bool dirty;
	char* filename;
	char statusmsg[80];
	time_t statusmsg_time;
	struct editorSyntax* syntax;
	MyFs* myfs;
	// NOTE: See below item is commented out
};

struct editorConfig E;

/*** filetypes ***/

const char* C_HL_extensions[] = {".c", ".h", ".cpp", nullptr};
const char* C_HL_keywords[] = {"switch", "if",	  "while",	 "for",	   "break", "continue",	 "return",	"else",
							   "struct", "union", "typedef", "static", "enum",	"class",	 "case",

							   "int|",	 "long|", "double|", "float|", "char|", "unsigned|", "signed|", "void|",
							   nullptr};

struct editorSyntax HLDB[] = {
	{"c", C_HL_extensions, C_HL_keywords, "//", "/*", "*/", HL_HIGHLIGHT_NUMBERS | HL_HIGHLIGHT_STRINGS},
};

#define HLDB_ENTRIES (sizeof(HLDB) / sizeof(HLDB[0]))

#ifdef _WIN32
#pragma region WINDOWS STUFF
#define _CRT_SECURE_NO_WARNINGS
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#undef DELETE
#include <io.h>

#ifndef STDOUT_FILENO
#define STDOUT_FILENO 1
#endif
#ifndef STDIN_FILENO
#define STDIN_FILENO 0
#endif
// Some code taken from - https://github.com/microsoft/terminal/issues/8820

static HANDLE hStdin = nullptr;
static HANDLE hStdout = nullptr;
static int savedConsoleOutputModeIsValid = 0;
static DWORD savedConsoleOutputMode = 0;
static int savedConsoleInputModeIsValid = 0;
static DWORD savedConsoleInputMode = 0;

#define write(fd, data, num) winWrite((fd), (char*)(data), (num))
#define read winRead

void disableRawMode() {
	printf("\x1b[0m");
	fflush(stdout);

	if (savedConsoleOutputModeIsValid)
		SetConsoleMode(hStdout, savedConsoleOutputMode);
	if (savedConsoleInputModeIsValid)
		SetConsoleMode(hStdin, savedConsoleInputMode);
}

int enableRawMode() {
	// Make sure the console state will be returned to its original state
	// when this program ends
	atexit(disableRawMode);

	// Get handles for stdin and stdout
	hStdin = GetStdHandle(STD_INPUT_HANDLE);
	if (hStdin == INVALID_HANDLE_VALUE || hStdin == nullptr)
		return 1;
	hStdout = GetStdHandle(STD_OUTPUT_HANDLE);
	if (hStdout == INVALID_HANDLE_VALUE || hStdout == nullptr)
		return 1;

	// Set console to "raw" mode

	if (!GetConsoleMode(hStdout, &savedConsoleOutputMode))
		return 1;
	savedConsoleOutputModeIsValid = 1;
	DWORD newOutputMode = savedConsoleOutputMode;
	newOutputMode |= ENABLE_PROCESSED_OUTPUT;
	newOutputMode &= ~ENABLE_WRAP_AT_EOL_OUTPUT;
	newOutputMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
	if (!SetConsoleMode(hStdout, newOutputMode))
		return 1;
	if (!GetConsoleMode(hStdin, &savedConsoleInputMode))
		return 1;
	savedConsoleInputModeIsValid = 1;
	DWORD newInputMode = savedConsoleInputMode;
	newInputMode &= ~ENABLE_ECHO_INPUT;
	newInputMode &= ~ENABLE_LINE_INPUT;
	newInputMode &= ~ENABLE_PROCESSED_INPUT;
	newInputMode |= ENABLE_VIRTUAL_TERMINAL_INPUT;
	if (!SetConsoleMode(hStdin, newInputMode))
		return 1;

	return 0;
}

// https://stackoverflow.com/questions/6812224/getting-terminal-size-in-c-for-windows
int getWindowSize(int* rows, int* cols) {
	CONSOLE_SCREEN_BUFFER_INFO csbi;

	GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi);
	*cols = csbi.srWindow.Right - csbi.srWindow.Left + 1;
	*rows = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
	return 0;
}

// Following code uses the Windows api to read and write to console
//  instead the C library functions
//  below stuff works as expected.

int winRead(int ignored, char* c, int toread) {
	(void)ignored;
	unsigned long readen = 0;
	ReadConsoleA(hStdin, c, toread, &readen, nullptr);
	return (int)readen;
}

int winWrite(int ignored, char* buf, int length) {
	(void)ignored;
	unsigned long wrote = 0;
	WriteConsoleA(hStdout, buf, length, &wrote, nullptr);
	return (int)wrote;
}

#pragma endregion
#else
#define _POSIX_C_SOURCE 200809L
#include <termios.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <fcntl.h>
#include <signal.h>

static struct termios orig_termios;
int getCursorPosition(int* rows, int* cols);
void die(const char* s);

int enableRawMode() {
	int fd = STDIN_FILENO;
	struct termios raw;

	if (!isatty(STDIN_FILENO))
		goto fatal;
	if (tcgetattr(fd, &orig_termios) == -1)
		goto fatal;

	raw = orig_termios; /* modify the original mode */
	/* input modes: no break, no CR to NL, no parity check, no strip char,
	 * no start/stop output control. */
	raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
	/* output modes - disable post processing */
	raw.c_oflag &= ~(OPOST);
	/* control modes - set 8 bit chars */
	raw.c_cflag |= (CS8);
	/* local modes - choing off, canonical off, no extended functions,
	 * no signal chars (^Z,^C) */
	raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
	/* control chars - set return condition: min number of bytes and timer. */
	raw.c_cc[VMIN] = 0;	 /* Return each byte, or zero for timeout. */
	raw.c_cc[VTIME] = 1; /* 100 ms timeout (unit is tens of second). */

	/* put terminal in raw mode after flushing */
	if (tcsetattr(fd, TCSAFLUSH, &raw) < 0)
		goto fatal;
	return 0;

fatal:
	errno = ENOTTY;
	return -1;
}

void disableRawMode() {
	int fd = STDIN_FILENO;
	/* Don't even check the return value as it's too late. */
	tcsetattr(fd, TCSAFLUSH, &orig_termios);
}

int getWindowSize(int* rows, int* cols) {
	struct winsize ws;
	if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
		/* ioctl() failed. Try to query the terminal itself. */
		int orig_row, orig_col, retval;

		/* Get the initial position so we can restore it later. */
		retval = getCursorPosition(&orig_row, &orig_col);
		if (retval == -1)
			goto failed;

		/* Go to right/bottom margin and get position. */
		if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12)
			goto failed;
		retval = getCursorPosition(rows, cols);
		if (retval == -1)
			goto failed;

		/* Restore position. */
		char seq[32];
		snprintf(seq, 32, "\x1b[%d;%dH", orig_row, orig_col);
		if (write(STDOUT_FILENO, seq, strlen(seq)) == -1) {
			/* Can't recover... */
		}
		return 0;
	} else {
		*cols = ws.ws_col;
		*rows = ws.ws_row;
		return 0;
	}

failed:
	return -1;
}

#define write(fd, str, len)                                                                                            \
	do {                                                                                                               \
		if (write((fd), (str), (len)) != len) {                                                                        \
			die("write");                                                                                              \
		}                                                                                                              \
	} while (0)
#endif

/*** prototypes ***/

void editorSetStatusMessage(const char* fmt, ...);
void editorRefreshScreen();
char* editorPrompt(const char* prompt, void (*callback)(char*, int));
void editorMoveCursor(int key);

/*** terminal ***/

void die(const char* s) {
	write(STDOUT_FILENO, "\x1b[2J", 4);
	write(STDOUT_FILENO, "\x1b[H", 3);

	throw std::runtime_error(s);
}

int editorReadKey() {
	int nread = 0;
	char c = '\0';
	while ((nread = (int)read(STDIN_FILENO, &c, 1)) != 1) {
		if (nread == -1 && errno != EAGAIN)
			die("read");
	}

	if (c == '\x1b') {
		char seq[3];

		if (read(STDIN_FILENO, &seq[0], 1) != 1)
			return '\x1b';
		if (read(STDIN_FILENO, &seq[1], 1) != 1)
			return '\x1b';

		if (seq[0] == '[') {
			if (seq[1] >= '0' && seq[1] <= '9') {
				if (read(STDIN_FILENO, &seq[2], 1) != 1)
					return '\x1b';
				if (seq[2] == '~') {
					switch (seq[1]) {
					case '1':
						return HOME_KEY;
					case '3':
						return DEL_KEY;
					case '4':
						return END_KEY;
					case '5':
						return PAGE_UP;
					case '6':
						return PAGE_DOWN;
					case '7':
						return HOME_KEY;
					case '8':
						return END_KEY;
					}
				}
			} else {
				switch (seq[1]) {
				case 'A':
					return ARROW_UP;
				case 'B':
					return ARROW_DOWN;
				case 'C':
					return ARROW_RIGHT;
				case 'D':
					return ARROW_LEFT;
				case 'H':
					return HOME_KEY;
				case 'F':
					return END_KEY;
				}
			}
		} else if (seq[0] == 'O') {
			switch (seq[1]) {
			case 'H':
				return HOME_KEY;
			case 'F':
				return END_KEY;
			}
		}

		return '\x1b';
	}
	return c;
}

int getCursorPosition(int* rows, int* cols) {
	char buf[32];
	unsigned int i = 0;

	write(STDOUT_FILENO, "\x1b[6n", 4);

	while (i < sizeof(buf) - 1) {
		if (read(STDIN_FILENO, &buf[i], 1) != 1)
			break;
		if (buf[i] == 'R')
			break;
		i++;
	}
	buf[i] = '\0';

	if (buf[0] != '\x1b' || buf[1] != '[')
		return -1;
	if (sscanf(&buf[2], "%d;%d", rows, cols) != 2)
		return -1;

	return 0;
}

/*** syntax highlighting ***/

int is_separator(int c) {
	return isspace(c) || c == '\0' || strchr(",.()+-/*=~%<>[];", c) != nullptr;
}

void editorUpdateSyntax(erow* row) {
	if (row->rsize == 0) {
		return;
	}
	row->hl = (editorHighlight*)realloc(row->hl, row->rsize);
	assert(row->hl != nullptr);

	memset(row->hl, HL_NORMAL, row->rsize * sizeof(editorHighlight));

	if (E.syntax == nullptr)
		return;

	const char** keywords = E.syntax->keywords;

	const char* scs = E.syntax->singleline_comment_start;
	const char* mcs = E.syntax->multiline_comment_start;
	const char* mce = E.syntax->multiline_comment_end;

	int scs_len = scs ? (int)strlen(scs) : 0;
	int mcs_len = mcs ? (int)strlen(mcs) : 0;
	int mce_len = mce ? (int)strlen(mce) : 0;

	int prev_sep = 1;
	char in_string = 0;
	bool in_comment = (row->idx > 0 && E.row[row->idx - 1].hl_open_comment);

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
					in_comment = false;
					prev_sep = 1;
					continue;
				}
				i++;

			} else if (!strncmp(&row->render[i], mcs, mcs_len)) {
				memset(&row->hl[i], HL_MLCOMMENT, mcs_len);
				i += mcs_len;
				in_comment = true;
			}
			continue;
		}

		if (E.syntax->flags & HL_HIGHLIGHT_STRINGS) {
			if (in_string) {
				row->hl[i] = HL_STRING;
				if (c == '\\' && i + 1 < row->rsize) {
					row->hl[i + 1] = HL_STRING;
					i += 2;
					continue;
				}
				if (c == in_string)
					in_string = 0;
				i++;
				prev_sep = 1;
			} else {
				if (c == '"' || c == '\'') {
					in_string = c;
					row->hl[i] = HL_STRING;
					i++;
				}
			}
			continue;
		}

		if (E.syntax->flags & HL_HIGHLIGHT_NUMBERS) {
			if ((isdigit(c) && (prev_sep || prev_hl == HL_NUMBER)) || (c == '.' && prev_hl == HL_NUMBER)) {
				if (row->rsize >= 8) {
					row->hl[i] = HL_NUMBER;
				}
				i++;
				prev_sep = 0;
				continue;
			}
		}

		if (prev_sep) {
			int j = 0;
			for (j = 0; keywords[j]; j++) {
				int klen = (int)strlen(keywords[j]);
				int kw2 = keywords[j][klen - 1] == '|';
				if (kw2)
					klen--;

				if (!strncmp(&row->render[i], keywords[j], klen) && is_separator(row->render[i + klen])) {
					memset(&row->hl[i], kw2 ? HL_KEYWORD2 : HL_KEYWORD1, klen);
					i += klen;
					break;
				}
			}
			if (keywords[j] != nullptr) {
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

int editorSyntaxToColor(int hl) {
	switch (hl) {
	case HL_COMMENT:
	case HL_MLCOMMENT:
		return 36;
	case HL_KEYWORD1:
		return 33;
	case HL_KEYWORD2:
		return 32;
	case HL_STRING:
		return 35;
	case HL_NUMBER:
		return 31;
	case HL_MATCH:
		return 34;
	default:
		return 37;
	}
}

void editorSelectSyntaxHighlight() {
	E.syntax = nullptr;
	if (E.filename == nullptr)
		return;

	char* ext = strrchr(E.filename, '.');

	for (unsigned int j = 0; j < HLDB_ENTRIES; j++) {
		struct editorSyntax* s = &HLDB[j];
		unsigned int i = 0;
		while (s->filematch[i]) {
			int is_ext = (s->filematch[i][0] == '.');
			if ((is_ext && ext && !strcmp(ext, s->filematch[i])) || (!is_ext && strstr(E.filename, s->filematch[i]))) {
				E.syntax = s;

				for (int filerow = 0; filerow < E.numrows; filerow++) {
					editorUpdateSyntax(&E.row[filerow]);
				}

				return;
			}
			i++;
		}
	}
}

/*** row operations ***/

int editorRowCxToRx(erow* row, int cx) {
	int rx = 0;
	int j = 0;
	for (j = 0; j < cx; j++) {
		if (row->chars[j] == '\t')
			rx += (TAB_SIZE - 1) - (rx % TAB_SIZE);
		rx++;
	}
	return rx;
}

int editorRowRxToCx(erow* row, int rx) {
	int cur_rx = 0;
	int cx = 0;
	for (cx = 0; cx < row->size; cx++) {
		if (row->chars[cx] == '\t')
			cur_rx += (TAB_SIZE - 1) - (cur_rx % TAB_SIZE);
		cur_rx++;

		if (cur_rx > rx)
			return cx;
	}
	return cx;
}

void editorUpdateRow(erow* row) {
	int tabs = 0;
	int j = 0;
	for (j = 0; j < row->size; j++)
		if (row->chars[j] == '\t')
			tabs++;

	free(row->render);
	int mallocSize = row->size + (tabs * (TAB_SIZE - 1) + 1);
	row->render = (char*)malloc(row->size + (tabs * (TAB_SIZE - 1) + 1));
	assert(row->render != nullptr);
	memset(row->render, 0, mallocSize);

	int idx = 0;
	for (j = 0; j < row->size; j++) {
		if (row->chars[j] == '\t') {
			row->render[idx++] = ' ';
			while (idx % TAB_SIZE != 0)
				row->render[idx++] = ' ';
		} else {
			row->render[idx++] = row->chars[j];
		}
	}
	row->render[idx] = '\0';
	row->rsize = idx;

	editorUpdateSyntax(row);
}

void editorInsertRow(int at, const char* s, size_t len) {
	if (at < 0 || at > E.numrows)
		return;

	E.row = (erow*)realloc(E.row, sizeof(erow) * (E.numrows + 1));
	assert(E.row != nullptr);

	// Move existing rows down
	if (at < E.numrows) // Only shift if inserting in the middle
		memmove(&E.row[at + 1], &E.row[at], sizeof(erow) * (E.numrows - at));

	// Fix loop bounds to prevent out-of-bounds writes
	for (int j = E.numrows; j > at; j--)
		E.row[j].idx = E.row[j - 1].idx + 1;

	E.row[at].idx = at;

	E.row[at].size = (int)(len);
	E.row[at].chars = (char*)malloc(len + 1);
	assert(E.row[at].chars != nullptr);
	memcpy(E.row[at].chars, s, len);
	E.row[at].chars[len] = '\0';

	E.row[at].rsize = 0;
	E.row[at].render = nullptr;
	E.row[at].hl = nullptr;
	E.row[at].hl_open_comment = false;
	editorUpdateRow(&E.row[at]);

	E.numrows++; // Increment only after everything is initialized
	E.dirty = true;
}

void editorFreeRow(erow* row) {
	free(row->render);
	free(row->chars);
	free(row->hl);
}

void editorDelRow(int at) {
	if (at < 0 || at >= E.numrows)
		return;
	editorFreeRow(&E.row[at]);
	memmove(&E.row[at], &E.row[at + 1], sizeof(erow) * (E.numrows - at - 1));
	for (int j = at; j < E.numrows - 1; j++)
		E.row[j].idx--;
	E.numrows--;
	E.dirty = true;
}

void editorRowInsertChar(erow* row, int at, int c) {
	if (at < 0 || at > row->size)
		at = row->size;
	row->chars = (char*)realloc(row->chars, row->size + 2);
	assert(row->chars != nullptr);

	memmove(&row->chars[at + 1], &row->chars[at], row->size - at + 1);
	row->size++;
	// TODO: utf8 support
	row->chars[at] = (char)c;
	editorUpdateRow(row);
	E.dirty = true;
}

void editorRowAppendString(erow* row, char* s, size_t len) {
	row->chars = (char*)realloc(row->chars, row->size + len + 1);
	assert(row->chars != nullptr);

	memcpy(&row->chars[row->size], s, len);
	row->size += (int)(len);
	row->chars[row->size] = '\0';
	editorUpdateRow(row);
	E.dirty = true;
}

void editorRowDelChar(erow* row, int at) {
	if (at < 0 || at >= row->size)
		return;
	memmove(&row->chars[at], &row->chars[at + 1], row->size - at);
	row->size--;
	editorUpdateRow(row);
	E.dirty = true;
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
		erow* row = &E.row[E.cy];
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
	if (E.cy != 0 && E.cy == E.numrows) {
		editorMoveCursor(ARROW_LEFT);
		return;
	}
	if (E.cx == 0 && E.cy == 0) {
		if (E.numrows == 1 && E.row->size == 0) {
			editorDelRow(0);
		}
		return;
	}
	erow* row = &E.row[E.cy];
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

char* editorRowsToString(int* buflen) {
	int totlen = 0;
	int j = 0;
	for (j = 0; j < E.numrows; j++)
		totlen += E.row[j].size + 1;
	*buflen = totlen;

	char* buf = (char*)malloc(totlen);
	assert(buf != nullptr);
	char* p = buf;
	for (j = 0; j < E.numrows; j++) {
		memcpy(p, E.row[j].chars, E.row[j].size);
		p += E.row[j].size;
		*p = '\n';
		p++;
	}
	// FIXME: not new line at end of file, should I really do this?
	if (E.numrows > 0) {
		if (E.row[j].size > 0) {
			E.row[j].size--;
		}
	}
	return buf;
}

bool editorOpen(const char* filename) {
	free(E.filename);
	E.filename = STRDUP(filename);

	editorSelectSyntaxHighlight();

	std::string content;
	if (!E.myfs->getContent(filename, content)) {
		return false;
	}

	std::string line;
	size_t start = 0;
	size_t end = content.find('\n');
	while (end != std::string::npos && end < content.size()) {
		line = content.substr(start, end - start);
		if (!line.empty() && (line.back() == '\r')) {
			line.pop_back();
		}
		editorInsertRow(E.numrows, line.c_str(), line.length());
		start = end + 1;
		end = content.find('\n', start);
	}
	// Handle the last line which may not end with a newline
	if (start < content.size()) {
		line = content.substr(start, content.size() - start);
		if (!line.empty() && (line.back() == '\r')) {
			line.pop_back();
		}
		editorInsertRow(E.numrows, line.c_str(), line.length());
	}
	E.dirty = false;
	return true;
}

void editorSave() {
	if (E.filename == nullptr) {
		E.filename = editorPrompt((char*)"Save as: %s (ESC to cancel)", nullptr);
		// ensure starts with /
		if (*E.filename != '/') {
			size_t len = strlen(E.filename);
			char* newFilename = (char*)realloc(E.filename, len + 2);
			if (newFilename != nullptr) {
				memmove(newFilename + 1, newFilename, len + 1);
				newFilename[0] = '/';
				E.filename = newFilename;
			}
		}
		if (E.filename == nullptr) {
			editorSetStatusMessage("Save aborted");
			return;
		}
		editorSelectSyntaxHighlight();
	}

	std::string str;
	int len = 0;
	char* buf = editorRowsToString(&len);
	if (buf != nullptr) {
		str.assign(buf, len);
		free(buf);
	}
	if (!E.myfs->setContent(E.filename, str)) {
		if (errno == ENOENT) {
			if (!E.myfs->createFile(E.filename)) {
				editorSetStatusMessage("Can't save! I/O error ", strerror(errno));
				E.filename = nullptr;
				return;
			}
			if (!E.myfs->setContent(E.filename, str)) {
				editorSetStatusMessage("Can't save! I/O error ", strerror(errno));
				E.filename = nullptr;
				return;
			}
		} else {
			editorSetStatusMessage("Can't save! I/O error ", strerror(errno));
			E.filename = nullptr;
			return;
		}
	}
	editorSetStatusMessage("Saved to disk");
	E.dirty = false;
}

/*** find ***/

void editorFindCallback(char* query, int key) {
	int last_match = -1;
	int direction = 1;

	int saved_hl_line = 0;
	char* saved_hl = nullptr;

	if (saved_hl != nullptr) {
		memcpy(E.row[saved_hl_line].hl, saved_hl, E.row[saved_hl_line].rsize);
		free(saved_hl);
		saved_hl = nullptr;
	}

	if (key == '\r' || key == '\x1b') {
		//last_match = -1;
		//direction = 1;
		return;
	}
	if (key == ARROW_RIGHT || key == ARROW_DOWN) {
		direction = 1;
	} else if (key == ARROW_LEFT || key == ARROW_UP) {
		direction = -1;
	} else {
		last_match = -1;
		direction = 1;
	}

	if (last_match == -1)
		direction = 1;
	int current = last_match;
	int i = 0;
	for (i = 0; i < E.numrows; i++) {
		current += direction;
		if (current == -1)
			current = E.numrows - 1;
		else if (current == E.numrows)
			current = 0;

		erow* row = &E.row[current];
		char* match = strstr(row->render, query);
		if (match != nullptr) {
			// last_match = current;
			E.cy = current;
			E.cx = editorRowRxToCx(row, (int)(match - row->render));
			E.rowoff = E.numrows;

			// saved_hl_line = current;
			saved_hl = (char*)malloc(row->rsize);
			assert(saved_hl != nullptr);
			memcpy(saved_hl, row->hl, row->rsize);
			memset(&row->hl[match - row->render], HL_MATCH, strlen(query));
			break;
		}
	}
}

void editorFind() {
	int saved_cx = E.cx;
	int saved_cy = E.cy;
	int saved_coloff = E.coloff;
	int saved_rowoff = E.rowoff;

	char* query = editorPrompt((char*)"Search: %s (Use ESC/Arrows/Enter)", editorFindCallback);

	if (query != nullptr) {
		free(query);
	} else {
		E.cx = saved_cx;
		E.cy = saved_cy;
		E.coloff = saved_coloff;
		E.rowoff = saved_rowoff;
	}
}

/*** append buffer ***/

struct abuf {
	char* b;
	int len;
};

#define ABUF_INIT {nullptr, 0}

void abAppend(struct abuf* ab, const char* s, int len) {
	char* newB = (char*)realloc(ab->b, ab->len + len);
	assert(newB != nullptr);
	if (newB == nullptr)
		return;
	memcpy(&newB[ab->len], s, len);
	ab->b = newB;
	ab->len += len;
}

void abFree(struct abuf* ab) {
	free(ab->b);
}

/*** output ***/

void editorScroll() {
	E.rx = 0;
	if (E.cy < E.numrows) {
		E.rx = editorRowCxToRx(&E.row[E.cy], E.cx);
	}
	E.rowoff = std::min(E.cy, E.rowoff);

	if (E.cy >= E.rowoff + E.screenrows) {
		E.rowoff = E.cy - E.screenrows + 1;
	}
	E.coloff = std::min(E.rx, E.coloff);

	if (E.rx >= E.coloff + E.screencols) {
		E.coloff = E.rx - E.screencols + 1;
	}
}

void editorDrawRows(struct abuf* ab) {
	int y = 0;
	for (y = 0; y < E.screenrows; y++) {
		int filerow = y + E.rowoff;
		if (filerow >= E.numrows) {
			if (E.numrows == 0 && y == E.screenrows / 3) {
				char welcome[80];
				int welcomelen = snprintf(welcome, sizeof(welcome), "Kilo editor -- version %s", KILO_VERSION);
				if (welcomelen > E.screencols)
					welcomelen = E.screencols;
				int padding = (E.screencols - welcomelen) / 2;
				if (padding) {
					abAppend(ab, "~", 1);
					padding--;
				}
				while (padding--)
					abAppend(ab, " ", 1);
				abAppend(ab, welcome, welcomelen);
			} else {
				abAppend(ab, "~", 1);
			}
		} else {
			int len = E.row[filerow].rsize - E.coloff;
			if (len < 0)
				len = 0;
			if (len > E.screencols)
				len = E.screencols;
			char* c = &E.row[filerow].render[E.coloff];
			editorHighlight* hl = &E.row[filerow].hl[E.coloff];
			int current_color = -1;
			int j = 0;
			for (j = 0; j < len; j++) {
				if (iscntrl(c[j])) {
					char sym = (c[j] <= 26) ? '@' + c[j] : '?';
					abAppend(ab, "\x1b[7m", 4);
					abAppend(ab, &sym, 1);
					abAppend(ab, "\x1b[m", 3);
					if (current_color != -1) {
						char buf[16];
						int clen = snprintf(buf, sizeof(buf), "\x1b[%dm", current_color);
						abAppend(ab, buf, clen);
					}
				} else if (hl[j] == HL_NORMAL) {
					if (current_color != -1) {
						abAppend(ab, "\x1b[39m", 5);
						current_color = -1;
					}
					abAppend(ab, &c[j], 1);
				} else {
					int color = editorSyntaxToColor(hl[j]);
					if (color != current_color) {
						current_color = color;
						char buf[16];
						int clen = snprintf(buf, sizeof(buf), "\x1b[%dm", color);
						abAppend(ab, buf, clen);
					}
					abAppend(ab, &c[j], 1);
				}
			}
			abAppend(ab, "\x1b[39m", 5);
		}

		abAppend(ab, "\x1b[K", 3);
		abAppend(ab, "\r\n", 2);
	}
}

void editorDrawStatusBar(struct abuf* ab) {
	abAppend(ab, "\x1b[7m", 4);
	char status[80], rstatus[80];
	int len = snprintf(status, sizeof(status), "%.20s - %d lines %s", E.filename ? E.filename : "[No Name]", E.numrows,
					   E.dirty ? "(modified)" : "");
	int rlen =
		snprintf(rstatus, sizeof(rstatus), "%s | %d/%d", E.syntax ? E.syntax->filetype : "no ft", E.cy + 1, E.numrows);
	if (len > E.screencols)
		len = E.screencols;
	abAppend(ab, status, len);
	while (len < E.screencols) {
		if (E.screencols - len == rlen) {
			abAppend(ab, rstatus, rlen);
			break;
		}
		abAppend(ab, " ", 1);
		len++;
	}
	abAppend(ab, "\x1b[m", 3);
	abAppend(ab, "\r\n", 2);
}

void editorDrawMessageBar(struct abuf* ab) {
	abAppend(ab, "\x1b[K", 3);
	int msglen = (int)strlen(E.statusmsg);
	if (msglen > E.screencols)
		msglen = E.screencols;
	if (msglen && time(nullptr) - E.statusmsg_time < 5)
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
	snprintf(buf, sizeof(buf), "\x1b[%d;%dH", (E.cy - E.rowoff) + 1, (E.rx - E.coloff) + 1);
	abAppend(&ab, buf, (int)strlen(buf));

	abAppend(&ab, "\x1b[?25h", 6);

	write(STDOUT_FILENO, ab.b, ab.len);
	abFree(&ab);
}

void editorSetStatusMessage(const char* fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(E.statusmsg, sizeof(E.statusmsg), fmt, ap);
	va_end(ap);
	E.statusmsg_time = time(nullptr);
}

/*** input ***/
char* editorPrompt(const char* prompt, void (*callback)(char*, int)) {
	size_t bufsize = 128;
	char* buf = (char*)malloc(bufsize);
	assert(buf != nullptr);

	size_t buflen = 0;
	buf[0] = '\0';

	while (true) {
		editorSetStatusMessage(prompt, buf);
		editorRefreshScreen();

		int c = editorReadKey();
		if (c == DEL_KEY || c == CTRL_KEY('h') || c == BACKSPACE) {
			if (buflen != 0)
				buf[--buflen] = '\0';
		} else if (c == '\x1b') {
			editorSetStatusMessage("");
			if (callback)
				callback(buf, c);
			free(buf);
			return nullptr;
		} else if (c == '\r') {
			if (buflen != 0) {
				editorSetStatusMessage("");
				if (callback)
					callback(buf, c);
				return buf;
			}
		} else if (!iscntrl(c) && c < 128) {
			if (buflen == bufsize - 1) {
				bufsize *= 2;
				char* oldBuf = buf;
				buf = (char*)realloc(oldBuf, bufsize);
				if (buf == nullptr) {
					free(oldBuf);
					die("realloc failed in editorPrompt");
				}
			}
			// TODO: utf8 support
			buf[buflen++] = (char)c;
			buf[buflen] = '\0';
		}

		if (callback)
			callback(buf, c);
	}
}

void editorMoveCursor(int key) {
	erow* row = (E.cy >= E.numrows) ? nullptr : &E.row[E.cy];

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

	row = (E.cy >= E.numrows) ? nullptr : &E.row[E.cy];
	int rowlen = row ? row->size : 0;
	if (E.cx > rowlen) {
		E.cx = rowlen;
	}
}

bool editorProcessKeypress() {
	static int quit_times = KILO_QUIT_TIMES;

	int c = editorReadKey();

	switch (c) {
	case '\r':
		editorInsertNewline();
		break;

	case CTRL_KEY('q'):
		if (E.dirty && quit_times > 0) {
			editorSetStatusMessage("WARNING!!! File has unsaved changes. "
								   "Press Ctrl-Q %d more times to quit.",
								   quit_times);
			quit_times--;
			return false;
		}
		write(STDOUT_FILENO, "\x1b[2J", 4);
		write(STDOUT_FILENO, "\x1b[H", 3);
		return true;
		break;

	case CTRL_KEY('s'):
		editorSave();
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

	case BACKSPACE:
	case CTRL_KEY('h'):
	case DEL_KEY:
		if (c == DEL_KEY)
			editorMoveCursor(ARROW_RIGHT);
		editorDelChar();
		break;

	case PAGE_UP:
	case PAGE_DOWN: {
		if (c == PAGE_UP) {
			E.cy = E.rowoff;
		} else if (c == PAGE_DOWN) {
			E.cy = E.rowoff + E.screenrows - 1;
			if (E.cy > E.numrows)
				E.cy = E.numrows;
		}

		int times = E.screenrows;
		while (times--)
			editorMoveCursor(c == PAGE_UP ? ARROW_UP : ARROW_DOWN);
	} break;

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

	quit_times = KILO_QUIT_TIMES;
	return false;
}

/*** init ***/

void initEditor() {
	E.cx = 0;
	E.cy = 0;
	E.rx = 0;
	E.rowoff = 0;
	E.coloff = 0;
	E.numrows = 0;
	E.row = nullptr;
	E.dirty = false;
	E.filename = nullptr;
	E.statusmsg[0] = '\0';
	E.statusmsg_time = 0;
	E.syntax = nullptr;
	E.myfs = nullptr;

	if (getWindowSize(&E.screenrows, &E.screencols) == -1)
		die("getWindowSize");
	E.screenrows -= 2;
}

void editorStart(MyFs& myfs, const std::string& filenameIn) {
	if (enableRawMode() != 0) {
		return;
	}
	initEditor();
	E.myfs = &myfs;
	write(STDOUT_FILENO, "\x1b[2J", 4);

	if (!editorOpen(filenameIn.c_str())) {
		E.filename = nullptr;
	}
	editorSetStatusMessage("HELP: Ctrl-S = save | Ctrl-Q = quit | Ctrl-F = find");

	bool stop = false;
	while (!stop) {
		editorRefreshScreen();
		stop = editorProcessKeypress();
	}
	for (int i = 0; i < E.numrows; i++) {
		editorFreeRow(&E.row[i]);
	}
	free(E.filename);

	write(STDOUT_FILENO, "\x1b[2J", 4);
	disableRawMode();
}
