#include "myfs.h"
#include "Helper.h"

// const std::string MyFs::MYFS_MAGIC = "MYFS";
//const uint8_t MyFs::CURR_VERSION = 0x03;

MyFs::MyFs(BlockDeviceSimulator* blkdevsim_)
	: blkdevsim(blkdevsim_), allocator(FAT_SIZE, blkdevsim->DEVICE_SIZE), totalFatSize(FAT_SIZE),
	  BLOCK_SIZE(DEFAULT_BLOCK_SIZE) {
	try {
		load();
		allocator.initialize(entries, BLOCK_SIZE);
		allocator.defrag(entries, blkdevsim);
		// allocator.defrag(entries, blkdevsim);
	} catch (const std::exception& e) {
		std::cerr << "Error loading file system: " << e.what() << std::endl;
		std::cout << CYAN "Creating new file system instance..." << std::endl;
		allocator.initialize(entries, BLOCK_SIZE);
		format();
		std::cout << GREEN "Finished!" RESET << std::endl;
	}
}

MyFs::~MyFs() {
	try {
		save(); // Ensure all changes are flushed to the block device
	} catch (std::runtime_error& e) {
		std::cout << e.what() << std::endl;
	}
}

#pragma region fatIO

void MyFs::save() {
	// Make sure we have the correct size
	size_t totalSize = 0;
	for (const EntryInfo& entry : entries) {
		totalSize += entry.serializedSize();
	}
	if (totalFatSize != totalSize) {
		std::cout << "fat size:" << totalFatSize << " entires size:" << totalSize << std::endl;
		totalFatSize = totalSize;
	}
	//std::accumulate(entries.begin(), entries.end(), static_cast<size_t>(0),
	//					   [](size_t totalSize, const EntryInfo& entry) {
	//						   return totalSize + entry.serializedSize();
	//					   }) == totalFatSize);
	assert(FAT_SIZE > BLOCK_SIZE);

	if (totalFatSize > static_cast<size_t>(FAT_SIZE - BLOCK_SIZE)) {
		throw std::overflow_error("FAT partition full");
	}
	// Allocate a buffer to hold all serialized entries
	std::vector<char> buffer(totalFatSize);
	size_t offset = 0;
	for (const EntryInfo& entry : entries) {
		entry.serialize(buffer.data() + offset);
		offset += entry.serializedSize();
	}

	// Write the header and entries to the block device
	myfs_header header{};
	std::memcpy(header.magic.data(), MYFS_MAGIC, header.magic.size());
	strncpy(header.magic.data(), MYFS_MAGIC, header.magic.size());
	header.version = CURR_VERSION;
	header.blockSize = BLOCK_SIZE;
	blkdevsim->write(0, sizeof(header), reinterpret_cast<const char*>(&header));
	blkdevsim->write(sizeof(header), sizeof(totalFatSize), reinterpret_cast<const char*>(&totalFatSize));

	blkdevsim->write(sizeof(header) + sizeof(totalFatSize), buffer.size(), buffer.data());
}

void MyFs::load() {
	// Read the header
	myfs_header header{};
	blkdevsim->read(0, sizeof(header), reinterpret_cast<char*>(&header));

	// Check for magic number and version
	if (strncmp(header.magic.data(), MYFS_MAGIC, header.magic.size()) != 0) {
		throw std::runtime_error("Invalid file system magic number.");
	}
	if (header.version != CURR_VERSION) {
		throw std::runtime_error("Unsupported file system version.");
	}
	if (header.blockSize <= 1) {
		throw std::runtime_error("Invalid block size");
	}
	BLOCK_SIZE = header.blockSize;
	blkdevsim->read(sizeof(header), sizeof(totalFatSize), reinterpret_cast<char*>(&totalFatSize));

	// Read the entries
	size_t offset = sizeof(header) + sizeof(totalFatSize);

	while (offset < totalFatSize) {
		// Buffer for the entry
		std::array<char, ENTRY_BUFFER_SIZE> buffer{};
		// Read the type and path length first
		blkdevsim->read(offset, HEADER_SIZE, buffer.data());
		// Check if buffer is just zeros
		if (std::all_of(buffer.begin(), buffer.begin() + HEADER_SIZE, [](char c) { return c == 0; })) {
			// If the buffer contains only zeros, it is the end of the entries
			break;
		}
		// Deserialize entry to get the path length
		EntryInfo entry;
		entry.deserialize(buffer.data());
		// Validate path length
		if (entry.path.size() > MAX_PATH_LENGTH) {
			throw std::runtime_error("Path exceeds maximum length.");
		}
		// Calculate the full entry size
		size_t entrySize = entry.serializedSize();
		std::vector<char> entryBuffer(entrySize);
		// Read the full entry into the buffer
		blkdevsim->read(offset, entrySize, entryBuffer.data());
		// Deserialize the entry
		entry.deserialize(entryBuffer.data());
		// Insert entry into the set
		entries.insert(entry);
		// Update the offset for the next entry
		offset += entrySize;
	}
}

