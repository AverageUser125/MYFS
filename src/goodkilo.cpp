#include "goodkilo.hpp"
#include "EntryInfo.hpp"
#include "myfs.hpp"
#include <set>
#include <stdexcept>
/* C / C++ */

/* ======================= Low level terminal handling ====================== */

static struct termios orig_termios; /* In order to restore at exit.*/
static struct editorConfig E;

void die(const char* s) {
	write(STDOUT_FILENO, "\x1b[2J", 4);
	write(STDOUT_FILENO, "\x1b[H", 3);
	throw std::runtime_error(s);
}

void disableRawMode() {
	/* Don't even check the return value as it's too late. */
	if (E.rawmode) {
		tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
		E.rawmode = false;
	}
}

/* Called at exit to avoid remaining in raw mode. */
void editorAtExit() {
	E.filename.clear();
	for (erow &row : E.rows) {
		editorFreeRow(&row);
	}
	E.rows.clear();
	write(STDOUT_FILENO, "\x1b[H", 3);	// move cursor to the top left
	write(STDOUT_FILENO, "\x1b[2J", 4); // Clear screen
	(void)signal(SIGWINCH, (__sighandler_t) nullptr);
	disableRawMode();
}

/* Raw mode: 1960 magic shit. */
int enableRawMode() {
	struct termios raw {};

	if (E.rawmode) {
		return 0; /* Already enabled. */
	}
	if (isatty(STDIN_FILENO) == 0) {
		die("isatty");
	}
	if (tcgetattr(STDIN_FILENO, &orig_termios) == -1) {
		die("tcgetattr");
	}

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
	if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) < 0) {
		die("tcgetattr");
	}
	E.rawmode = true;
	return 0;
}

/* Read a key from the terminal put in raw mode, trying to handle
 * escape sequences. */
int readKey() {
	int nread = 0;
	char c = 0;
	std::array<char, 3> seq{};
	while ((nread = read(STDIN_FILENO, &c, 1)) == 0) {
		;
	}
	if (nread == -1) {
		throw std::runtime_error("invalid key detected");
	}

	while (true) {
		if (c == ESC) {
			/* escape sequence */
			/* If this is just an ESC, we'll timeout here. */
			if (read(STDIN_FILENO, seq.data(), 1) == 0) {
				return ESC;
			}
			if (read(STDIN_FILENO, &seq[1], 1) == 0) {
				return ESC;
			}

			/* ESC [ sequences. */
			if (seq[0] == '[') {
				if (seq[1] >= '0' && seq[1] <= '9') {
					/* Extended escape, read additional byte. */
					if (read(STDIN_FILENO, &seq[2], 1) == 0)
						return ESC;
					if (seq[2] == '~') {
						switch (seq[1]) {
						case '3':
							return DEL_KEY;
						case '5':
							return PAGE_UP;
						case '6':
							return PAGE_DOWN;
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
			}

			/* ESC O sequences. */
			else if (seq[0] == 'O') {
				switch (seq[1]) {
				case 'H':
					return HOME_KEY;
				case 'F':
					return END_KEY;
				}
			}
		} else {
			return c;
		}
	}
}

/* Use the ESC [6n escape sequence to query the horizontal cursor position
 * and return it. On error -1 is returned, on success the position of the
 * cursor is stored at *rows and *cols and 0 is returned. */
int getCursorPosition(int* rows, int* cols) {
    std::array<char, MAX_KEYPRESS_LENGTH> buf{};
    int i = 0;

    if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4)
        return -1;

    while (i < static_cast<int>(buf.size()) - 1) {
        if (read(STDIN_FILENO, &buf[i], 1) != 1)
            return -1;
        if (buf[i] == 'R')
            break;
        i++;
    }

    buf[i] = '\0';

    if (buf[0] != '\x1b' || buf[1] != '[')
        return -1;

    char* end = nullptr;
    *rows = std::strtol(&buf[2], &end, NUMBER_BASE);
    if (*end != ';')
        return -1;
    *cols = std::strtol(end + 1, &end, NUMBER_BASE);
    if (*end != 'R')
        return -1;

    return 0;
}

/* Try to get the number of columns in the current terminal. If the ioctl()
 * call fails the function will try to query the terminal itself.
 * Returns 0 on success, -1 on error. */
int getWindowSize(int* rows, int* cols) {
	struct winsize ws {};

	if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
		// Fallback method uses moving the cursor to the bottom right corner
		if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12)
			return -1;
		return getCursorPosition(rows, cols);
	}
	*cols = ws.ws_col;
	*rows = ws.ws_row;
	return 0;
}

