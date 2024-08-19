#include "editor.hpp"
static struct editorConfig E;
static DWORD originalConsoleMode;

#pragma region terminal

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

	if (E.rawmode) {
		return 0; // Already enabled
	}

	// Get the current console mode
	if (!GetConsoleMode(hStdin, &originalConsoleMode)) {
		return -1; // Error getting console mode
	}

	// Modify the mode to disable processed input, echo input, and line input
	mode = originalConsoleMode;
	mode &= ~(ENABLE_PROCESSED_INPUT | ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT | ENABLE_INSERT_MODE | ENABLE_EXTENDED_FLAGS);

	// Set the new console mode
	if (!SetConsoleMode(hStdin, mode)) {
		return -1; // Error setting console mode
	}

	E.rawmode = true;
	return 0;
}

void disableRawMode() {
	if (E.rawmode) {
		// Restore the original console mode
		SetConsoleMode(GetStdHandle(STD_INPUT_HANDLE), originalConsoleMode);
		E.rawmode = false;
	}
}

#pragma endregion

#pragma region findMode

void editorFind() {
	std::array<char, 80 + 1> query = {0};
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
			if (qlen < query.size()) {
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
					saved_hl = (char*)malloc(row->rsize);
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

#pragma endregion

#pragma region keyPresses

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
					return ENTER;
				case VK_ESCAPE:
					return ESC;
				case VK_TAB:
					return TAB;
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


bool editorProcessKeypress(MyFs& myfs, int c) {
	/* When the file is modified, requires Ctrl-q to be pressed N times
     * before actually quitting. */
	static int quit_times = KILO_QUIT_TIMES;

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
			return false;
		}
		return true;
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
		if (c < 128 && iscntrl(c) == 0)
			editorInsertChar(c);
		break;
	}

	quit_times = KILO_QUIT_TIMES; /* Reset it to the original value. */

	return false;
}

#pragma endregion

#pragma region bufferStuff

static void abAppend(std::vector<char>& ab, const char* s, int len) {
	ab.insert(ab.end(), s, s + len);
}

static void welcomeMessage(std::vector<char>& ab) {
	std::array<char, MAX_STATUS_LENGTH> welcome{};
	int welcomelen = snprintf(welcome.data(), welcome.size(), "%s", WELCOME_MESSAGE);

	int padding = (E.screencols - welcomelen) / 2;
	if (padding > 0) {
		abAppend(ab, "~", 1);
		padding--;
	}
	while (padding-- > 0) {
		abAppend(ab, " ", 1);
	}
	abAppend(ab, welcome.data(), welcomelen);
}

void editorSetStatusMessage(const char* fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	int len = vsnprintf(E.statusmsg.data(), sizeof(E.statusmsg), fmt, ap);
	E.statusmsg[len] = 0;
	va_end(ap);
	E.statusmsg_time = time(nullptr);
}