void MyFs::format() {
	myfs_header header{};
	strncpy(header.magic.data(), MYFS_MAGIC, header.magic.size());
	header.version = CURR_VERSION;
	header.blockSize = DEFAULT_BLOCK_SIZE;
	totalFatSize = 0;
	blkdevsim->write(0, sizeof(header), reinterpret_cast<const char*>(&header));
	blkdevsim->write(sizeof(header), sizeof(totalFatSize), reinterpret_cast<const char*>(&totalFatSize));

	size_t remainingSize = blkdevsim->DEVICE_SIZE - sizeof(header) - sizeof(totalFatSize);
	std::vector<char> clearBuffer(remainingSize, 0); // Create a buffer filled with zeros
	blkdevsim->write(sizeof(header) + sizeof(totalFatSize), remainingSize, clearBuffer.data());

	try {
		EntryInfo newEntry;
		newEntry.path = "/";
		newEntry.type = DIRECTORY_TYPE;
		newEntry.size = 0;
		newEntry.address = -1;
		// Add the entry to the file system
		addTableEntry(newEntry);

	} catch (const std::runtime_error& e) {
		std::cerr << "Error creating root directory: " << e.what() << std::endl;
	}
}

#pragma endregion

#pragma region entryManagment

void MyFs::setContent(const std::string& filepath, const std::string& content) {
	std::optional<EntryInfo> entryOpt = getEntryInfo(filepath);
	if (!entryOpt) {
		throw std::runtime_error("File not found: " + filepath);
	}

	EntryInfo entry = *entryOpt;
	size_t newSize = content.size();

	reallocateTableEntry(entry, newSize);
	blkdevsim->write(entry.address, newSize, content.data());

	save();
}

void MyFs::setContent(EntryInfo entry, const std::string& content) {
	size_t newSize = content.size();

	reallocateTableEntry(entry, newSize);
	blkdevsim->write(entry.address, newSize, content.data());

	save();
}

std::string MyFs::getContent(const EntryInfo& entry) {
	std::string content(entry.size, '\0');
	blkdevsim->read(entry.address, entry.size, content.data());
	return content;
}

std::string MyFs::getContent(const std::string& filepath) {
	std::optional<EntryInfo> entryOpt = getEntryInfo(filepath);
	if (!entryOpt) {
		throw std::runtime_error("File not found");
	}

	const EntryInfo entry = *entryOpt;
	std::string content(entry.size, '\0');
	blkdevsim->read(entry.address, entry.size, content.data());
	return content;
}

std::optional<EntryInfo> MyFs::getEntryInfo(const std::string& fileName) {
	auto it =
		std::find_if(entries.begin(), entries.end(), [&](const EntryInfo& entry) { return entry.path == fileName; });

	if (it != entries.end()) {
		return *it;
	}
	return std::nullopt;
}

