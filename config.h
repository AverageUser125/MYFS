#pragma once
#define _CRT_SECURE_NO_WARNINGS

#include <cassert>

// clang-format off
#define FS_NAME "myfs"
#define COLUMN_SPACING 28

#define KILO_QUIT_TIMES 3
#define KILO_VERSION "0.0.2"
#define WELCOME_MESSAGE "Kilo editor -- verison " KILO_VERSION
#define STATUS_MESSAGE_TIME 5
#define TAB_SIZE 4

#define REGION_DEFAULT_CAPACITY (8 * 1024)

#define RESET "\033[0m"
#define RED "\033[31m"
#define GREEN "\033[32m"
#define YELLOW "\033[33m"
#define BLUE "\033[34m"
#define MAGENTA "\033[35m"
#define CYAN "\033[36m"
#define WHITE "\033[37m"
#define BOLDYELLOW "\033[1m\033[33m"
#define BOLDGREEN "\033[01;32m"
#define BOLDBLUE "\033[01;34m"

// Commands
#define FORMAT_CMD 			  "format"
#define LIST_CMD 			  "ls"
#define CONTENT_CMD 		  "cat"
#define CREATE_FILE_CMD 	  "touch"
#define CREATE_DIR_CMD 	      "mkdir"
#define EDIT_CMD 			  "edit"
#define TREE_CMD 			  "tr"
#define HELP_CMD 			  "help"
#define EXIT_CMD1 			  "exit"
#define EXIT_CMD2 			  "quit"
#define CD_CMD 			      "cd"
#define MOVE_CMD 		      "mv"
#define COPY_CMD 			  "cp"
#define DELETE_DIR_CMD		  "rmdir"
#define DELETE_CMD 		      "rm"


// reasons to not be using std::string: https://wiki.sei.cmu.edu/confluence/display/cplusplus/ERR58-CPP.+Handle+all+exceptions+thrown+before+main()+begins+executing
static const char* const MENU_ASCII_ART =    
	"\n\n"
	"                   $$$$$$$$$\\$$\\ $$\\                  $$$$$$\\                        $$\\                             \r\n"
	"                   $$  _____|\\__|$$ |                $$  __$$\\                       $$ |                            \r\n"
	"                   $$ |      $$\\ $$ | $$$$$$\\        $$ /  \\__|$$\\   $$\\  $$$$$$$\\ $$$$$$\\    $$$$$$\\  $$$$$$\\$$$$\\  \r\n"
	"                   $$$$$$\\   $$ |$$ |$$  __$$\\       \\$$$$$$\\  $$ |  $$ |$$  _____|\\_$$  _|  $$  __$$\\ $$  _$$  _$$\\ \r\n"
	"                   $$  __|   $$ |$$ |$$$$$$$$ |       \\____$$\\ $$ |  $$ |\\$$$$$$\\    $$ |    $$$$$$$$ |$$ / $$ / $$ |\r\n"
	"                   $$ |      $$ |$$ |$$   ____|      $$\\   $$ |$$ |  $$ | \\____$$\\   $$ |$$\\ $$   ____|$$ | $$ | $$ |\r\n"
	"                   $$ |      $$ |$$ |\\$$$$$$$\\       \\$$$$$$  |\\$$$$$$$ |$$$$$$$  |  \\$$$$  |\\$$$$$$$\\ $$ | $$ | $$ |\r\n"
	"                   \\__|      \\__|\\__| \\_______|       \\______/  \\____$$ |\\_______/    \\____/  \\_______|\\__| \\__| \\__|\r\n"
	"                                                               $$\\   $$ |                                            \r\n"
	"                                                               \\$$$$$$  |                                            \r\n"
	"                                                                \\______/                                             \r\n\r\n";
// clang-format on

enum class CommandType {
	FORMAT,
	LIST,
	EXIT,
	HELP,
	CREATE_FILE,
	CONTENT,
	DELETE_DIR,
	DELETE,
	EDIT,
	CREATE_DIR,
	CD,
	TREE,
	COPY,
	MOVE,
	UNKNOWN
};

#ifdef _MSC_VER
#define ALWAYS_INLINE __forceinline
#define PACK(__Declaration__) __pragma(pack(push, 1)) __Declaration__ __pragma(pack(pop))
#else
#define ALWAYS_INLINE __attribute__((always_inline))
#define PACK(__Declaration__) __Declaration__ __attribute__((__packed__))
#endif

// https://www.reddit.com/r/ProgrammerTIL/comments/58c6dx/til_how_to_defer_in_c/
template <typename F>
struct saucy_defer {
	F f;

	ALWAYS_INLINE saucy_defer(F fIn) : f(fIn) {
	}

	ALWAYS_INLINE ~saucy_defer() {
		f();
	}
};

#define DEFER_1(x, y) x##y
#define DEFER_2(x, y) DEFER_1(x, y)
#define DEFER_3(x) DEFER_2(x, __COUNTER__)
#define defer(code) auto DEFER_3(_defer_) = ::saucy_defer([&]() { code; })
