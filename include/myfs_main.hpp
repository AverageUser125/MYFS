#pragma once

#include "config.hpp"
#include "myfs.hpp"
#include "blkdev.hpp"
#include "goodkilo.hpp"
#include "shellPrompt.hpp"
#include <iomanip>

#include <cmath>

// for the printEntires function
// makes it look nicer
#define COLUMN_SPACING 28

std::string addCurrentDirAdvance(const std::string& path, const std::string& currentDir);
void printHelpMessage();
std::vector<std::string> splitCmd(const std::string& cmd);
CommandType getCommandType(const std::string& cmd);
void editFile(MyFs& myfs, const std::string& fileLocation);
void printEntries(const std::vector<EntryInfo>& entries);
bool handleCommand(const std::string& command, std::vector<std::string>& args, MyFs& myfs, std::string& currentDir);
int main(int argc, char** argv);