void MyFs::addTableEntry(EntryInfo& entryToAdd) {
	// Insert the entry into the set
	if (totalFatSize >= FAT_SIZE - 1) {
		entries.erase(entryToAdd);
		std::cout << totalFatSize << std::endl;
		throw std::overflow_error("FAT table full");
	}
	totalFatSize += entryToAdd.serializedSize();

	entryToAdd.address = allocator.allocate(entryToAdd.size);
	assert(entryToAdd.address >= FAT_SIZE && entryToAdd.address < blkdevsim->DEVICE_SIZE);
	entries.insert(entryToAdd);
	save();
}

void MyFs::removeTableEntry(EntryInfo& entryToRemove) {
	totalFatSize -= entryToRemove.serializedSize();
	assert(totalFatSize >= 1);

	allocator.deallocate(entryToRemove);
	entries.erase(entryToRemove);

	save();
}

void MyFs::reallocateTableEntry(EntryInfo& entryToUpdate, size_t newSize) {
	entries.erase(entryToUpdate);
	allocator.reallocate(entryToUpdate, newSize);
	//assert(entryToUpdate.address + entryToUpdate.size == allocator.nextAvailableAddress);

	assert(entryToUpdate.address >= FAT_SIZE && entryToUpdate.address < blkdevsim->DEVICE_SIZE);
	assert(entryToUpdate.size == newSize);

	entries.insert(entryToUpdate);

	save();
}

#pragma endregion

#pragma region fileIO

bool MyFs::isFileExists(const std::string& filepath) {
	return getEntryInfo(filepath).has_value();
}

EntryInfo MyFs::createFile(const std::string& filepath) {
	if (filepath.empty()) {
		throw std::runtime_error("invalid file path:" + filepath);
	}
	if (isFileExists(filepath)) {
		throw std::runtime_error("File already exists");
	}
	// Create the file entry
	EntryInfo newEntry;
	newEntry.path = ensureStartsWithSlash(filepath);
	newEntry.type = FILE_TYPE;
	newEntry.size = 0;
	newEntry.address = -1;

	std::pair<std::string, std::string> pathAndName = splitPath(filepath);
	// Add the entry to the file system
	addFileToDirectory(pathAndName.first, pathAndName.second);
	addTableEntry(newEntry);
	return newEntry;
}

#pragma endregion
#pragma region directoryIO

EntryInfo MyFs::createDirectory(const std::string& filepath) {
	if (isFileExists(filepath)) {
		throw std::runtime_error("Directory already exists");
	}

	// Create the file entry
	EntryInfo newEntry;
	newEntry.path = ensureStartsWithSlash(filepath);
	newEntry.type = DIRECTORY_TYPE;
	newEntry.size = 0;
	newEntry.address = -1;

	std::pair<std::string, std::string> pathAndName = splitPath(filepath);
	addFileToDirectory(pathAndName.first, pathAndName.second);

	// Add the entry to the file system
	addTableEntry(newEntry);

	save();
	return newEntry;
}

std::vector<std::string> MyFs::readDirectoryEntries(const EntryInfo& directoryEntry) {
	std::vector<std::string> directoryEntries;

	// Ensure the directoryEntry type is correct (e.g., directory type)
	if (directoryEntry.type != DIRECTORY_TYPE) {
		throw std::runtime_error("Invalid entry type for directory");
	}

	// Read the directory content from the file system
	std::string content = getContent(directoryEntry.path);

	// Assuming entries are separated by new lines or some delimiter
	std::istringstream stream(content);
	std::string entry;
	while (std::getline(stream, entry)) {
		directoryEntries.push_back(entry);
	}

	return directoryEntries;
}

void MyFs::writeDirectoryEntries(const EntryInfo& directoryEntry, const std::vector<std::string>& directoryEntries) {
	if (directoryEntries.size() > MAX_DIRECTORY_SIZE) {
		throw std::runtime_error("maxium amount of files in a directory exceeded");
	}
	if (directoryEntries.empty()) {
		setContent(directoryEntry, "");
	}
	// Convert directory entries to a single string with appropriate delimiter
	std::ostringstream oss;
	for (const std::string& entryName : directoryEntries) {
		oss << entryName << '\n'; // Assuming newline as delimiter
	}
	std::string content = oss.str();
	// remove trailing whitespace
	auto end = std::find_if_not(content.rbegin(), content.rend(), ::isspace).base();
	// Erase characters from the end to the found position
	content.erase(end, content.end());
	// Write the content to the file system
	setContent(directoryEntry, content);
}

