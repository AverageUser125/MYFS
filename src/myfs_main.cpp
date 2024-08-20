
#include "myfs_main.hpp"
#include "editor.hpp"


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
		if (part.empty() && !inQuotes) {
			// Skip empty parts if not in quotes
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

Errors handleCommand(const std::string& command, std::vector<std::string>& args, MyFs& myfs, std::string& currentDir) {

	CommandType commandType = getCommandType(command);

	switch (commandType) {
	case CommandType::LIST: {
		std::vector<EntryInfo> dlist;
		if (args.empty()) {
			dlist = myfs.listTree();
		} else {
			std::cout << RED << TREE_CMD << ": zero arguments requested" RESET << std::endl;
			return invalidArguments;
		}
		printEntries(dlist);

		break;
	}
	// Swaped with LIST cause it gave me anurism
	case CommandType::TREE: {
		std::vector<EntryInfo> dlist;
		Errors err = OK;
		if (args.empty()) {
			err = myfs.listDir(currentDir, dlist);
		} else {
			err = myfs.listDir(args[0], dlist);
		}
		if (err != OK)
			return err;
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
			return invalidArguments;
		}
		return myfs.createFile(args[0]);
		break;
	case CommandType::CONTENT: {
		if (args.size() != 1) {
			return invalidArguments;
		}
		std::optional<EntryInfo> entryOpt = myfs.getEntryInfo(args[0]);
		if (!entryOpt) {
			return fileNotFound;
		}
		EntryInfo entry = *entryOpt;
		std::string content; 
		Errors err = myfs.getContent(entry, content);
		if (err != OK)
			return OK;
		if (!content.empty() && content.back() != '\n') {
			content += '\n';
		}
		std::cout << content;
		break;
	}
	case CommandType::DELETE: {
		if (args.size() != 1) {
			return invalidArguments;
		}
		Errors err = myfs.remove(args[0]);
		if (err != OK)
			return err;
		break;
	}
	case CommandType::EDIT: {
		if (args.size() > 1) {
			return invalidArguments;
		}
		if (args.size() == 1) {
			editorStart(myfs, args[0].c_str());
		} else {
			editorStart(myfs, "");
		}
		break;
	}
	case CommandType::CREATE_DIR: {
		if (args.size() != 1) {
			return invalidArguments;
		}
		Errors err = myfs.createDirectory(args[0]);
		if (err != OK)
			return err;
		break;
	}
	case CommandType::CD: {
		if (args.size() != 1) {
			return invalidArguments;
		}
		std::string newDir = args[0];
		std::optional<EntryInfo> entryOpt = myfs.getEntryInfo(newDir);
		if (!entryOpt) {
			return fileNotFound;
		}
		EntryInfo entry = entryOpt.value();
		if (entry.type != DIRECTORY_TYPE) {
			return invalidType;
		}

		currentDir = entry.path;
		break;
	}
	case CommandType::MOVE: {
		if (args.size() != 2) {
			return invalidArguments;
		}
		Errors err = myfs.move(args[0], args[1]);
		if (err != OK)
			return err;
		break;
	}
	case CommandType::COPY: {
		if (args.size() != 2) {
			return invalidArguments;
		}
		Errors err =myfs.copy(args[0], args[1]);
		if (err != OK)
			return err;
		break;
	}
	case CommandType::EXIT:
		return fileSystemClosed;
	case CommandType::UNKNOWN:
	default:
		std::cout << RED "Unknown command: " << command << RESET << std::endl;
		break;
	}
	return OK;
}

int main(int argc, char** argv) {
	std::string bldevfile;
	if (argc == 1) {
		std::cout << CYAN "Please enter the file name: " RESET;
		std::cin >> bldevfile;
		bldevfile = RESOURCES_PATH + bldevfile + ".bin";
		// Flush stdin to clear any leftover input
		std::cin.clear();
		std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
	} else if (argc == 2) {
		bldevfile = argv[1];
	} else {
		std::cerr << "Too many arguments" << std::endl;
		return EXIT_FAILURE;
	}

	std::string currentDir = "/";
	// may fail, if can't create file, or file is read-only
	BlockDeviceSimulator blkdevptr;
	if (!blkdevptr.init(bldevfile)) {
		std::cerr << "failed to virtually open file\n";
		return EXIT_FAILURE;
	}
	MyFs myfs(&blkdevptr);

	// Print the welcome message
	std::cout << GREEN << MENU_ASCII_ART << RESET << std::endl;
	std::cout << "To get help, please type 'help' on the prompt below.\r\n" << std::endl;


	Errors err = OK;
	while (err != fileSystemClosed) {
		std:: cout << (BOLDGREEN FS_NAME RESET ":" BOLDBLUE + currentDir + RESET "$ ");

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
		err = handleCommand(command, args, myfs, currentDir);
		if (err != OK && err != fileSystemClosed) {
			std::cout << RED << "An error occurred: " << MyFs::errToString(err) << RESET << std::endl;
		}
		if (err == invalidArguments) {
			std::cout << "use \"help\" to get the correct arguements\n";
		}
	}
	return EXIT_SUCCESS;
}