/* ======================= Editor rows implementation ======================= */

/* Update the rendered version and the syntax highlight of a row. */
void editorUpdateRow(erow* row) {
	unsigned int tabs = 0;
	unsigned int nonprint = 0;
	int j = 0;
	int idx = 0;

	/* Create a version of the row we can directly print on the screen,
     * respecting tabs, substituting non printable characters with '?'. */
	free(row->render);
	for (j = 0; j < row->size; j++)
		if (row->chars[j] == TAB)
			tabs++;

	unsigned long long allocsize = (unsigned long long)row->size + static_cast<unsigned long long>(tabs) * 8 +
								   static_cast<unsigned long long>(nonprint) * 9 + 1;
	if (allocsize > UINT32_MAX) {
		throw std::runtime_error("Some line of the edited file is too long for kilo\n");
	}

	row->render = static_cast<char*>(malloc(row->size + tabs * 8 + nonprint * 9 + 1));
	idx = 0;
	for (j = 0; j < row->size; j++) {
		if (row->chars[j] == TAB) {
			row->render[idx++] = ' ';
			while ((idx + 1) % 8 != 0)
				row->render[idx++] = ' ';
		} else {
			row->render[idx++] = row->chars[j];
		}
	}
	row->rsize = idx;
	row->render[idx] = '\0';

	/* Update the syntax highlighting attributes of the row. */
	row->hl = static_cast<unsigned char*>(realloc(row->hl, row->rsize));
	memset(row->hl, HL_NORMAL, row->rsize);
}

/* Insert a row at the specified position, shifting the other rows on the bottom
 * if required. */
void editorInsertRow(int at, const char* s, int len) {
	if (at > E.rows.size())
		return;
	// Increase the size of the vector by adding a new row.
	E.rows.push_back(erow{});

	// Move the rows starting from the position `at` one place to the right.
	if (at != E.rows.size() - 1) {
		std::move(E.rows.begin() + at, E.rows.end() - 1, E.rows.begin() + at + 1);
		for (int j = at + 1; j < E.rows.size(); j++)
			E.rows[j].idx++;
	}
	// Initialize the new row.
	E.rows[at].size = len;
	E.rows[at].chars = static_cast<char*>(malloc(len + 1));
	memcpy(E.rows[at].chars, s, len + 1);
	E.rows[at].hl = nullptr;
	E.rows[at].hl_oc = 0;
	E.rows[at].render = nullptr;
	E.rows[at].rsize = 0;
	E.rows[at].idx = at;

	// Update the row and set the dirty flag.
	editorUpdateRow(&E.rows[at]);
	E.dirty = true;
}


/* Free row's heap allocated stuff. */
void editorFreeRow(erow* row) {
	free(row->render);
	free(row->chars);
	free(row->hl);
}

/* Remove the row at the specified position, shifting the remainign on the
 * top. */
void editorDelRow(int at) {
	if (at >= E.rows.size())
		return;

	// Free the memory for the row at the specified index.
	editorFreeRow(&E.rows[at]);

	// Remove the row at the specified index.
	E.rows.erase(E.rows.begin() + at);
	// Update indices for rows that follow.
	for (int j = at; j < E.rows.size(); j++)
		E.rows[j].idx--;
	// Mark the editor as dirty.
	E.dirty = true;
}

/* Turn the editor rows into a single heap-allocated string.
 * Returns the pointer to the heap-allocated string and populate the
 * integer pointed by 'buflen' with the size of the string, escluding
 * the final nulterm. */