void MyFs::addFileToDirectory(const std::string& directoryPath, const std::string& filename) {
	std::optional<EntryInfo> directoryEntryOpt = getEntryInfo(ensureStartsWithSlash(directoryPath));
	if (!directoryEntryOpt || directoryEntryOpt->type != DIRECTORY_TYPE) {
		throw std::runtime_error("Invalid directory1: " + directoryPath);
	}

	const EntryInfo& directoryEntry = *directoryEntryOpt;

	if (filename == "/" || filename == " " || filename.empty() || filename == "\n" || filename == "\r" ||
		filename == "\t" || filename == "." || filename == "..") [[unlikely]] {
		return;
	}
	// Read directory entries
	std::vector<std::string> directoryEntries = readDirectoryEntries(directoryEntry);

	// Modify directory entries
	auto it = std::find(directoryEntries.begin(), directoryEntries.end(), filename);
	if (it != directoryEntries.end()) {
		throw std::runtime_error("File already exists in the directory: " + filename);
	}
	directoryEntries.push_back(filename);

	// Write updated directory entries back to disk
	writeDirectoryEntries(directoryEntry, directoryEntries);
}

void MyFs::removeFileFromDirectory(const std::string& directoryPath, const std::string& filename) {
	std::optional<EntryInfo> directoryEntryOpt = getEntryInfo(directoryPath);
	if (!directoryEntryOpt || directoryEntryOpt->type != DIRECTORY_TYPE) {
		std::cout << "Directory not found: " << directoryPath << std::endl;
		return;
		// throw std::runtime_error("Invalid directory2: " + directoryPath);
	}

	const EntryInfo& directoryEntry = *directoryEntryOpt;

	// Read directory entries
	std::vector<std::string> directoryEntries = readDirectoryEntries(directoryEntry);

	// Modify directory entries
	auto it = std::find(directoryEntries.begin(), directoryEntries.end(), filename);
	if (it == directoryEntries.end()) {
		if (!filename.empty()) {
			std::cout << "File not found in the directory: " << filename << std::endl;
		}
		return;
		//throw std::runtime_error("File not found in the directory: " + filename);
	}
	directoryEntries.erase(it);

	// Write updated directory entries back to disk
	writeDirectoryEntries(directoryEntry, directoryEntries);
}

#pragma endregion
#pragma region generalUtils

std::vector<EntryInfo> MyFs::listDir(const std::string& currentDir) {
	if (currentDir.empty()) {
		return {}; // List root directory if no path is provided
	}
	// use readDirectoryEntries
	std::optional<EntryInfo> directoryEntryOpt = getEntryInfo(currentDir);
	if (!directoryEntryOpt || directoryEntryOpt->type != DIRECTORY_TYPE) {
		throw std::runtime_error("Invalid path: " + currentDir);
	}

	const EntryInfo& directoryEntry = *directoryEntryOpt;
	std::vector<std::string> directoryEntries = readDirectoryEntries(directoryEntry);

	std::vector<EntryInfo> result;
	for (const std::string& filename : directoryEntries) {
		std::optional<EntryInfo> entryOpt = getEntryInfo(addCurrentDir(filename, currentDir));
		if (entryOpt) {
			result.push_back(*entryOpt);
		}
	}
	return result;
}

std::vector<EntryInfo> MyFs::listTree() {
	std::vector<EntryInfo> result;
	result.reserve(entries.size());
	for (const EntryInfo& entry : entries) {
		result.push_back(entry);
	}
	return result;
}

