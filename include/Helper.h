#pragma once

#include <iostream>
#include <string>
#include <utility>
#include <vector>
#include <sstream>
#include <iomanip>
#include <cassert>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <numeric>
#include <optional>
#include <set>
#include <stdexcept>
#include <cctype>
#include <cerrno>
#include <fcntl.h>
#include <csignal>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <sys/types.h>
#include <termios.h>
#include <ctime>
#include <unistd.h>

#define FS_NAME "myfs"

#define MYFS_MAGIC "MYFS"
#define CURR_VERSION 0x03
#define MAX_DIRECTORY_SIZE 6
#define FAT_SIZE 4096
#define MAX_PATH_LENGTH 256
#define HEADER_SIZE (sizeof(uint8_t) + sizeof(size_t))
#define ENTRY_BUFFER_SIZE (HEADER_SIZE + MAX_PATH_LENGTH + sizeof(size_t) + sizeof(size_t))


// clang-format off
// Console colors
#define RESET               "\033[0m"
#define RED                 "\033[31m"
#define GREEN               "\033[32m"
#define YELLOW              "\033[33m"
#define BLUE                "\033[34m"
#define MAGENTA             "\033[35m"
#define CYAN                "\033[36m"
#define WHITE               "\033[37m"
#define BOLDYELLOW          "\033[1m\033[33m"
#define BOLDGREEN           "\033[01;32m"
#define BOLDBLUE            "\033[01;34m"

#define COLUMN_SPACING      28

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


// https://wiki.sei.cmu.edu/confluence/display/cplusplus/ERR58-CPP.+Handle+all+exceptions+thrown+before+main()+begins+executing
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
void printHelpMessage();
std::pair<std::string, std::string> splitPath(const std::string& filepath);
std::string ensureStartsWithSlash(const std::string& str);
std::string addCurrentDirAdvance(const std::string& filepath, const std::string& currentDir);

std::string addCurrentDir(const std::string& filename, const std::string& currentDir);

// Set allocator block size depending on if 32 bit or 64 bit, work in linux only
// why do I do this you may ask? malloc does the same so why not?
#if __GNUC__
#if __x86_64__ || __ppc64__
#define DEFAULT_BLOCK_SIZE 32
#else
#define DEFAULT_BLOCK_SIZE 16
#endif
#endif