char* editorRowsToString(int* buflen) {
	char* buf = nullptr;
	char* p = nullptr;
	int totlen = 0;
	int j = 0;

	/* Compute count of bytes */
	for (j = 0; j < E.rows.size(); j++)
		totlen += E.rows[j].size + 1; /* +1 is for "\n" at end of every row */
	*buflen = totlen;
	totlen++; /* Also make space for nulterm */

	p = buf = static_cast<char*>(malloc(totlen));
	for (j = 0; j < E.rows.size(); j++) {
		memcpy(p, E.rows[j].chars, E.rows[j].size);
		p += E.rows[j].size;
		*p = '\n';
		p++;
	}
	*p = '\0';
	return buf;
}

/* Insert a character at the specified position in a row, moving the remaining
 * chars on the right if needed. */
void editorRowInsertChar(erow* row, int at, int c) {
	if (at > row->size) {
		/* Pad the string with spaces if the insert location is outside the
         * current length by more than a single character. */
		int padlen = at - row->size;
		/* In the next line +2 means: new char and null term. */
		row->chars = static_cast<char*>(realloc(row->chars, row->size + padlen + 2));
		memset(row->chars + row->size, ' ', padlen);
		row->chars[row->size + padlen + 1] = '\0';
		row->size += padlen + 1;
	} else {
		/* If we are in the middle of the string just make space for 1 new
         * char plus the (already existing) null term. */
		row->chars = static_cast<char*>(realloc(row->chars, row->size + 2));
		memmove(row->chars + at + 1, row->chars + at, row->size - at + 1);
		row->size++;
	}
	row->chars[at] = c;
	editorUpdateRow(row);
	E.dirty = true;
}

/* Append the string 's' at the end of a row */
void editorRowAppendString(erow* row, char* s, int len) {
	row->chars = static_cast<char*>(realloc(row->chars, row->size + len + 1));
	memcpy(row->chars + row->size, s, len);
	row->size += len;
	row->chars[row->size] = '\0';
	editorUpdateRow(row);
	E.dirty = true;
}

/* Delete the character at offset 'at' from the specified row. */
void editorRowDelChar(erow* row, int at) {
	if (row->size <= at)
		return;
	memmove(row->chars + at, row->chars + at + 1, row->size - at);
	editorUpdateRow(row);
	row->size--;
	E.dirty = true;
}

/* Insert the specified char at the current prompt position. */
void editorInsertChar(int c) {
	int filerow = E.rowoff + E.cy;
	int filecol = E.coloff + E.cx;
	erow* row = (filerow >= E.rows.size()) ? nullptr : &E.rows[filerow];

	/* If the row where the cursor is currently located does not exist in our
     * logical representaion of the file, add enough empty rows as needed. */
	if (row == nullptr) {
		while (E.rows.size() <= filerow)
			editorInsertRow(E.rows.size(), "", 0);
	}
	row = &E.rows[filerow];
	editorRowInsertChar(row, filecol, c);
	if (E.cx == E.screencols - 1)
		E.coloff++;
	else
		E.cx++;
	E.dirty = true;
}

/* Inserting a newline is slightly complex as we have to handle inserting a
 * newline in the middle of a line, splitting the line as needed. */
inline void fixCursor() {
	if (E.cy == E.screenrows - 3 || E.cy == E.screenrows) {
		E.rowoff++;
	} else {
		E.cy++;
	}
	E.cx = 0;
	E.coloff = 0;
}

void editorInsertNewline() {
	int filerow = E.rowoff + E.cy;
	int filecol = E.coloff + E.cx;
	erow* rows = (filerow >= E.rows.size()) ? nullptr : &E.rows[filerow];

	if (rows == nullptr) {
		if (filerow == E.rows.size()) {
			editorInsertRow(filerow, "", 0);
			fixCursor();
		}
		return;
	}

	// If the cursor is over the current line size, adjust it
	if (filecol >= rows->size) {
		filecol = rows->size;
	}

	if (filecol == 0) {
		editorInsertRow(filerow, "", 0);
	} else {
		// Split the line between two rows
		editorInsertRow(filerow + 1, rows->chars + filecol, rows->size - filecol);
		rows = &E.rows[filerow];
		rows->chars[filecol] = '\0';
		rows->size = filecol;
		editorUpdateRow(rows);
	}

	fixCursor();
}