void MyFs::remove(const std::string& filepath) {
	std::optional<EntryInfo> entryOpt = getEntryInfo(filepath);
	if (!entryOpt) {
		throw std::runtime_error("Invalid file: " + filepath);
	}
	EntryInfo entry = *entryOpt;

	std::pair<std::string, std::string> pathAndName = splitPath(filepath);

	if (entry.type == FILE_TYPE) {
		removeTableEntry(entry);

	} else if (entry.type == DIRECTORY_TYPE) {
		std::vector<std::string> directoryEntries = readDirectoryEntries(entry);
		for (const std::string& filename : directoryEntries) {
			try {
				remove(addCurrentDir(filename, filepath));
			} catch (std::runtime_error& e) {
				// incase
			}
		}
		if (filepath != "/") [[unlikely]] {
			removeTableEntry(entry);
		}
	}
	removeFileFromDirectory(pathAndName.first, pathAndName.second);
}

void MyFs::move(const std::string& srcfilepath, const std::string& dstfilepath) {
	std::optional<EntryInfo> entryOpt = getEntryInfo(srcfilepath);
	if (!entryOpt) {
		throw std::runtime_error("Invalid file: " + srcfilepath);
	}
	EntryInfo entry = *entryOpt;
	if (isFileExists(dstfilepath)) {
		throw std::runtime_error("file " + dstfilepath + " already exists");
	}
	if (dstfilepath == "/" || srcfilepath == "/") [[unlikely]] {
		throw std::runtime_error("root ain't moving, so stop it");
	}
	if (dstfilepath.find(srcfilepath) == 0 && dstfilepath[srcfilepath.length()] == '/') {
		throw std::runtime_error("recursive move detected");
	}
	std::pair<std::string, std::string> dstPathAndName = splitPath(dstfilepath);
	std::pair<std::string, std::string> srcPathAndName = splitPath(srcfilepath);

	if (entry.type == DIRECTORY_TYPE) {
		std::vector<std::string> directoryEntries = readDirectoryEntries(entry);
		for (const std::string& filename : directoryEntries) {
			try {
				move(addCurrentDir(filename, srcfilepath), addCurrentDir(filename, dstfilepath));
			} catch (const std::runtime_error& e) {
				// Handle exception if needed
			}
		}
	}

	removeFileFromDirectory(srcPathAndName.first, srcPathAndName.second);
	entries.erase(entry);
	totalFatSize -= entry.path.size();
	entry.path = dstfilepath;
	totalFatSize += entry.path.size();
	entries.insert(entry);
	addFileToDirectory(dstPathAndName.first, dstPathAndName.second);
}

void MyFs::copy(const std::string& srcfilepath, const std::string& dstfilepath) {
	std::optional<EntryInfo> entryOpt = getEntryInfo(srcfilepath);
	if (!entryOpt) {
		throw std::runtime_error("Invalid file: " + srcfilepath);
	}
	EntryInfo entry = *entryOpt;
	if (isFileExists(dstfilepath)) {
		throw std::runtime_error("file " + dstfilepath + " already exists");
	}
	// Check if destPath starts with srcPath and is immediately followed by a slash or nothing
	if (srcfilepath == "/") [[unlikely]] {

		throw std::runtime_error("copying root will always cause recursive copy.");
	}
	if (dstfilepath.find(srcfilepath) == 0 && dstfilepath[srcfilepath.length()] == '/') {
		throw std::runtime_error("recursive copy detected");
	}
	std::pair<std::string, std::string> dstPathAndName = splitPath(dstfilepath);

	if (entry.type == FILE_TYPE) {
		EntryInfo dstEntry = createFile(dstfilepath); // Create the new file at dstfilepath and get its EntryInfo
		std::string content = getContent(entry);
		setContent(dstEntry, content);

	} else if (entry.type == DIRECTORY_TYPE) {
		createDirectory(dstfilepath); // Create the new directory at dstfilepath

		std::vector<std::string> directoryEntries = readDirectoryEntries(entry);
		for (const std::string& filename : directoryEntries) {
			try {
				copy(addCurrentDir(filename, srcfilepath), addCurrentDir(filename, dstfilepath));
			} catch (const std::runtime_error& e) {
				// Handle exception if needed
			}
		}
	}
}

#pragma endregion