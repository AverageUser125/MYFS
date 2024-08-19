#pragma once

#define CTRL_KEY(k) ((k) & 0x1f)
#define INVERSE_COLOR "\033[7m"

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#undef DELETE
#include <iostream>
#include <vector>
#include <string>
#include <conio.h>
#include "config.hpp"
#include "myfs.hpp"
#include <stdexcept>
#include <vector>
#include <string>
#include <cstdarg>

using erow = struct erow {
	size_t idx;  /* Row index in the file, zero-based. */
	size_t size; /* Size of the row, excluding the null term. */
	size_t rsize; /* Size of the rendered row. */
	char* chars;	   /* Row content. */
	char* render;	   /* Row content "rendered" for screen (for TABs). */
	unsigned char* hl; /* Syntax highlight type for each character in render.*/
	int hl_oc;		   /* Row had open comment at end in last syntax highlight
                          check. */
};

#define MAX_STATUS_LENGTH 80

struct editorConfig {
	std::vector<erow> rows; /* Rows */
	std::string filename; /* Currently open filename */
	std::array<char, MAX_STATUS_LENGTH> statusmsg = {0};
	time_t statusmsg_time = 0;
	struct editorSyntax* syntax = nullptr; /* Current syntax highlight, or NULL. */
	size_t cx = 0, cy = 0;		/* Cursor x and y position in characters */
	size_t rowoff = 0;			   /* Offset of row displayed. */
	size_t coloff = 0;			/* Offset of column displayed. */
	size_t screenrows = 0;		   /* Number of rows that we can show */
	size_t screencols = 0;		/* Number of cols that we can show */
	bool rawmode = false;	/* Raw mode*/
	bool dirty = false;		/* File modified but not saved. */
};

enum KEY_ACTION {
	KEY_NULL = 0,	 /* NULL */
	TAB = 9,		 /* Tab */
	ENTER = 13,		 /* Enter */
	ESC = 27,		 /* Escape */
	BACKSPACE = 127, /* Backspace */
	/* The following are just soft codes, not really reported by the
     * terminal directly. */
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

enum syntaxHighlight {
	HL_NORMAL,
	HL_NONPRINT,
	HL_COMMENT,	  /* Single line comment. */
	HL_MLCOMMENT, /* Multi-line comment. */
	HL_KEYWORD1,
	HL_KEYWORD2,
	HL_STRING,
	HL_NUMBER,
	HL_MATCH /* Search match. */
};


int readKey();
bool editorProcessKeypress(MyFs& myfs, int key);
void editorMoveCursor(int key);
void initEditor();
void updateWindowSize();
void editorInsertNewline();
void editorDelChar();
void editorInsertChar(int c);
void editorRefreshScreen();
void editorSetStatusMessage(const char* fmt, ...);
int editorOpen(MyFs& myfs, const std::string& filename);
std::string editorPrompt(const char* prompt);
int editorSave(MyFs & myfs);
std::string editorRowsToString();
void editorInsertRow(int at, const char* s, size_t len);
void editorStart(MyFs& myfs, const std::string& filenameIn);
void disableRawMode();
int enableRawMode();
void fixCursor();