/* Delete the char at the current prompt position. */
void editorDelChar() {
	int filerow = E.rowoff + E.cy;
	int filecol = E.coloff + E.cx;
	erow* row = (filerow >= E.rows.size()) ? nullptr : &E.rows[filerow];

	if (row == nullptr) {
		return;
	}
	if (filecol == 0 && filerow == 0) {
		if (E.rows.size() == 1 && row->size == 0) {
			editorDelRow(filerow);
			E.rowoff = 0;
			E.cy = 0;
			E.cx = 0;
			E.coloff = 0;
		}
		return;
	}

	if (filecol == 0) {
		/* Handle the case of column 0, we need to move the current line
         * on the right of the previous one. */
		filecol = E.rows[filerow - 1].size;
		editorRowAppendString(&E.rows[filerow - 1], row->chars, row->size);
		editorDelRow(filerow);
		row = nullptr;
		if (E.cy == 0)
			E.rowoff--;
		else
			E.cy--;
		E.cx = filecol;
		if (E.cx >= E.screencols) {
			int shift = (E.screencols - E.cx) + 1;
			E.cx -= shift;
			E.coloff += shift;
		}
	} else {
		editorRowDelChar(row, filecol - 1);
		if (E.cx == 0 && (E.coloff != 0))
			E.coloff--;
		else
			E.cx--;
	}
	if (row != nullptr)
		editorUpdateRow(row);
	E.dirty = true;
}

/* Load the specified program in the editor memory and returns 0 on success
 * or 1 on error. */
 int editorOpen(MyFs& myfs, const std::string& filename) {
	if (filename.empty()) {
		return -1;
	}
	if (!myfs.isFileExists(MyFs::splitPath(filename).first)) {
		return 1;
	}
	std::optional<EntryInfo> entryOpt = myfs.getEntryInfo(filename);
	if (!entryOpt) { 
		// do it this way an not isFileExists to get the file entry and therefore the file size
		return -1;
	}
	EntryInfo entry = *entryOpt;
	E.dirty = false;
	E.filename = filename;

	std::string content = myfs.getContent(entry);

	std::string line;
	size_t start = 0;
	size_t end = content.find('\n');
	while (end != std::string::npos && end < entry.size) {
		line = content.substr(start, end - start);
		if (!line.empty() && (line.back() == '\r')) {
			line.pop_back();
		}
		editorInsertRow(E.rows.size(), line.c_str(), line.length());
		start = end + 1;
		end = content.find('\n', start);
	}
    // Handle the last line which may not end with a newline
    if (start < entry.size) {
        std::string line = content.substr(start, entry.size - start);
        if (!line.empty() && (line.back() == '\r')) {
            line.pop_back();
        }
        editorInsertRow(E.rows.size(), line.c_str(), line.length());
    }

	E.dirty = false;
	return 0;
}

std::string editorPrompt(const char* prompt) {
	std::string buf;
	while (true) {
		editorSetStatusMessage(prompt, buf.c_str());
		editorRefreshScreen();
		int c = readKey();

		if (c == DEL_KEY || c == CTRL_KEY('h') || c == BACKSPACE) {
			if (!buf.empty()) {
				buf.pop_back();
			}
		} else if (c == ESC || c == CTRL_KEY('q')) { // Escape key
			editorSetStatusMessage("");
			return "";			// Returning empty string to indicate cancellation
		} else if (c == '\r' || c == '\n'|| c == CTRL_KEY('s')) { // Enter key
			if (!buf.empty()) {
				editorSetStatusMessage("");
				return buf;
			}
		} else if (c < 128 && std ::iscntrl(c) == 0) {
			buf.push_back(static_cast<char>(c));
		}
	}
}
/* Save the current file on disk. Return 0 on success, 1 on error. */
int editorSave(MyFs& myfs) {

	if (E.filename.empty()) {
		std::string promptedFilename = editorPrompt("Save as: %s (ESC to cancel)");
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
			E.filename = absoluteFilename;

		} catch (const std::exception& e) {
			editorSetStatusMessage("Can't create file! I/O error: %s", e.what());
			return 1;
		}
	}

	try {
		myfs.createFile(E.filename);
	} catch (std::runtime_error& e) {
		// catch error like FILE_EXISTS.
	}

	int len = 0;
	char* buf = editorRowsToString(&len);
	if (buf == nullptr) {
		editorSetStatusMessage("Can't save! Memory error.");
		return 1;
	}

	std::string content(buf, len);
	free(buf);

	try {
		myfs.setContent(E.filename, content);
	} catch (const std::exception& e) {
		editorSetStatusMessage("Can't save! I/O error: %s", e.what());
		return 1;
	}

	E.dirty = false;
	editorSetStatusMessage("%d bytes written on disk", len);
	return 0;
}

