

/*** includes ***/
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#undef DELETE

#include <cassert>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>
#include "myfs.hpp"

/*** defines ***/

#define KILO_TAB_STOP 8
#define KILO_QUIT_TIMES 3

#define CTRL_KEY(k) ((k) & 0x1f)

enum editorKey {
	ESC = 27,
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
};

enum editorHighlight {
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
	char* filetype;
	char** filematch;
	char** keywords;
	char* singleline_comment_start;
	char* multiline_comment_start;
	char* multiline_comment_end;
	int flags;
};

typedef struct erow {
	int idx;
	int size;
	int rsize;
	char* chars;
	char* render;
	unsigned char* hl;
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
	erow* row;
	int dirty;
	char* filename;
	char statusmsg[80];
	time_t statusmsg_time;
	struct editorSyntax* syntax;
};

struct editorConfig E;
DWORD originalConsoleMode;
/*** filetypes ***/

char* C_HL_extensions[] = {".c", ".h", ".cpp", NULL};
char* C_HL_keywords[] = {"switch", "if",	  "while",	 "for",	   "break", "continue",	 "return",	"else",	 "struct",
						 "union",  "typedef", "static",	 "enum",   "class", "case",

						 "int|",   "long|",	  "double|", "float|", "char|", "unsigned|", "signed|", "void|", NULL};

struct editorSyntax HLDB[] = {
	{"c", C_HL_extensions, C_HL_keywords, "//", "/*", "*/", HL_HIGHLIGHT_NUMBERS | HL_HIGHLIGHT_STRINGS},
};

/*** prototypes ***/
void editorSetStatusMessage(const char* fmt, ...);
void editorSetStatusMessage(const char* fmt, ...);
void editorRefreshScreen();
char* editorPrompt(char* prompt, void (*callback)(char*, int));
void updateWindowSize();

#define HLDB_ENTRIES (sizeof(HLDB) / sizeof(HLDB[0]))

#pragma region terminal

int readKey() {
	HANDLE hStdin = GetStdHandle(STD_INPUT_HANDLE);
	DWORD numEvents = 0;
	INPUT_RECORD ir;
	DWORD eventsRead;

	while (true) {
		updateWindowSize();
		Sleep(1);

		// Check for available input events
		if (!GetNumberOfConsoleInputEvents(hStdin, &numEvents) || numEvents == 0) {
			continue; // No events available, continue waiting
		}

		// Read input events
		if (!ReadConsoleInput(hStdin, &ir, 1, &eventsRead)) {
			continue; // Error reading input, continue waiting
		}

		if (ir.EventType == KEY_EVENT) {
// Process the key event
#define TEMP_KEY_EVENT KEY_EVENT
#undef KEY_EVENT
			// Process the key event
			KEY_EVENT_RECORD keyEvent = (KEY_EVENT_RECORD)ir.Event.KeyEvent;
#define KEY_EVENT TEMP_KEY_EVENT
#undef TEMP_KEY_EVENT
			bool ctrlPressed = (keyEvent.dwControlKeyState & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED)) != 0;
			bool keyDown = keyEvent.bKeyDown;

			if (keyDown) {
				if (ctrlPressed && keyEvent.wVirtualKeyCode == 'S') {
					// Handle Ctrl+S
					editorSetStatusMessage("Ctrl+S pressed!");
					return CTRL_KEY('S');
				}

				switch (keyEvent.wVirtualKeyCode) {
				case VK_UP:
					return ARROW_UP;
				case VK_DOWN:
					return ARROW_DOWN;
				case VK_LEFT:
					return ARROW_LEFT;
				case VK_RIGHT:
					return ARROW_RIGHT;
				case VK_HOME:
					return HOME_KEY;
				case VK_END:
					return END_KEY;
				case VK_PRIOR:
					return PAGE_UP;
				case VK_NEXT:
					return PAGE_DOWN;
				case VK_DELETE:
					return DEL_KEY;
				case VK_RETURN:
					return '\n';
				case VK_ESCAPE:
					return '\x1b';
				case VK_TAB:
					return '\t';
				case VK_BACK:
					return BACKSPACE;
				default:
					if (keyEvent.uChar.AsciiChar == 0 || keyEvent.uChar.AsciiChar == -1) {
						continue; // Skip if no character code
					}
					if (ctrlPressed) {
						return CTRL_KEY(keyEvent.uChar.AsciiChar);
					}
					return keyEvent.uChar.AsciiChar;
				}
			}
		}
	}
}