void editorRefreshScreen() {
	HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
	CONSOLE_SCREEN_BUFFER_INFO csbi;
	GetConsoleScreenBufferInfo(hConsole, &csbi);

	// Buffer for screen content
	std::vector<char> ab(E.screencols * E.screenrows + 2, ' ');

	// Fill buffer with content
	for (size_t i = 0; i < E.screenrows - 2; ++i) {
		size_t rowIndex = E.rowoff + i;
		size_t startIdx = i * E.screencols;

		if (rowIndex < static_cast<int>(E.rows.size())) {
			const erow& row = E.rows[rowIndex];
			if (row.rsize > 0 && row.render != nullptr) {
				// Print row content
				int copyLen = std::min(row.rsize, E.screencols);
				std::copy(row.render, row.render + copyLen, ab.begin() + startIdx);
				// Fill the rest of the line with spaces
				if (row.rsize < E.screencols) {
					std::fill(ab.begin() + startIdx + row.rsize, ab.begin() + startIdx + E.screencols, ' ');
				}
			} else if (row.rsize == 0 && row.render == nullptr) {
				// If the row is empty (new line character), don't put a '~'
				std::fill(ab.begin() + startIdx, ab.begin() + startIdx + E.screencols, ' ');
			}
		} else {
			// Print '~' for lines beyond the content
			ab[startIdx] = '~';
			std::fill(ab.begin() + startIdx + 1, ab.begin() + startIdx + E.screencols, ' ');
		}
	}
	// Create the status line (first row)
	std::array<char, MAX_STATUS_LENGTH> status{};
	std::array<char, MAX_STATUS_LENGTH> rstatus{};
	int len =
		snprintf(status.data(), status.size(), "%.20s - %llu lines %s",
					   E.filename.empty() ? "[No Name]" : E.filename.c_str(), E.rows.size(), E.dirty ? "(modified)" : "");

	int rlen = snprintf(rstatus.data(), rstatus.size(), "%zd/%llu", E.rowoff + E.cy + 1, E.rows.size());

	// Write the status line into the buffer
	len = std::min(static_cast<size_t>(len), E.screencols);
	rlen = std::min(static_cast<size_t>(rlen), E.screencols);

	int statusIndex = (E.screenrows - 2) * E.screencols;

	std::copy(status.data(), status.data() + len, ab.begin() + statusIndex);
	if (rlen > 0) {
		std::copy(rstatus.data(), rstatus.data() + rlen, ab.begin() + statusIndex + E.screencols - rlen);
	}
	if (len < E.screencols) {
		std::fill(ab.begin() + statusIndex + len, ab.begin() + statusIndex + E.screencols - rlen, ' ');
	}

	// Second status line (message update)
	size_t msglen = strlen(E.statusmsg.data());
	if ((msglen != 0) && (time(nullptr) - E.statusmsg_time < 5)) {
		msglen = std::min(msglen, E.screencols);
		int msgIndex = (E.screenrows - 1) * E.screencols;
		std::copy(E.statusmsg.data(), E.statusmsg.data() + msglen, ab.begin() + msgIndex);
	}

	// Fill the rest of the second status line with spaces
	std::fill(ab.begin() + static_cast<size_t>(E.screenrows - 1) * E.screencols + msglen,
			  ab.begin() + static_cast<size_t> (E.screenrows) * E.screencols, ' ');


	// Write the buffer to console
	DWORD written;
	COORD coordScreen = {0, 0};
	WriteConsoleOutputCharacterA(hConsole, ab.data(), ab.size(), coordScreen, &written);

	// Set cursor position (cx, cy) if needed
	COORD cursorPos = {(SHORT)E.cx, (SHORT)E.cy};
	SetConsoleCursorPosition(hConsole, cursorPos);
}


/* void editorRefreshScreen() {
	int y = 0;
	erow* r = nullptr;
	std::vector<char> ab;
	// stdout stuff
	HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
	CONSOLE_CURSOR_INFO cursorInfo;
	GetConsoleCursorInfo(hConsole, &cursorInfo);
	CONSOLE_SCREEN_BUFFER_INFO csbi;
	GetConsoleScreenBufferInfo(hConsole, &csbi);
	DWORD written; // temp variable -- unsused

	cursorInfo.bVisible = FALSE;
	SetConsoleCursorInfo(hConsole, &cursorInfo);

	SetConsoleCursorPosition(hConsole, {0, 0}); // go home
	for (y = 0; y < E.screenrows; y++) {
		int filerow = E.rowoff + y;

		if (filerow >= E.rows.size()) {
			if (E.rows.size() == 0 && y == E.screenrows / 3) {
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

	// Create a two rows status. First row:
	abAppend(ab, "\x1b[0K", 4);
	abAppend(ab, "\x1b[7m", 4);
	std::array<char, MAX_STATUS_LENGTH> status{};
	std::array<char, MAX_STATUS_LENGTH> rstatus{};
	int len = snprintf(status.data(), status.size(), "%.20s - %llu lines %s",
					   E.filename != nullptr ? E.filename : "[No Name]", E.rows.size(), E.dirty ? "(modified)" : "");
	int rlen = snprintf(rstatus.data(), rstatus.size(), "%u/%llu", E.rowoff + E.cy + 1, E.rows.size());
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

	// Second row depends on E.statusmsg and the status message update time.
	abAppend(ab, "\x1b[0K", 4);
	int msglen = strlen(E.statusmsg.data());
	if ((msglen != 0) && time(nullptr) - E.statusmsg_time < 5)
		abAppend(ab, E.statusmsg.data(), msglen <= E.screencols ? msglen : E.screencols);

	// Put cursor at its current position. Note that the horizontal position
    // at which the cursor is displayed may be different compared to 'E.cx'
    // because of TABs.
	int j = 0;
	int cx = 1;
	int filerow = E.rowoff + E.cy;
	erow* rows = (filerow >= E.rows.size()) ? nullptr : &E.rows[filerow];
	if (rows != nullptr) {
		for (j = E.coloff; j < (E.cx + E.coloff); j++) {
			if (j < rows->size && rows->chars[j] == TAB)
				cx += (TAB_SIZE - 1) - ((cx) % TAB_SIZE);
			cx++;
		}
	}

    WriteConsole(hConsole, ab.data(), static_cast<DWORD>(ab.size()), &written, nullptr);

	SetConsoleCursorPosition(hConsole, {(short)cx, (short)E.cy});
	cursorInfo.bVisible = TRUE;
	SetConsoleCursorInfo(hConsole, &cursorInfo);

	// ab is freed cause it's a vector
}
*/
#pragma endregion

