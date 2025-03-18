#include "config.h"
#include <iostream>
#include "myfs.h"
#include "blkdev.h"
#include <type_traits>
#include <vector>
#include <iomanip>
#include <ios>
#include <sstream>
#include <fstream>
#include <stdio.h>
#include <map>
#include "editor.h"
#include <cerrno>
#include <cstring>
#include <system_error>
#include "arena.h"
#include <ctime>

// clang-format off
void printHelpMessage() {
	std::cout
		<< std::setw(COLUMN_SPACING) << std::left << MAGENTA LIST_CMD"    <dir>" << std::setw(0) << YELLOW "Lists directory content.\r\n" RESET
		<< std::setw(COLUMN_SPACING) << std::left << MAGENTA CONTENT_CMD"   <path>" << std::setw(0) << YELLOW "Shows file content.\r\n" RESET
		<< std::setw(COLUMN_SPACING) << std::left << MAGENTA CREATE_FILE_CMD" <path>" << std::setw(0)<< YELLOW "Creates an empty file.\r\n" RESET
		<< std::setw(COLUMN_SPACING) << std::left << MAGENTA CREATE_DIR_CMD" <path>" << std::setw(0) << YELLOW "Creates an empty directory.\r\n" RESET
		<< std::setw(COLUMN_SPACING) << std::left << MAGENTA CD_CMD"    <path>" << std::setw(0) << YELLOW "Changes current directory.\r\n" RESET
		<< std::setw(COLUMN_SPACING) << std::left << MAGENTA DELETE_CMD"    <path>" << std::setw(0) << YELLOW "Removes current directory.\r\n" RESET
		<< std::setw(COLUMN_SPACING) << std::left << MAGENTA EDIT_CMD"  <path>" << std::setw(0) << YELLOW "Re-sets file content.\r\n" RESET
		<< std::setw(COLUMN_SPACING) << std::left << MAGENTA MOVE_CMD"    <path1> <path2>" << std::setw(0) << YELLOW "Moves the file.\r\n" RESET
		<< std::setw(COLUMN_SPACING) << std::left << MAGENTA COPY_CMD"    <path1> <path2>" << std::setw(0) << YELLOW "Copies a file.\r\n" RESET
		<< std::setw(COLUMN_SPACING) << std::left << MAGENTA HELP_CMD << std::setw(0) << YELLOW "Shows this help message.\r\n" RESET
		<< std::setw(COLUMN_SPACING) << std::left << MAGENTA EXIT_CMD1 << std::setw(0) << YELLOW "Gracefully exit.\r\n" RESET;

}

// clang-format on

std::string addCurrentDirAdvance(const std::string& path, const std::string& currentDir) {
	const std::vector<std::string> specialDirectory = {"..", "."};
	std::string currentPath = currentDir; // Start from the given currentDir
	std::vector<std::string> pathTokens;

	bool isAction = false;

	// if it is just "/", no checks needed
	if (path.empty() || path == "/") {
		return "/";
	}
	if (path == ".") {
		return currentDir;
	}

	if (path[0] == '/') {
		currentPath = "/";
	}
	std::istringstream stream(path);

	// account for start with ./ means current directory
	if (path.substr(0, 2) == "./") {
		stream.ignore(2);
	}

	std::string currToken;
	while (std::getline(stream, currToken, '/')) {
		pathTokens.push_back(currToken);
	}

	// check for special characters
	for (const auto& token : pathTokens) {
		// checks for special characters; sets action
		isAction = token == specialDirectory[1] || token == specialDirectory[0];

		// action depending on character
		if (isAction) { // ..
			size_t index = currentPath.find_last_of('/');
			if (index != std::string::npos) {
				currentPath.erase(index);
			}

			if (currentPath.empty()) {
				currentPath = "/";
			}
		} else { // Normal path
			if (currentPath.empty() || currentPath == "/") {
				currentPath += token;
			} else {
				currentPath += "/" + token;
			}
		}
	}

	// Ensure the path starts with a single "/"
	if (currentPath.empty()) {
		currentPath = "/";
	} else if (currentPath[0] != '/') {
		currentPath = "/" + currentPath;
	}

	return currentPath;
}

std::vector<std::string> splitCmd(const std::string& cmd) {
	std::vector<std::string> ans;
	std::stringstream ss(cmd);
	std::string part;
	bool inQuotes = false;
	std::string current;

	while (std::getline(ss, part, ' ')) {
		if (part.empty()) {
			continue;
		}
		if (!inQuotes && part.front() == '"' && part.back() == '"') {
			// Handle strings fully enclosed in quotes
			ans.push_back(part.substr(1, part.size() - 2));
		} else if (!inQuotes && part.front() == '"') {
			// Start of a quoted string
			inQuotes = true;
			current = part.substr(1) + " ";
		} else if (inQuotes && part.back() == '"') {
			// End of a quoted string
			current += part.substr(0, part.size() - 1);
			ans.push_back(current);
			inQuotes = false;
		} else if (inQuotes) {
			// Middle of a quoted string
			current += part + " ";
		} else {
			// Regular word
			ans.push_back(part);
		}
	}

	return ans;
}

