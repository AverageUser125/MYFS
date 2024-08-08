#pragma once
#ifndef TERMINAL_H
#define TERMINAL_H

#include <string>
#include <termios.h>
#include <iostream>
#include <unistd.h>
#include <cerrno>
#include <cstring>
#include <cstdlib>
#include <deque>
#include <regex>
#include <csignal>
#include "Helper.hpp"
#include "goodkilo.hpp"

#define HISTORY_LENGTH 5
#define MAX_INPUT_LENGTH 88
#define MAX_SCREEN_SIZE 999

class CommandHistory {
  private:
	std::deque<std::string> history;
	size_t max_size;
	size_t current_index; // Index for navigation
	bool is_at_end = false;

  public:
	explicit CommandHistory(size_t size);

	void addCommand(const std::string& command);
	[[nodiscard]] std::string getCurrentCommand() const;
	void goBack();
	void goForward();
	void reset();
};

class shellPrompt {
  public:
	shellPrompt();
	~shellPrompt();
	std::string readInput();
	void setPrompt(const std::string& newPrompt);

  private:
	void enableRawMode();
	void disableRawMode();
	void moveCursor();
	static void moveCursorUnsafe(size_t n);
	void updateDisplay();
	bool processKeypress();
	void displayPrompt();
	static std::string stripEscapeCodes(const std::string& text);

	size_t cursorPosition; // Track cursor position in the input string
	std::string input;	   // Store the input string
	struct termios orig_termios;
	std::string prompt; // Prompt for user input
	size_t promptLength;

	CommandHistory commandHistory; // Command history for up and down arrow keys
};

#endif // TERMINAL_H