#pragma region fileIO

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
	std::string buf = "";
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
#pragma endregion

#pragma region eventHandle
void editorMoveCursor(int key) {
	// Static variable to remember the original column position
	static int originalColumn = 0;

	int filerow = E.rowoff + E.cy;
	int filecol = E.coloff + E.cx;
	int rowlen = 0;
	erow* rows = (filerow >= E.rows.size()) ? nullptr : &E.rows[filerow];

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
		if (rows == nullptr) {
			originalColumn = E.cx;
			break;
		}
		if (filecol < rows->size) {
			if (E.cx == E.screencols - 1) {
				E.coloff++;
			} else {
				E.cx += 1;
			}
		} else if (filecol == rows->size) {
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
		rows = (E.rowoff + E.cy >= E.rows.size()) ? nullptr : &E.rows[E.rowoff + E.cy];
		rowlen = rows != nullptr ? rows->size : 0;
		E.cx = (originalColumn < rowlen) ? originalColumn : rowlen;
		E.coloff = (E.cx > E.screencols - 1) ? E.cx - E.screencols + 1 : 0;
		break;
	case ARROW_DOWN:
		// Save the current column position before moving
		originalColumn = originalColumn < E.cx ? E.cx : originalColumn;

		if (filerow <= E.rows.size() - 1) {
			if (E.cy == E.screenrows - 1) {
				E.rowoff++;
			} else {
				E.cy += 1;
			}
		}
		// Move to the original column or the end of the line if the line is
		// shorter
		rows = (E.rowoff + E.cy >= E.rows.size()) ? nullptr : &E.rows[E.rowoff + E.cy];
		rowlen = rows != nullptr ? rows->size : 0;
		E.cx = (originalColumn < rowlen) ? originalColumn : rowlen;
		E.coloff = (E.cx > E.screencols - 1) ? E.cx - E.screencols + 1 : 0;

		break;
	}
	/* Fix cx if the current line has not enough chars. */
	filerow = E.rowoff + E.cy;
	filecol = E.coloff + E.cx;
	rows = (filerow >= E.rows.size()) ? nullptr : &E.rows[filerow];
	rowlen = rows != nullptr ? rows->size : 0;
	if (filecol > rowlen) {
		E.cx -= filecol - rowlen;
		if (E.cx < 0) {
			E.coloff += E.cx;
			E.cx = 0;
		}
	}
}

#pragma endregion

#pragma region editorRows

/* Update the rendered version and the syntax highlight of a row. */
void editorUpdateRow(erow* rows) {
	size_t tabs = 0;
	size_t nonprint = 0;
	size_t j = 0;
	size_t idx = 0;

	/* Create a version of the row we can directly print on the screen,
     * respecting tabs, substituting non printable characters with '?'. */
	free(rows->render);
	for (j = 0; j < rows->size; j++)
		if (rows->chars[j] == TAB)
			tabs++;

	size_t allocsize =
		static_cast<size_t>(rows->size) + static_cast<size_t>(tabs) * 8 + static_cast<size_t>(nonprint) * 9 + 1;
	if (allocsize > UINT32_MAX) {
		throw std::runtime_error("Some line of the edited file is too long for kilo\n");
	}

	rows->render = (char*)malloc(rows->size + tabs * 8 + nonprint * 9 + 1);
	idx = 0;
	for (j = 0; j < rows->size; j++) {
		if (rows->chars[j] == TAB) {
			rows->render[idx++] = ' ';
			while ((idx + 1) % 8 != 0)
				rows->render[idx++] = ' ';
		} else {
			rows->render[idx++] = rows->chars[j];
		}
	}
	rows->rsize = idx;
	rows->render[idx] = '\0';

	/* Update the syntax highlighting attributes of the row. */
	rows->hl = (unsigned char*)realloc(rows->hl, rows->rsize);
	memset(rows->hl, HL_NORMAL, rows->rsize);
}

/* Insert a row at the specified position, shifting the other rows on the bottom
 * if required. */