/* ============================= Terminal update ============================ */

/* We define a very simple "append buffer" structure, that is an heap
 * allocated string where we can append to. This is useful in order to
 * write all the escape sequences in a buffer and flush them to the standard
 * output in a single call, to avoid flickering effects. */


static void abAppend(std::vector<char>& ab, const char* s, int len) {
	ab.insert(ab.end(), s, s + len);
}

void welcomeMessage(std::vector<char> ab) {
	std::array<char, MAX_STATUS_LENGTH> welcome{};
	int welcomelen = snprintf(welcome.data(), welcome.size(), WELCOME_MESSAGE);
	int padding = (E.screencols - welcomelen) / 2;
	if (padding != 0) {
		abAppend(ab, "~", 1);
		padding--;
	}
	while ((padding--) != 0)
		abAppend(ab, " ", 1);
	abAppend(ab, welcome.data(), welcomelen);
}

/* This function writes the whole screen using VT100 escape characters
 * starting from the logical state of the editor in the global state 'E'. */
void editorRefreshScreen() {
	int y = 0;
	erow* r = nullptr;
	std::array<char, 32> buf{};
	std::vector<char> ab{};

	abAppend(ab, "\x1b[?25l", 6); /* Hide cursor. */
	abAppend(ab, "\x1b[H", 3);	   /* Go home. */
	for (y = 0; y < E.screenrows; y++) {
		int filerow = E.rowoff + y;

		if (filerow >= E.rows.size()) {
			if (E.rows.empty() && y == E.screenrows / 3) {
				welcomeMessage(ab);
			} else {
				abAppend(ab, "~\x1b[0K\r\n", 7);
			}
			continue;
		}

		r = &E.rows[filerow];

		int len = r->rsize - E.coloff;
		if (len > 0) {
			if (len > E.screencols)
				len = E.screencols;
			char* c = r->render + E.coloff;
			unsigned char* hl = r->hl + E.coloff;
			int j = 0;
			for (j = 0; j < len; j++) {
				if (hl[j] == HL_NONPRINT) {
					char sym = 0;
					if (c[j] <= 26)
						sym = '@' + c[j];
					else
						sym = '?';
					abAppend(ab, &sym, 1);
				} else {
					abAppend(ab, c + j, 1);
				}
			}
		}
		abAppend(ab, "\x1b[39m", 5);
		abAppend(ab, "\x1b[0K", 4);
		abAppend(ab, "\r\n", 2);
	}

	/* Create a two rows status. First row: */
	abAppend(ab, "\x1b[0K", 4);
	abAppend(ab, "\x1b[7m", 4);
	std::array<char, MAX_STATUS_LENGTH> status{};
	std::array<char, MAX_STATUS_LENGTH> rstatus{};
	int len = snprintf(status.data(), status.size(), "%.20s - %zu lines %s",
					   E.filename.empty() ? "[No Name]" : E.filename.c_str(), E.rows.size(), E.dirty ? "(modified)" : "");
	int rlen = snprintf(rstatus.data(), rstatus.size(), "%d/%zu", E.rowoff + E.cy + 1, E.rows.size());
	if (len > E.screencols)
		len = E.screencols;
	abAppend(ab, status.data(), len);
	while (len < E.screencols) {
		if (E.screencols - len == rlen) {
			abAppend(ab, rstatus.data(), rlen);
			break;
		}
		abAppend(ab, " ", 1);
		len++;
	}
	abAppend(ab, "\x1b[0m\r\n", 6);

	/* Second row depends on E.statusmsg and the status message update time. */
	abAppend(ab, "\x1b[0K", 4);
	int msglen = strlen(E.statusmsg.data());
	if ((msglen != 0) && time(nullptr) - E.statusmsg_time < 5)
		abAppend(ab, E.statusmsg.data(), msglen <= E.screencols ? msglen : E.screencols);

	/* Put cursor at its current position. Note that the horizontal position
     * at which the cursor is displayed may be different compared to 'E.cx'
     * because of TABs. */
	int j = 0;
	int cx = 1;
	int filerow = E.rowoff + E.cy;
	erow* row = (filerow >= E.rows.size()) ? nullptr : &E.rows[filerow];
	if (row != nullptr) {
		for (j = E.coloff; j < (E.cx + E.coloff); j++) {
			if (j < row->size && row->chars[j] == TAB)
				cx += (TAB_SIZE - 1) - ((cx) % TAB_SIZE);
			cx++;
		}
	}
	snprintf(buf.data(), sizeof(buf), "\x1b[%d;%dH", E.cy + 1, cx);
	abAppend(ab, buf.data(), strlen(buf.data()));
	abAppend(ab, "\x1b[?25h", 6); /* Show cursor. */
	write(STDOUT_FILENO, ab.data(), ab.size());

	// the vector is automatically cleared
}

