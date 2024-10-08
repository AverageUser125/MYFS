#pragma once
#ifndef GOODKILO_H
#define GOODKILO_H

#define KILO_QUERY_LEN 256
#define ABUF_INIT {NULL, 0}
#define MAX_STATUS_LENGTH 80
#define MAX_KEYPRESS_LENGTH 20
#define NUMBER_BASE 10

#define CTRL_KEY(k) ((k) & 0x1f)

#include "myfs.hpp"
#include "config.hpp"
#include <csignal>
#include <cstdio>
#include <termios.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <cstdlib>
#include <cstdarg>
#include <functional>
#include <array>
#include <vector>

/* Syntax highlight types */
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

/* This structure represents a single line of the file we are editing. */
using erow = struct erow {
	int idx;		   /* Row index in the file, zero-based. */
	int size;		   /* Size of the row, excluding the null term. */
	int rsize;		   /* Size of the rendered row. */
	char* chars;	   /* Row content. */
	char* render;	   /* Row content "rendered" for screen (for TABs). */
	unsigned char* hl; /* Syntax highlight type for each character in render.*/
	int hl_oc;		   /* Row had open comment at end in last syntax highlight
                          check. */
};

struct editorConfig {
	int cx, cy;		/* Cursor x and y position in characters */
	int rowoff;		/* Offset of row displayed. */
	int coloff;		/* Offset of column displayed. */
	int screenrows; /* Number of rows that we can show */
	int screencols; /* Number of cols that we can show */
	bool rawmode;	/* Is terminal raw mode enabled? */
	std::vector<erow> rows;		/* Rows */
	bool dirty;		/* File modified but not saved. */
	std::string filename; /* Currently open filename */
	std::array<char, MAX_STATUS_LENGTH> statusmsg;
	time_t statusmsg_time;
	struct editorSyntax* syntax; /* Current syntax highlight, or NULL. */
};

enum KEY_ACTION {
	KEY_NULL = 0,	 /* NULL */
	TAB = 9,		 /* Tab */
	ENTER = 13,		 /* Enter */
	CTRL_U = 21,	 /* Ctrl-u */
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

/*** terminal ***/
void handleSigWinCh(int unused __attribute__((unused)));
void disableRawMode();
int enableRawMode();
void die(const char* s);
int getWindowSize(int* rows, int* cols);
int getCursorPosition(int* rows, int* cols);

/*** append buffer ***/
using abuf = struct abuf {
	char* b;
	int len;
};

/*** row operations ***/
void editorRowAppendString(erow* row, char* s, int len);
void editorFreeRow(erow* row);
void editorDelRow(int at);
void editorInsertRow(int at, const char* s, int len);
void editorUpdateRow(erow* row);
void editorRowInsertChar(erow* row, int at, int c);
void editorRowDelChar(erow* row, int at);
void editorOpen(const std::string& filename, MyFs& myfs);
char* editorRowsToString(int* buflen);
int editorSave(MyFs& myfs);
void editorFind();
std::string editorPrompt(const char* prompt);
void editorMoveCursor(int key);
int readKey();
bool editorProcessKeypress(MyFs& myfs);
int interpretExtendedKeys();
inline void snapCursorToEndOfLine(erow* row);
void editorSetStatusMessage(const char* fmt, ...);
void editorRefreshScreen();
void editorWelcomeDraw(abuf* ab);
void editorInsertChar(int c);
void editorDelChar();
void editorInsertNewline();
void initEditor();
void editorStart(MyFs& myfs, const std::string& filenameIn);
void fixCursor();
void editorAtExit();

#define HLDB_ENTRIES (sizeof(HLDB) / sizeof(HLDB[0]))

#endif