#include "goodkilo.hpp"
#include "shellPrompt.hpp"

// Initialize static members

shellPrompt::shellPrompt()
	: cursorPosition(0), orig_termios(), prompt(">"), promptLength(2), commandHistory(HISTORY_LENGTH) {
	// could fail when piping like:
	// program | "1234"
	if (tcgetattr(STDIN_FILENO, &orig_termios) == -1) {
		die("tcgetattr");
	}
	enableRawMode();
}

shellPrompt::~shellPrompt() {
	disableRawMode();
}

void shellPrompt::moveCursorUnsafe(size_t newPosition) {
	// the 999 is just to make it guaranteed to be the last row
	std::cout << "\x1b[" << MAX_SCREEN_SIZE << ";" << newPosition + 1 << "H";
	std::cout.flush();
}

void shellPrompt::moveCursor() {
	if (cursorPosition <= promptLength) {
		cursorPosition = promptLength;
	} else if (cursorPosition > promptLength + input.length()) {
		cursorPosition = promptLength + input.length();
	}
	// Move cursor to the last row and specified column
	moveCursorUnsafe(cursorPosition);
}

#pragma region display

std::string shellPrompt::stripEscapeCodes(const std::string& text) {
	// Regex to match ansii  escape sequences
	std::regex escape_codes(R"([\u001b\u009b][[()#;?]*(?:[0-9]{1,4}(?:;[0-9]{0,4})*)?[0-9A-ORZcf-nqry=><])");
	return std::regex_replace(text, escape_codes, "");
}

void shellPrompt::setPrompt(const std::string& newPrompt) {
	prompt = newPrompt;
	promptLength = stripEscapeCodes(newPrompt).length();
	moveCursorUnsafe(0);
}

void shellPrompt::displayPrompt() {
	std::cout << prompt;
	cursorPosition = promptLength;
	updateDisplay();
}

void shellPrompt::updateDisplay() {
	// Move cursor to start of input
	moveCursorUnsafe(promptLength);

	// Display input
	std::cout << input;

	// Clear the rest of the line
	std::cout << "\033[K";

	// Move cursor back to correct position
	moveCursor();

	std::cout.flush();
}

#pragma endregion
#pragma region keyPresses

bool shellPrompt::processKeypress() {
	int c = readKey();
	switch (c) {
	case '\n': /* Enter */
	case '\r':
		return true;
	case CTRL_KEY('c'): /* Ctrl-c */
		break;
	case BACKSPACE:
	case CTRL_KEY('h'):
	case DEL_KEY:
		if (cursorPosition > promptLength) {
			input.erase(cursorPosition - promptLength - 1, 1);
			cursorPosition--;
			updateDisplay();
		}
		break;
	case ARROW_UP:
		try {
			commandHistory.goBack();
			input = commandHistory.getCurrentCommand();
			cursorPosition = promptLength + input.size();
			updateDisplay();
		} catch (const std::out_of_range&) {
			// Ignore, no more history to go back to
		}
		break;
	case ARROW_DOWN:
		try {
			commandHistory.goForward();
			input = commandHistory.getCurrentCommand();
			cursorPosition = promptLength + input.size();
			updateDisplay();
		} catch (const std::out_of_range&) {
			input.clear();
			cursorPosition = promptLength;
			updateDisplay();
		}
		break;
	case ARROW_LEFT:
		if (cursorPosition > promptLength) {
			cursorPosition--;
			moveCursor();
		}
		break;
	case ARROW_RIGHT:
		if (cursorPosition < promptLength + input.size()) {
			cursorPosition++;
			moveCursor();
		}
		break;
	case CTRL_KEY('l'):
		updateDisplay();
		break;
	case ESC:
		break;
	default:
		if (iscntrl(c) == 0) {
			input.insert(cursorPosition - promptLength, 1, c);
			cursorPosition++;
			updateDisplay();
		}
		break;
	}
	return false;
}

std::string shellPrompt::readInput() {
	moveCursorUnsafe(0);
	displayPrompt();

	while (!processKeypress()) {
		// Keep processing keypresses until enter is pressed
	}
	write(STDOUT_FILENO, "\r\n", 2); // write newline back to terminal

	std::string inputCopy = input;
	if (!inputCopy.empty()) {
		commandHistory.addCommand(inputCopy);
	}
	commandHistory.reset();
	input.clear();
	return inputCopy;
}

#pragma endregion
#pragma region rawMode

void shellPrompt::enableRawMode() {
	struct termios raw {};

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
	// raw.c_oflag &= ~(OPOST);
	/* control modes - set 8 bit chars */
	raw.c_cflag |= (CS8);
	/* local modes - choing off, canonical off, no extended functions,
     * no signal chars (^Z,^C) */
	raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
	/* control chars - set return condition: min number of bytes and timer. */
	raw.c_cc[VMIN] = 0; /* Return each byte, or zero for timeout. */
	// raw.c_cc[VTIME] = 1; /* 100 ms timeout (unit is tens of second). */

	/* put terminal in raw mode after flushing */
	if (tcsetattr(STDOUT_FILENO, TCSAFLUSH, &raw) < 0) {
		die("tcgetattr");
	}
}

void shellPrompt::disableRawMode() {
	if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios) == -1) {
		die("tcsetattr");
	}
}

#pragma endregion
#pragma region history

CommandHistory::CommandHistory(size_t size) : max_size(size), current_index(0) {
}

void CommandHistory::addCommand(const std::string& command) {
	if (history.size() == max_size) {
		history.pop_back(); // Remove the oldest command
	}
	history.push_front(command); // Insert new command at the front
	reset();					 // Reset the current index when a new command is added
}

std::string CommandHistory::getCurrentCommand() const {
	if (history.empty()) {
		throw std::out_of_range("No commands in history.");
	}
	return history[current_index];
}

void CommandHistory::goBack() {
	if (history.empty()) {
		throw std::out_of_range("No commands in history.");
	}
	if (is_at_end) {
		is_at_end = false; // Set is_at_end to false on the first up press
	} else if (current_index < history.size() - 1) {
		++current_index; // Increment only if not at the oldest command
	}
}

void CommandHistory::goForward() {
	if (history.empty() || current_index == 0) {
		throw std::out_of_range("No more commands to go forward.");
	}
	--current_index;
	is_at_end = (current_index == 0);
}

void CommandHistory::reset() {
	current_index = 0;
	is_at_end = true;
}

#pragma endregion