/* Set an editor status message for the second line of the status, at the
 * end of the screen. */
void editorSetStatusMessage(const char* fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	int len = vsnprintf(E.statusmsg.data(), sizeof(E.statusmsg), fmt, ap);
	E.statusmsg[len] = 0;
	va_end(ap);
	E.statusmsg_time = time(nullptr);
}

/* =============================== Find mode ================================ */


void editorFind() {
	std::array<char, KILO_QUERY_LEN + 1> query = {0};
	int qlen = 0;
	int last_match = -1;	/* Last line where a match was found. -1 for none. */
	int find_next = 0;		/* if 1 search next, if -1 search prev. */
	int saved_hl_line = -1; /* No saved HL */
	char* saved_hl = nullptr;

#define FIND_RESTORE_HL                                                                                                \
	do {                                                                                                               \
		if (saved_hl) {                                                                                                \
			memcpy(E.rows[saved_hl_line].hl, saved_hl, E.rows[saved_hl_line].rsize);                                     \
			free(saved_hl);                                                                                            \
			saved_hl = NULL;                                                                                           \
		}                                                                                                              \
	} while (0)

	/* Save the cursor position in order to restore it later. */
	int saved_cx = E.cx;
	int saved_cy = E.cy;
	int saved_coloff = E.coloff;
	int saved_rowoff = E.rowoff;

	while (true) {
		editorSetStatusMessage("Search: %s (Use ESC/Arrows/Enter)", query.data());
		editorRefreshScreen();

		int c = readKey();
		if (c == DEL_KEY || c == CTRL_KEY('h') || c == BACKSPACE) {
			if (qlen != 0)
				query[--qlen] = '\0';
			last_match = -1;
		} else if (c == ESC || c == ENTER) {
			if (c == ESC) {
				E.cx = saved_cx;
				E.cy = saved_cy;
				E.coloff = saved_coloff;
				E.rowoff = saved_rowoff;
			}
			FIND_RESTORE_HL;
			editorSetStatusMessage("");
			return;
		} else if (c == ARROW_RIGHT || c == ARROW_DOWN) {
			find_next = 1;
		} else if (c == ARROW_LEFT || c == ARROW_UP) {
			find_next = -1;
		} else if (isprint(c) != 0) {
			if (qlen < KILO_QUERY_LEN) {
				query[qlen++] = c;
				query[qlen] = '\0';
				last_match = -1;
			}
		}

		/* Search occurrence. */
		if (last_match == -1)
			find_next = 1;
		if (find_next != 0) {
			char* match = nullptr;
			int match_offset = 0;
			int i = 0;
			int current = last_match;

			for (i = 0; i < E.rows.size(); i++) {
				current += find_next;
				if (current == -1)
					current = E.rows.size() - 1;
				else if (current == E.rows.size())
					current = 0;
				match = strstr(E.rows[current].render, query.data());
				if (match != nullptr) {
					match_offset = match - E.rows[current].render;
					break;
				}
			}
			find_next = 0;

			/* Highlight */
			FIND_RESTORE_HL;

			if (match != nullptr) {
				erow* row = &E.rows[current];
				last_match = current;
				if (row->hl != nullptr) {
					saved_hl_line = current;
					saved_hl = static_cast<char*>(malloc(row->rsize));
					memcpy(saved_hl, row->hl, row->rsize);
					memset(row->hl + match_offset, HL_MATCH, qlen);
				}
				E.cy = 0;
				E.cx = match_offset;
				E.rowoff = current;
				E.coloff = 0;
				/* Scroll horizontally as needed. */
				if (E.cx > E.screencols) {
					int diff = E.cx - E.screencols;
					E.cx -= diff;
					E.coloff += diff;
				}
			}
		}
	}
}