CommandType getCommandType(const std::string& cmd) {
	static const std::map<std::string, CommandType> commandMap = {{FORMAT_CMD, CommandType::FORMAT},
																  {LIST_CMD, CommandType::LIST},
																  {EXIT_CMD1, CommandType::EXIT},
																  {EXIT_CMD2, CommandType::EXIT},
																  {HELP_CMD, CommandType::HELP},
																  {CREATE_FILE_CMD, CommandType::CREATE_FILE},
																  {CONTENT_CMD, CommandType::CONTENT},
																  {DELETE_CMD, CommandType::DELETE},
																  {DELETE_DIR_CMD, CommandType::DELETE_DIR},
																  {TREE_CMD, CommandType::TREE},
																  {EDIT_CMD, CommandType::EDIT},
																  {CREATE_DIR_CMD, CommandType::CREATE_DIR},
																  {CD_CMD, CommandType::CD},
																  {MOVE_CMD, CommandType::MOVE},
																  {COPY_CMD, CommandType::COPY}};

	auto it = commandMap.find(cmd);
	return (it != commandMap.end()) ? it->second : CommandType::UNKNOWN;
}

void printTree(const std::string& dir, MyFs& myfs, int depth = 0) {
	std::vector<std::string> contents;
	if (!myfs.getDirectoryContents(dir, contents)) {
		if (errno != ENOTDIR) {
			std::cerr << "ERROR [" << dir << "]: " << strerror(errno) << '\n';
		}
		return;
	}

	for (const auto& entry : contents) {
		if (entry == "." || entry == "..") {
			continue;
		}
		std::cout << std::string(depth * 2, ' ') << "- " << entry << '\n';

		std::string fullPath;
		if (dir == "/") {
			fullPath.reserve(entry.size() + 2);
			fullPath = "/";
			fullPath += entry;
		} else {
			fullPath.reserve(dir.size() + entry.size() + 2);
			fullPath = dir;
			fullPath += "/";
			fullPath += entry;
		}
		printTree(fullPath, myfs, depth + 1);
	}
}

void printDirectoryInfo(const std::vector<MyFs::FileInfo>& files, const std::string& dirPath) {
	printf("\n\n\t\tFolder: %s\n\n\n", dirPath.c_str());
	printf("%10s%4s%8s%6s%6s%10s%28s  %s\n", "Rsights", "Lc", "Inode", "UID", "GID", "Size", "Modification time",
		   "Name");
	printf("%10s%4s%8s%6s%6s%10s%28s  %s\n", "======", "==", "====", "===", "===", "====", "=================", "====");

	for (const auto& file : files) {
		char* modifTime = ctime(&file.modificationTime);
		if (modifTime != nullptr)
			modifTime[strlen(modifTime) - 1] = '\0';

		char modeStr[9] = {0};
		MyFs::rightsToString(file.mode, modeStr);
		printf("%10s%4hu%8u%6u%6u%10u%28s  %s\n", modeStr, file.linkCount, file.inode, file.uid, file.gid, file.size,
			   modifTime ? modifTime : "-", file.name.c_str());
	}
}

