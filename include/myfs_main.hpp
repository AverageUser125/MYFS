#pragma once

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

#include "config.hpp"
#include "myfs.hpp"
#include "blkdev.hpp"
#include <iomanip>
#include <iostream>
#include <cmath>

// for the printEntires function
// makes it look nicer
#define COLUMN_SPACING 28

std::string addCurrentDirAdvance(const std::string& path, const std::string& currentDir);
void printHelpMessage();
std::vector<std::string> splitCmd(const std::string& cmd);
CommandType getCommandType(const std::string& cmd);
void printEntries(const std::vector<EntryInfo>& entries);
Errors handleCommand(const std::string& command, std::vector<std::string>& args, MyFs& myfs, std::string& currentDir);
int main(int argc, char** argv);