/* ========================= Editor events handling  ======================== */

/* Handle cursor position change because arrow keys were pressed. */
void editorMoveCursor(int key) {
	// Static variable to remember the original column position
	static int originalColumn = 0;

	int filerow = E.rowoff + E.cy;
	int filecol = E.coloff + E.cx;
	int rowlen = 0;
	erow* row = (filerow >= E.rows.size()) ? nullptr : &E.rows[filerow];

	switch (key) {
	case ARROW_LEFT:
		if (E.cx != 0) {
			E.cx--;
			originalColumn = E.cx;
			break;
		}
		if (E.coloff != 0) {
			E.coloff--;
		} else {
			if (filerow > 0) {
				E.cy--;
				E.cx = E.rows[filerow - 1].size;
				if (E.cx > E.screencols - 1) {
					E.coloff = E.cx - E.screencols + 1;
					E.cx = E.screencols - 1;
				}
			}
		}
		originalColumn = E.cx;
		break;
	case ARROW_RIGHT:
		if ((row != nullptr) && filecol < row->size) {
			if (E.cx == E.screencols - 1) {
				E.coloff++;
			} else {
				E.cx += 1;
			}
		} else if ((row != nullptr) && filecol == row->size) {
			E.cx = 0;
			E.coloff = 0;
			if (E.cy == E.screenrows - 1) {
				E.rowoff++;
			} else {
				E.cy += 1;
			}
		}
		originalColumn = E.cx;
		break;
	case ARROW_UP:
		// Save the current column position before moving
		originalColumn = originalColumn < E.cx ? E.cx : originalColumn;

		if (E.cy == 0) {
			if (E.rowoff != 0)
				E.rowoff--;
		} else {
			E.cy -= 1;
		}

		// Move to the original column or the end of the line if the line is
		// shorter
		row = (E.rowoff + E.cy >= E.rows.size()) ? nullptr : &E.rows[E.rowoff + E.cy];
		rowlen = row != nullptr ? row->size : 0;
		E.cx = (originalColumn < rowlen) ? originalColumn : rowlen;
		E.coloff = (E.cx > E.screencols - 1) ? E.cx - E.screencols + 1 : 0;
		break;
	case ARROW_DOWN:
		// Save the current column position before moving
		originalColumn = originalColumn < E.cx ? E.cx : originalColumn;

		if (filerow < E.rows.size()) {
			if (E.cy == E.screenrows - 1) {
				E.rowoff++;
			} else {
				E.cy += 1;
			}
		}
		// Move to the original column or the end of the line if the line is
		// shorter
		row = (E.rowoff + E.cy >= E.rows.size()) ? nullptr : &E.rows[E.rowoff + E.cy];
		rowlen = row != nullptr ? row->size : 0;
		E.cx = (originalColumn < rowlen) ? originalColumn : rowlen;
		E.coloff = (E.cx > E.screencols - 1) ? E.cx - E.screencols + 1 : 0;

		break;
	}
	/* Fix cx if the current line has not enough chars. */
	filerow = E.rowoff + E.cy;
	filecol = E.coloff + E.cx;
	row = (filerow >= E.rows.size()) ? nullptr : &E.rows[filerow];
	rowlen = row != nullptr ? row->size : 0;
	if (filecol > rowlen) {
		E.cx -= filecol - rowlen;
		if (E.cx < 0) {
			E.coloff += E.cx;
			E.cx = 0;
		}
	}
}