void editorInsertRow(int at, const char* s, size_t len) {
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
	E.rows[at].chars = (char*)malloc(len + 1);
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
void editorFreeRow(erow* rows) {
	free(rows->render);
	free(rows->chars);
	free(rows->hl);
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
	for (size_t j = at; j < E.rows.size(); j++)
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

	p = buf = (char*)malloc(totlen);
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
void editorRowInsertChar(erow* rows, int at, int c) {
	if (at > rows->size) {
		/* Pad the string with spaces if the insert location is outside the
         * current length by more than a single character. */
		int padlen = at - rows->size;
		/* In the next line +2 means: new char and null term. */
		rows->chars = (char*)realloc(rows->chars, rows->size + padlen + 2);
		memset(rows->chars + rows->size, ' ', padlen);
		rows->chars[rows->size + padlen + 1] = '\0';
		rows->size += padlen + 1;
	} else {
		/* If we are in the middle of the string just make space for 1 new
         * char plus the (already existing) null term. */
		rows->chars = (char*)realloc(rows->chars, rows->size + 2);
		memmove(rows->chars + at + 1, rows->chars + at, rows->size - at + 1);
		rows->size++;
	}
	rows->chars[at] = c;
	editorUpdateRow(rows);
	E.dirty = true;
}

/* Append the string 's' at the end of a row */
void editorRowAppendString(erow* rows, char* s, size_t len) {
	rows->chars = (char*)realloc(rows->chars, rows->size + len + 1);
	memcpy(rows->chars + rows->size, s, len);
	rows->size += len;
	rows->chars[rows->size] = '\0';
	editorUpdateRow(rows);
	E.dirty = true;
}

/* Delete the character at offset 'at' from the specified row. */
void editorRowDelChar(erow* rows, int at) {
	if (rows->size <= at)
		return;
	memmove(rows->chars + at, rows->chars + at + 1, rows->size - at);
	editorUpdateRow(rows);
	rows->size--;
	E.dirty = true;
}

/* Insert the specified char at the current prompt position. */
void editorInsertChar(int c) {
	int filerow = E.rowoff + E.cy;
	int filecol = E.coloff + E.cx;
	erow* rows = (filerow >= E.rows.size()) ? nullptr : &E.rows[filerow];

	/* If the row where the cursor is currently located does not exist in our
     * logical representaion of the file, add enough empty rows as needed. */
	if (rows == nullptr) {
		while (E.rows.size() <= filerow)
			editorInsertRow(E.rows.size(), "", 0);
	}
	rows = &E.rows[filerow];
	editorRowInsertChar(rows, filecol, c);
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
	erow* rows = (filerow >= E.rows.size()) ? nullptr : &E.rows[filerow];

	// when at the last non-existant line
	// should just delete the previous if empty and move up one
	// if previous not empty will just go to the end
	if (rows == nullptr) {
		if (filerow != 0) {
			// Get the previous row
			erow* prevRow = &E.rows[filerow - 1];
			// If the previous row is empty, delete it and move up one line
			if (prevRow->size == 0) {
				editorDelRow(filerow - 1);
				E.cy--;
				E.cx = 0; // Move to the end of the new current line
			} else {
				// Previous row is not empty, move the cursor to the end of the previous row
				E.cy--;
				E.cx = prevRow->size;
			}
		}
		return;
	}
	// special case when it's the first and only line
	if (filecol == 0 && filerow == 0) {
		if (E.rows.size() == 1 && rows->size == 0) {
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
		editorRowAppendString(&E.rows[filerow - 1], rows->chars, rows->size);
		editorDelRow(filerow);
		rows = nullptr;
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
		editorRowDelChar(rows, filecol - 1);
		if (E.cx == 0 && (E.coloff != 0))
			E.coloff--;
		else
			E.cx--;
	}

	if (rows != nullptr)
		editorUpdateRow(rows);

	E.dirty = true;
}

#pragma endregion

#pragma region mainStuff

void editorStart(MyFs& myfs, const std::string& filenameIn){
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
		_getch();
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

void initEditor() {
	E.cx = 0;
	E.cy = 0;
	E.rowoff = 0;
	E.coloff = 0;
	E.screencols = 0;
	E.screenrows = 0;
	E.rows.clear();
	E.dirty = false;
	E.filename.clear();
	E.syntax = nullptr;
	E.rawmode = false;
	updateWindowSize();
}

/* Called at exit to avoid remaining in raw mode. */
void editorAtExit() {
	E.filename.clear();
	for (erow &row : E.rows) {
		editorFreeRow(&row);
	}
	E.rows.clear();
	disableRawMode();
}
#pragma endregion