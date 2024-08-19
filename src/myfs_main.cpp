
#include "myfs_main.hpp"

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

	std::string token;
	while (std::getline(stream, token, '/')) {
		pathTokens.push_back(token);
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
        << std::setw(COLUMN_SPACING) << std::left << MAGENTA EXIT_CMD << std::setw(0) << YELLOW "Gracefully exit.\r\n" RESET;

}

// clang-format on

std::vector<std::string> splitCmd(const std::string& cmd) {
	std::vector<std::string> ans;
	std::stringstream ss(cmd);
	std::string part;
	bool inQuotes = false;
	std::string current;

	while (std::getline(ss, part, ' ')) {
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
	static const std::map<std::string, CommandType> commandMap = {{LIST_CMD, CommandType::LIST},
																  {EXIT_CMD, CommandType::EXIT},
																  {HELP_CMD, CommandType::HELP},
																  {CREATE_FILE_CMD, CommandType::CREATE_FILE},
																  {CONTENT_CMD, CommandType::CONTENT},
																  {DELETE_CMD, CommandType::DELETE},
																  {TREE_CMD, CommandType::TREE},
																  {EDIT_CMD, CommandType::EDIT},
																  {CREATE_DIR_CMD, CommandType::CREATE_DIR},
																  {CD_CMD, CommandType::CD},
																  {MOVE_CMD, CommandType::MOVE},
																  {COPY_CMD, CommandType::COPY}};

	auto it = commandMap.find(cmd);
	return (it != commandMap.end()) ? it->second : CommandType::UNKNOWN;
}

void editFile(MyFs& myfs, const std::string& fileLocation) {	
	std::optional<EntryInfo> entryOpt = myfs.getEntryInfo(fileLocation);
	if (entryOpt) {
		EntryInfo entry = *entryOpt;
		if (entry.type != FILE_TYPE) {
			// throw std::runtime_error("Can only edit files");
		}
	}

	try {
		editorStart(myfs, fileLocation);
	} catch (std::runtime_error& e) {
	}
}

void printEntries(const std::vector<EntryInfo>& entries) {
	// Find the maximum width for address and size
	size_t maxAddressWidth = 0;
	size_t maxSizeWidth = 0;

	for (const EntryInfo& entry : entries) {
		maxAddressWidth = std::max(maxAddressWidth, std::to_string(entry.address).length());
		maxSizeWidth = std::max(maxSizeWidth, std::to_string(entry.size).length());
	}

	// Print the entries with dynamic width
	// clang-format off
	for (const EntryInfo& entry : entries) {
		std::cout << std::setw(maxAddressWidth) << std::right 
		<< entry.address << " " << std::setw(maxSizeWidth) << std::right 
		<< entry.size << " " << (entry.type == DIRECTORY_TYPE ? BOLDBLUE : RESET) 
		<< entry.path << RESET "\r\n";
	}
	// clang-format on
}

bool handleCommand(const std::string& command, std::vector<std::string>& args, MyFs& myfs, std::string& currentDir) {

	CommandType commandType = getCommandType(command);

	switch (commandType) {
	case CommandType::LIST: {
		std::vector<EntryInfo> dlist;
		if (args.empty()) {
			dlist = myfs.listTree();
		} else {
			std::cout << RED << TREE_CMD << ": zero arguments requested" RESET << std::endl;
			return false;
		}
		printEntries(dlist);

		break;
	}
	// Swaped with LIST cause it gave me anurism
	case CommandType::TREE: {
		std::vector<EntryInfo> dlist;
		if (args.empty()) {
			dlist = myfs.listDir(currentDir);
		} else {
			dlist = myfs.listDir(args[0]);
		}
		for (EntryInfo& entry : dlist) {
			entry.path = MyFs::splitPath(entry.path).second;
		}
		printEntries(dlist);

		break;
	}
	case CommandType::HELP:
		printHelpMessage();
		break;
	case CommandType::CREATE_FILE:
		if (args.size() != 1) {
			throw std::runtime_error(CREATE_FILE_CMD " needs arguments");
		}
		myfs.createFile(args[0]);
		break;
	case CommandType::CONTENT: {
		if (args.size() != 1) {
			throw std::runtime_error(CONTENT_CMD " needs arguments");
		}
		std::optional<EntryInfo> entryOpt = myfs.getEntryInfo(args[0]);
		if (!entryOpt) {
			throw std::runtime_error("File doesn't exist");
		}
		EntryInfo entry = *entryOpt;
		if (entry.type != FILE_TYPE) {
			// throw std::runtime_error("Can only get content of files");
		}
		std::string content = myfs.getContent(entry);
		if (!content.empty() && content.back() != '\n') {
			content += '\n';
		}
		std::cout << content;
		break;
	}
	case CommandType::DELETE: {
		if (args.size() != 1) {
			throw std::runtime_error(DELETE_CMD " needs arguments");
		}
		myfs.remove(args[0]);
		break;
	}
	case CommandType::EDIT: {
		if (args.size() > 1) {
			throw std::runtime_error(EDIT_CMD " needs arguments");
		}
		if (args.size() == 1) {
			editFile(myfs, args[0]);
		} else {
			editFile(myfs, "");
		}
		break;
	}
	case CommandType::CREATE_DIR: {
		if (args.size() != 1) {
			throw std::runtime_error(CREATE_DIR_CMD " needs arguments");
		}
		myfs.createDirectory(args[0]);
		break;
	}
	case CommandType::CD: {
		if (args.size() != 1) {
			throw std::runtime_error(CD_CMD " needs arguments");
		}
		std::string newDir = args[0];
		std::optional<EntryInfo> entryOpt = myfs.getEntryInfo(newDir);
		if (!entryOpt) {
			std::cout << RED << "No such file or directory: " << args[0] << RESET << std::endl;
			break;
		}
		EntryInfo entry = entryOpt.value();
		if (entry.type != DIRECTORY_TYPE) {
			std::cout << RED << "Cannot change directory to a file: " << args[0] << RESET << std::endl;
			break;
		}

		currentDir = entry.path;
		break;
	}
	case CommandType::MOVE: {
		if (args.size() != 2) {
			throw std::runtime_error(MOVE_CMD " requires 2 arguments");
		}
		myfs.move(args[0], args[1]);
		break;
	}
	case CommandType::COPY: {
		if (args.size() != 2) {
			throw std::runtime_error(COPY_CMD " requires 2 arguments");
		}
		myfs.copy(args[0], args[1]);
		break;
	}
	case CommandType::EXIT:
		return true;
	case CommandType::UNKNOWN:
	default:
		std::cout << RED "Unknown command: " << command << RESET << std::endl;
		break;
	}
	return false;
}

int main(int argc, char** argv) {
	std::string bldevfile;
	if (argc == 1) {
		std::cout << CYAN "Please enter the file name: " RESET;
		std::cin >> bldevfile;
		// Flush stdin to clear any leftover input
		std::cin.clear();
		std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
	} else if (argc == 2) {
		bldevfile = argv[1];
	} else {
		std::cerr << "Too many arguments" << std::endl;
		return -1;
	}

	std::string currentDir = "/";
	// may fail, if can't create file, or file is read-only
	BlockDeviceSimulator blkdevptr(bldevfile);
	MyFs myfs(&blkdevptr);

	// Print the welcome message
	std::cout << GREEN << MENU_ASCII_ART << RESET << std::endl;
	std::cout << "To get help, please type 'help' on the prompt below.\r\n" << std::endl;

	shellPrompt shell;

	bool exit = false;
	while (!exit) {
		shell.setPrompt(BOLDGREEN FS_NAME RESET ":" BOLDBLUE + currentDir + RESET "$ ");

		std::string cmdline = shell.readInput();
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
			std::cout << RED << "An error occurred: " << e.what() << RESET << std::endl;
		}
	}
	return 0;
}