bool handleCommand(const std::string& command, std::vector<std::string>& args, MyFs& myfs, std::string& currentDir) {

	CommandType commandType = getCommandType(command);

	switch (commandType) {
	case CommandType::CREATE_FILE: {
		if (args.empty()) {
			throw std::runtime_error(CREATE_FILE_CMD " needs arguments");
		}
		for (const std::string& filename : args) {
			if (!myfs.createFile(filename)) {
				std::cerr << "ERROR [" << filename << "]: " << strerror(errno) << '\n';
			}
		}
		break;
	}
	case CommandType::CONTENT: {
		if (args.empty()) {
			throw std::runtime_error(CONTENT_CMD " needs arguments");
		}
		std::string content;
		if (!myfs.getContent(args[0], content)) {
			std::cerr << "ERROR [" << args[0] << "]: " << strerror(errno) << '\n';
		}
		std::cout << content << '\n';
		break;
	}
	case CommandType::EDIT: {
		if (args.empty()) {
			editorStart(myfs, "");
		} else if (args.size() == 1) {
			editorStart(myfs, args[0]);
		} else {
			throw std::runtime_error(EDIT_CMD " needs only 1 argument");
		}
		break;
	}
	case CommandType::LIST: {
		std::vector<MyFs::FileInfo> files;
		const std::string& path = args.empty() ? currentDir : args[0];

		if (!myfs.getDirectoryInfo(path, files)) {
			std::cerr << "ERROR [" << path << "]: " << strerror(errno) << '\n';
		} else {
			printDirectoryInfo(files, path);
		}
		break;
	}
	case CommandType::TREE: {
		if (!args.empty()) {
			throw std::runtime_error(LIST_CMD " doesn't need arguments");
		}
		printTree("/", myfs);
		break;
	}
	case CommandType::DELETE: {
		if (args.empty()) {
			throw std::runtime_error(DELETE_CMD " needs arguments");
		}
		for (const std::string& filename : args) {
			if (!myfs.deleteFile(filename)) {
				std::cerr << "ERROR [" << filename << "]: " << strerror(errno) << '\n';
			}
		}
		break;
	}
	case CommandType::DELETE_DIR: {
		if (args.empty()) {
			throw std::runtime_error(DELETE_CMD " needs arguments");
		}
		for (const std::string& filename : args) {
			if (!myfs.deleteDirectory(filename)) {
				std::cerr << "ERROR [" << filename << "]: " << strerror(errno) << '\n';
			}
		}
		break;
	}
	case CommandType::CREATE_DIR: {
		if (args.empty()) {
			throw std::runtime_error(DELETE_CMD " needs arguments");
		}
		for (const std::string& filename : args) {
			if (!myfs.createDirectory(filename)) {
				std::cerr << "ERROR [" << filename << "]: " << strerror(errno) << '\n';
			}
		}
		break;
	}
	case CommandType::CD: {
		if (args.size() != 1) {
			throw std::runtime_error(CD_CMD " needs exactly 1 argument");
		}
		std::string newCurr = addCurrentDirAdvance(args[0], currentDir);
		// TODO: check if directory
		if (!myfs.isFileExists(newCurr)) {
			std::cerr << "ERROR [" << newCurr << "]: Directory does not exist\n";
			break;
		}
		currentDir = std::move(newCurr);
		break;
	}
	case CommandType::FORMAT: {
		myfs.format();
		break;
	}
	case CommandType::COPY: {
		if (args.size() != 2) {
			throw std::runtime_error(COPY_CMD " needs exactly 2 arguments");
		}
		if (!myfs.copyFile(args[0], args[1])) {
			std::cerr << "ERROR " << strerror(errno) << '\n';
		}
		break;
	}
	case CommandType::MOVE: {
		if (args.size() != 2) {
			throw std::runtime_error(MOVE_CMD " needs exactly 2 arguments");
		}
		if (!myfs.moveFile(args[0], args[1])) {
			std::cerr << "ERROR " << strerror(errno) << '\n';
		}
		break;
	}
	case CommandType::HELP:
		printHelpMessage();
		break;
	case CommandType::EXIT:
		return true;
	case CommandType::UNKNOWN:
	default: {
		std::cout << RED "Unknown command: " << command << RESET "\n";
		break;
	}
	}

	return false;
}

int realMain(int argc, char** argv) {
	std::string bldevfile;
	if (argc == 1) {
#ifdef _WIN32
		bldevfile = R"(C:\Users\Cyber_User\Documents\magshimim\Year2\Architectury\W14\file.bin)";
#else
		bldevfile = "/home/user/architecture/W14/file.bin";
#endif
		//std::cout << CYAN "Please enter the file name: " RESET;
		//std::cin >> bldevfile;
		// Flush stdin to clear any leftover input
		//std::cin.clear();
		//std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
	} else if (argc == 2) {
		bldevfile = argv[1];
	} else {
		std::cerr << "Too many arguments\n";
		return 1;
	}

	std::string currentDir = "/";
	// may fail, if can't create file, or file is read-only
	BlockDeviceSimulator device(bldevfile);
	MyFs myfs(std::move(device));

	// Print the welcome message
	std::cout << GREEN << MENU_ASCII_ART << RESET "\n";
	std::cout << "To get help, please type 'help' on the prompt below.\n\n";

	bool exit = false;
	while (!exit) {
		std::cout << (BOLDGREEN FS_NAME RESET ":" BOLDBLUE + currentDir + RESET "$ ");

		std::string cmdline;
		std::getline(std::cin, cmdline, '\n');
		if (cmdline.empty()) {
			continue;
		}

		std::vector<std::string> cmd = splitCmd(cmdline);
		std::string command = cmd[0];
		std::vector<std::string> args(cmd.begin() + 1, cmd.end());
		for (std::string& arg : args) {
			arg = addCurrentDirAdvance(arg, currentDir);
		}

		try {
			exit = handleCommand(command, args, myfs, currentDir);
		} catch (const std::exception& e) {
			std::cout << RED << "An error occurred: " << e.what() << RESET "\n";
		}
	}
	return 0;
}

int main(int argc, char** argv) {
	try {
		realMain(argc, argv);
	} catch (std::exception& e) {
		std::cerr << e.what() << '\n';
		return 1;
	} catch (...) {
		std::cerr << "Unknown error type\n";
		return 1;
	}
}