void updateWindowSize() {
	CONSOLE_SCREEN_BUFFER_INFO csbi;
	GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi);
	int width = csbi.srWindow.Right - csbi.srWindow.Left + 1;
	int height = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;

	if (E.screencols != width || E.screenrows != height) {

		E.screencols = width;
		E.screenrows = height; // Reserve space for status lines
		if (E.screenrows > 3) {
			editorRefreshScreen();
		}
	}
}

int enableRawMode() {
	assert(E.screencols != 0 || E.screenrows != 0);
	HANDLE hStdin = GetStdHandle(STD_INPUT_HANDLE);
	DWORD mode;

	// Get the current console mode
	if (!GetConsoleMode(hStdin, &originalConsoleMode)) {
		return -1; // Error getting console mode
	}

	// Modify the mode to disable processed input, echo input, and line input
	mode = originalConsoleMode;
	mode &= ~(ENABLE_PROCESSED_INPUT | ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT | ENABLE_INSERT_MODE |
			  ENABLE_EXTENDED_FLAGS | ENABLE_MOUSE_INPUT);
	// mode &= ~(ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT);
	mode |= (ENABLE_VIRTUAL_TERMINAL_INPUT & ENABLE_VIRTUAL_TERMINAL_PROCESSING);
	// Set the new console mode
	if (!SetConsoleMode(hStdin, mode)) {
		return -1; // Error setting console mode
	}
	return 0;
}

void disableRawMode() {
	// Restore the original console mode
	SetConsoleMode(GetStdHandle(STD_INPUT_HANDLE), originalConsoleMode);	
}

/*** terminal ***/

/*** syntax highlighting ***/

int is_separator(int c) {
	return isspace(c) || c == '\0' || strchr(",.()+-/*=~%<>[];", c) != NULL;
}

