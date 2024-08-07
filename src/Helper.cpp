#include "Helper.hpp"

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

std::pair<std::string, std::string> splitPath(const std::string& filepath) {
	size_t pos = filepath.find_last_of('/');
	if (pos == std::string::npos) {
		// No directory separator found, the whole path is the filename
		return {"", filepath};
	}

	if (pos == 0) {
		// The directory is the root "/"
		return {"/", filepath.substr(1)};
	}

	std::string directory = filepath.substr(0, pos);
	std::string filename = filepath.substr(pos + 1);
	return {directory, filename};
}

// ensureEnclosedInSlashes
std::string ensureStartsWithSlash(const std::string& str) {
	std::string result = str;

	// Check if the string starts with '/'
	if (result.empty() || result[0] != '/') {
		result.insert(result.begin(), '/');
	}

	return result;
}

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

std::string addCurrentDir(const std::string& filename, const std::string& currentDir) {
	if (filename.empty() || filename[0] == '/') {
		return filename;
	}
	if (currentDir.back() == '/') {
		if (filename[0] == '/') {
			return currentDir + filename.substr(1);
		}
		return currentDir + filename;
	}
	if (filename[0] == '/') {
		return currentDir + filename;
	}
	return currentDir + "/" + filename;
}