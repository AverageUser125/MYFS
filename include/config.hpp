#pragma once

#include <cassert>

#pragma region myfsSettings
constexpr const char* const MYFS_MAGIC = "MYFS";
#define CURR_VERSION 0x03
constexpr size_t MAX_DIRECTORY_SIZE = 6;
constexpr size_t FAT_SIZE = 4096;
#pragma endregion

#pragma region editor
#define KILO_QUIT_TIMES 3
#define KILO_VERSION "0.0.2"
#define WELCOME_MESSAGE "Kilo editor -- verison " KILO_VERSION "\x1b[0K\r\n"
#define TAB_SIZE 8
#pragma endregion

// clang-format off
#pragma region shellSettings
#define FS_NAME "myfs"

// Commands
#define LIST_CMD 			  "ls"
#define CONTENT_CMD 		  "cat"
#define CREATE_FILE_CMD 	  "touch"
#define CREATE_DIR_CMD 	      "mkdir"
#define EDIT_CMD 			  "edit"
#define TREE_CMD 			  "tr"
#define HELP_CMD 			  "help"
#define EXIT_CMD 			  "exit"
#define CD_CMD 			      "cd"
#define MOVE_CMD 		      "mv"
#define COPY_CMD 			  "cp"
#define DELETE_CMD 		      "rm"


// reasons to not be using std::string: https://wiki.sei.cmu.edu/confluence/display/cplusplus/ERR58-CPP.+Handle+all+exceptions+thrown+before+main()+begins+executing
constexpr const char* const MENU_ASCII_ART =    
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
	LIST,
	EXIT,
	HELP,
	CREATE_FILE,
	CONTENT,
	DELETE,
	EDIT,
	CREATE_DIR,
	CD,
	TREE,
	COPY,
	MOVE,
	UNKNOWN
};
#pragma endregion

#pragma region allocatorSettings

// Set allocator block size depending on if 32 bit or 64 bit, work in linux only
// why do I do this you may ask? malloc does the same so why not?

#define DEFAULT_BLOCK_SIZE 32

#pragma endregion