void editorUpdateSyntax(erow* row) {
	row->hl = static_cast<unsigned char*>(realloc(row->hl, row->rsize));
	memset(row->hl, HL_NORMAL, row->rsize);

	if (E.syntax == NULL)
		return;

	char** keywords = E.syntax->keywords;

	char* scs = E.syntax->singleline_comment_start;
	char* mcs = E.syntax->multiline_comment_start;
	char* mce = E.syntax->multiline_comment_end;

	int scs_len = scs ? strlen(scs) : 0;
	int mcs_len = mcs ? strlen(mcs) : 0;
	int mce_len = mce ? strlen(mce) : 0;

	int prev_sep = 1;
	int in_string = 0;
	int in_comment = (row->idx > 0 && E.row[row->idx - 1].hl_open_comment);

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
				if (c == in_string)
					in_string = 0;
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
			if ((isdigit(c) && (prev_sep || prev_hl == HL_NUMBER)) || (c == '.' && prev_hl == HL_NUMBER)) {
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
				if (kw2)
					klen--;

				if (!strncmp(&row->render[i], keywords[j], klen) && is_separator(row->render[i + klen])) {
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
	E.syntax = NULL;
	if (E.filename == NULL)
		return;

	char* ext = strrchr(E.filename, '.');

	for (unsigned int j = 0; j < HLDB_ENTRIES; j++) {
		struct editorSyntax* s = &HLDB[j];
		unsigned int i = 0;
		while (s->filematch[i]) {
			int is_ext = (s->filematch[i][0] == '.');
			if ((is_ext && ext && !strcmp(ext, s->filematch[i])) || (!is_ext && strstr(E.filename, s->filematch[i]))) {
				E.syntax = s;

				int filerow;
				for (filerow = 0; filerow < E.numrows; filerow++) {
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
	int j;
	for (j = 0; j < cx; j++) {
		if (row->chars[j] == '\t')
			rx += (KILO_TAB_STOP - 1) - (rx % KILO_TAB_STOP);
		rx++;
	}
	return rx;
}

int editorRowRxToCx(erow* row, int rx) {
	int cur_rx = 0;
	int cx;
	for (cx = 0; cx < row->size; cx++) {
		if (row->chars[cx] == '\t')
			cur_rx += (KILO_TAB_STOP - 1) - (cur_rx % KILO_TAB_STOP);
		cur_rx++;

		if (cur_rx > rx)
			return cx;
	}
	return cx;
}

void editorUpdateRow(erow* row) {
	int tabs = 0;
	int j;
	for (j = 0; j < row->size; j++)
		if (row->chars[j] == '\t')
			tabs++;

	free(row->render);
	row->render = static_cast<char*>(malloc(row->size + tabs * (KILO_TAB_STOP - 1) + 1));

	int idx = 0;
	for (j = 0; j < row->size; j++) {
		if (row->chars[j] == '\t') {
			row->render[idx++] = ' ';
			while (idx % KILO_TAB_STOP != 0)
				row->render[idx++] = ' ';
		} else {
			row->render[idx++] = row->chars[j];
		}
	}
	row->render[idx] = '\0';
	row->rsize = idx;

	editorUpdateSyntax(row);
}

void editorInsertRow(int at, char* s, size_t len) {
	if (at < 0 || at > E.numrows)
		return;

	E.row = static_cast<erow*>(realloc(E.row, sizeof(erow) * (E.numrows + 1)));
	memmove(&E.row[at + 1], &E.row[at], sizeof(erow) * (E.numrows - at));
	for (int j = at + 1; j <= E.numrows; j++)
		E.row[j].idx++;

	E.row[at].idx = at;

	E.row[at].size = len;
	E.row[at].chars = static_cast<char*>(malloc(len + 1));
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
	E.dirty++;
}

void editorRowInsertChar(erow* row, int at, int c) {
	if (at < 0 || at > row->size)
		at = row->size;
	row->chars = static_cast<char*>(realloc(row->chars, row->size + 2));
	memmove(&row->chars[at + 1], &row->chars[at], row->size - at + 1);
	row->size++;
	row->chars[at] = c;
	editorUpdateRow(row);
	E.dirty++;
}

void editorRowAppendString(erow* row, char* s, size_t len) {
	row->chars = static_cast<char*>(realloc(row->chars, row->size + len + 1));
	memcpy(&row->chars[row->size], s, len);
	row->size += len;
	row->chars[row->size] = '\0';
	editorUpdateRow(row);
	E.dirty++;
}

void editorRowDelChar(erow* row, int at) {
	if (at < 0 || at >= row->size)
		return;
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
	if (E.cy == E.numrows)
		return;
	if (E.cx == 0 && E.cy == 0)
		return;

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
std::string editorRowsToString() {
	try {
		// Calculate the total length needed including newline characters
		size_t totlen = 0;
		// Create a string with the required length
		std::string result;
		// Append rows to the result string
		for (int i = 0; i < E.numrows;i++) {
			const auto& row = E.row[i];
			result.append(row.chars, row.size);
			result.push_back('\n'); // Add newline character
		}

		// Remove the trailing newline if needed
		if (!result.empty() && result.back() == '\n') {
			result.pop_back();
		}

		return result;
	} catch (...) {
		// Handle any unexpected exceptions
		return std::string("");
	}
}


 int editorOpen(MyFs& myfs, const std::string& filename) {
	if (filename.empty()) {
		return -1;
	}
	if (!myfs.isFileExists(MyFs::splitPath(filename).first)) {
		return 1;
	}

	E.filename = strdup(filename.data());
	std::optional<EntryInfo> entryOpt = myfs.getEntryInfo(filename);
	if (!entryOpt) {
		// do it this way an not isFileExists to get the file entry and therefore the file size
		return -1;
	}
	EntryInfo entry = *entryOpt;
	E.dirty = false;

	std::string content = myfs.getContent(entry);
	std::string line;
	size_t start = 0;
	size_t end = content.find('\n');
	while (end != std::string::npos && end < entry.size) {
		line = content.substr(start, end - start);
		if (!line.empty() && (line.back() == '\r')) {
			line.pop_back();
		}
		editorInsertRow(E.numrows, line.data(), line.length());
		start = end + 1;
		end = content.find('\n', start);
	}
	// Handle the last line which may not end with a newline
	if (start < entry.size) {
		std::string line = content.substr(start, entry.size - start);
		if (!line.empty() && (line.back() == '\r')) {
			line.pop_back();
		}
		editorInsertRow(E.numrows, line.data(), line.length());
	}

	E.dirty = false;
	return 0;
}



int editorSave(MyFs& myfs) {

	if (E.filename == nullptr) {
		std::string promptedFilename = editorPrompt("Save as: %s (ESC to cancel)", nullptr);
		if (promptedFilename.empty()) {
			editorSetStatusMessage("Save aborted");
			return 1;
		}
		std::string absoluteFilename = MyFs::addCurrentDir(promptedFilename, "/");

		try {
			if (myfs.isFileExists(absoluteFilename)) {
				editorSetStatusMessage("File already exists. Save aborted.");
				return 1;
			}
			myfs.createFile(absoluteFilename);
			E.filename = strdup(absoluteFilename.data());

		} catch (const std::exception& e) {
			editorSetStatusMessage("Can't create file! I/O error: %s", e.what());
			return 1;
		}
	}

	std::optional<EntryInfo> entryOpt = myfs.getEntryInfo(E.filename);
	EntryInfo entry;
	if (!entryOpt) {
		entry = myfs.createFile(E.filename);
	} else {
		entry = *entryOpt;
	}

	std::string content = editorRowsToString();

	try {
		myfs.setContent(entry, content);
	} catch (const std::exception& e) {
		editorSetStatusMessage("Can't save! I/O error: %s", e.what());
		return 1;
	}

	E.dirty = false;
	editorSetStatusMessage("%d bytes written on disk", content.size());
	return 0;
}

/*** find ***/

void editorFindCallback(char* query, int key) {
	static int last_match = -1;
	static int direction = 1;

	static int saved_hl_line;
	static char* saved_hl = NULL;

	if (saved_hl) {
		memcpy(E.row[saved_hl_line].hl, saved_hl, E.row[saved_hl_line].rsize);
		free(saved_hl);
		saved_hl = NULL;
	}

	if (key == '\r' || key == '\x1b') {
		last_match = -1;
		direction = 1;
		return;
	} else if (key == ARROW_RIGHT || key == ARROW_DOWN) {
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
	int i;
	for (i = 0; i < E.numrows; i++) {
		current += direction;
		if (current == -1)
			current = E.numrows - 1;
		else if (current == E.numrows)
			current = 0;

		erow* row = &E.row[current];
		char* match = strstr(row->render, query);
		if (match) {
			last_match = current;
			E.cy = current;
			E.cx = editorRowRxToCx(row, match - row->render);
			E.rowoff = E.numrows;

			saved_hl_line = current;
			saved_hl = static_cast<char*>(malloc(row->rsize));
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

	char* query = editorPrompt("Search: %s (Use ESC/Arrows/Enter)", editorFindCallback);

	if (query) {
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

#define ABUF_INIT                                                                                                      \
	{ NULL, 0 }

void abAppend(struct abuf* ab, const char* s, int len) {
	char* newLine = static_cast<char*>(realloc(ab->b, ab->len + len));

	if (newLine == NULL)
		return;
	memcpy(&newLine[ab->len], s, len);
	ab->b = newLine;
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

	if (E.cy < E.rowoff) {
		E.rowoff = E.cy;
	}
	if (E.cy >= E.rowoff + E.screenrows) {
		E.rowoff = E.cy - E.screenrows + 1;
	}
	if (E.rx < E.coloff) {
		E.coloff = E.rx;
	}
	if (E.rx >= E.coloff + E.screencols) {
		E.coloff = E.rx - E.screencols + 1;
	}
}

void editorDrawRows(struct abuf* ab) {
	int y;
	for (y = 0; y < E.screenrows - 2; y++) {
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
			unsigned char* hl = &E.row[filerow].hl[E.coloff];
			int current_color = -1;
			int j;
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
		} else {
			abAppend(ab, " ", 1);
			len++;
		}
	}
	abAppend(ab, "\x1b[m", 3);
	abAppend(ab, "\r\n", 2);
}

void editorDrawMessageBar(struct abuf* ab) {
	abAppend(ab, "\x1b[K", 3);
	int msglen = strlen(E.statusmsg);
	if (msglen > E.screencols)
		msglen = E.screencols;
	if (msglen && time(NULL) - E.statusmsg_time < 5)
		abAppend(ab, E.statusmsg, msglen);
}

void editorRefreshScreen() {
	editorScroll();

	struct abuf ab = ABUF_INIT;
	CONSOLE_SCREEN_BUFFER_INFO csbi;
	CONSOLE_CURSOR_INFO ci;

	HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
	GetConsoleScreenBufferInfo(hConsole, &csbi);
	GetConsoleCursorInfo(hConsole, &ci);

	ci.bVisible = false;
	SetConsoleCursorInfo(hConsole, &ci);


	editorDrawRows(&ab);
	editorDrawStatusBar(&ab);
	editorDrawMessageBar(&ab);

	char buf[32];
	snprintf(buf, sizeof(buf), "\x1b[%d;%dH", (E.cy - E.rowoff) + 1, (E.rx - E.coloff) + 1);
	abAppend(&ab, buf, strlen(buf));


	DWORD written = 0;
    DWORD consoleSize = csbi.dwSize.X * csbi.dwSize.Y;
	FillConsoleOutputCharacterA(hConsole, ' ', consoleSize, {0, 0}, &written);

	SetConsoleCursorPosition(hConsole, {0, 0});
	
	WriteConsoleA(hConsole, ab.b, ab.len, &written, nullptr);

	ci.bVisible = true;
	SetConsoleCursorInfo(hConsole, &ci);

	abFree(&ab);
}

void editorSetStatusMessage(const char* fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(E.statusmsg, sizeof(E.statusmsg), fmt, ap);
	va_end(ap);
	E.statusmsg_time = time(NULL);
}

/*** input ***/

char* editorPrompt(char* prompt, void (*callback)(char*, int)) {
	size_t bufsize = 128;
	char* buf = static_cast<char*>(malloc(bufsize));

	size_t buflen = 0;
	buf[0] = '\0';

	while (1) {
		editorSetStatusMessage(prompt, buf);
		editorRefreshScreen();

		int c = readKey();
		if (c == DEL_KEY || c == CTRL_KEY('h') || c == BACKSPACE) {
			if (buflen != 0)
				buf[--buflen] = '\0';
		} else if (c == '\x1b') {
			editorSetStatusMessage("");
			if (callback)
				callback(buf, c);
			free(buf);
			return NULL;
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
				buf = static_cast<char*>(realloc(buf, bufsize));
			}
			buf[buflen++] = c;
			buf[buflen] = '\0';
		}

		if (callback)
			callback(buf, c);
	}
}

void editorMoveCursor(int key) {
	erow* row = (E.cy >= E.numrows) ? NULL : &E.row[E.cy];

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

bool editorProcessKeypress(MyFs& myfs, int c) {
	static int quit_times = KILO_QUIT_TIMES;

	switch (c) {
	case '\n':
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
		//write(STDOUT_FILENO, "\x1b[2J", 4);
		//write(STDOUT_FILENO, "\x1b[H", 3);
		return true;
		break;

	case CTRL_KEY('s'):
		editorSave(myfs);
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
	E.row = NULL;
	E.dirty = 0;
	E.filename = NULL;
	E.statusmsg[0] = '\0';
	E.statusmsg_time = 0;
	E.syntax = NULL;

	updateWindowSize();
	E.screenrows -= 2;
}


void editorStart(MyFs& myfs, const std::string& filenameIn) {
	HWND consoleWindow = GetConsoleWindow();
	MSG msg{};

	system("cls");
	initEditor();

	if (enableRawMode() != 0) {
		return;
	}

	// to convert from const to non const, a.k.a making a copy
	if (editorOpen(myfs, filenameIn) == 1) {
		editorSetStatusMessage("File cannot exist, Press any key to exit");
		editorRefreshScreen();
		readKey();
		disableRawMode();
		system("cls");
		return;
	}


	editorRefreshScreen();
	while (true) {

		int key = readKey();
		if (editorProcessKeypress(myfs, key)) {
			break;
		}
		editorRefreshScreen();
	}
	disableRawMode();
	system("cls");
}