/* Process events arriving from the standard input, which is, the user
 * is typing stuff on the terminal. */

bool editorProcessKeypress(MyFs& myfs) {
	/* When the file is modified, requires Ctrl-q to be pressed N times
     * before actually quitting. */
	static int quit_times = KILO_QUIT_TIMES;

	int c = readKey();
	switch (c) {
	case ENTER: /* Enter */
		editorInsertNewline();
		break;
	case CTRL_KEY('c'): /* Ctrl-c */
		/* We ignore ctrl-c, it can't be so simple to lose the changes
         * to the edited file. */
		break;
	case CTRL_KEY('q'): /* Ctrl-q */
		/* Quit if the file was already saved. */
		if (E.dirty && (quit_times != 0)) {
			editorSetStatusMessage("WARNING!!! File has unsaved changes. "
								   "Press Ctrl-Q %d more times to quit.",
								   quit_times);
			quit_times--;
			return true;
		}
		return false;
		break;
	case CTRL_KEY('s'): /* Ctrl-s */
		editorSave(myfs);
		break;
	case CTRL_KEY('f'):
		editorFind();
		break;
	case BACKSPACE:		/* Backspace */
	case CTRL_KEY('h'): /* Ctrl-h */
	case DEL_KEY:
		editorDelChar();
		break;
	case PAGE_UP:
	case PAGE_DOWN:
		if (c == PAGE_UP && E.cy != 0)
			E.cy = 0;
		else if (c == PAGE_DOWN && E.cy != E.screenrows - 1)
			E.cy = E.screenrows - 1;
		{
			int times = E.screenrows;
			while ((times--) != 0)
				editorMoveCursor(c == PAGE_UP ? ARROW_UP : ARROW_DOWN);
		}
		break;

	case ARROW_UP:
	case ARROW_DOWN:
	case ARROW_LEFT:
	case ARROW_RIGHT:
		editorMoveCursor(c);
		break;
	case CTRL_KEY('l'): /* ctrl+l, clear screen */
						/* Just refresht the line as side effect. */
	case ESC:
		/* Nothing to do for ESC in this mode. */
		break;
	default:
		if (iscntrl(c) == 0)
			editorInsertChar(c);
		break;
	}

	quit_times = KILO_QUIT_TIMES; /* Reset it to the original value. */

	return true;
}

void updateWindowSize() {
	if (getWindowSize(&E.screenrows, &E.screencols) == -1)
		die("getWindowSize");
	E.screenrows -= 2; /* Get room for status bar. */
}

void handleSigWinCh(int unused __attribute__((unused))) {
	updateWindowSize();
	if (E.cy > E.screenrows)
		E.cy = E.screenrows - 1;
	if (E.cx > E.screencols)
		E.cx = E.screencols - 1;
	editorRefreshScreen();
}

void initEditor() {
	E.cx = 0;
	E.cy = 0;
	E.rowoff = 0;
	E.coloff = 0;
	E.rows.clear();
	E.dirty = false;
	E.filename = "";
	E.syntax = nullptr;
	updateWindowSize();
	signal(SIGWINCH, handleSigWinCh);
}

void editorStart(MyFs& myfs, const std::string& filenameIn) {

	initEditor();
	enableRawMode();

	if (editorOpen(myfs, filenameIn) == 1) {
		editorSetStatusMessage("File cannot exist, Press any key to exit");
		editorRefreshScreen();
		system("pause");
		disableRawMode();
		system("cls");
		return;
	}

	editorSetStatusMessage("HELP: Ctrl-S = save | Ctrl-Q = quit | Ctrl-F = find");
	
	bool toContinue = true;
	while (toContinue) {
		editorRefreshScreen();
		toContinue = editorProcessKeypress(myfs);
	}
	editorAtExit();
}