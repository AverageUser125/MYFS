#include "myfs.hpp"
#include "config.hpp"
#include <array>

// const std::string MyFs::MYFS_MAGIC = "MYFS";
//const uint8_t MyFs::CURR_VERSION = 0x03;

MyFs::MyFs(BlockDeviceSimulator* blkdevsim_)
	: blkdevsim(blkdevsim_), allocator(FAT_SIZE, blkdevsim->DEVICE_SIZE, DEFAULT_BLOCK_SIZE), totalFatSize(FAT_SIZE),
	  BLOCK_SIZE(DEFAULT_BLOCK_SIZE) {

	Errors err = load();
	if (err == OK) {
		allocator.initialize(entries, BLOCK_SIZE);
		allocator.defrag(entries, blkdevsim);
		return;
	}
		
	(void)format();
}

MyFs::~MyFs() {
	// if fails nothing can do
	(void)save(); // Ensure all changes are flushed to the block device
}

#pragma region fatIO

Errors MyFs::save() {
	// Make sure we have the correct size
	// assert(std::accumulate(entries.begin(), entries.end(), static_cast<size_t>(0),
	//					   [](size_t totalSize, const EntryInfo& entry) {
	//						   return totalSize + entry.serializedSize();
	//					   }) == totalFatSize);

	if (totalFatSize > static_cast<size_t>(FAT_SIZE - BLOCK_SIZE)) {
		return fatPartitionFull;
	}
	// Allocate a buffer to hold all serialized entries
	std::vector<char> buffer(totalFatSize);
	size_t offset = 0;
	for (const EntryInfo& entry : entries) {
		entry.serialize(buffer.data() + offset);
		offset += entry.serializedSize();
	}

	// Write the header and entries to the block device
	myfs_header header(BLOCK_SIZE);

	blkdevsim->write(0, sizeof(header), reinterpret_cast<const char*>(&header));
	blkdevsim->write(sizeof(header), sizeof(totalFatSize), reinterpret_cast<const char*>(&totalFatSize));
	blkdevsim->write(sizeof(header) + sizeof(totalFatSize), buffer.size(), buffer.data());

	return OK;
}

Errors MyFs::load() {
	// Read the header
	myfs_header header(BLOCK_SIZE);
	blkdevsim->read(0, sizeof(header), reinterpret_cast<char*>(&header));

	// Check for magic number and version
	if (strncmp(header.magic.data(), MYFS_MAGIC, header.magic.size()) != 0) {
		return invalidHeaderMagic;
	}
	if (header.version != CURR_VERSION) {
		return invalidHeaderVersion;
	}
	if (header.blockSize <= 1 && header.blockSize < FAT_SIZE) {
		return invalidHeaderBlockSize;
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
		assert(std::all_of(buffer.begin(), buffer.begin() + HEADER_SIZE, [](char c) { return c == 0; }) == false);
		// Deserialize entry to get the path length
		EntryInfo entry;
		entry.deserialize(buffer.data());
		// Validate path length
		if (entry.path.size() > MAX_PATH) {
			return maxPathLengthReached;
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
	return OK;
}

Errors MyFs::format() {
	myfs_header header(DEFAULT_BLOCK_SIZE);
	totalFatSize = 0;
	blkdevsim->write(0, sizeof(header), reinterpret_cast<const char*>(&header));
	blkdevsim->write(sizeof(header), sizeof(totalFatSize), reinterpret_cast<const char*>(&totalFatSize));

	size_t remainingSize = blkdevsim->DEVICE_SIZE - sizeof(header) - sizeof(totalFatSize);
	std::vector<char> clearBuffer(remainingSize, 0); // Create a buffer filled with zeros
	blkdevsim->write(sizeof(header) + sizeof(totalFatSize), remainingSize, clearBuffer.data());

	EntryInfo newEntry;
	newEntry.path = "/";
	newEntry.type = DIRECTORY_TYPE;
	newEntry.size = 0;
	newEntry.address = -1;
	// Add the entry to the file system
	return addTableEntry(newEntry);
}

#pragma endregion

#pragma region entryManagment

Errors MyFs::setContent(const std::string& filepath, const std::string& content) {
	std::optional<EntryInfo> entryOpt = getEntryInfo(filepath);
	if (!entryOpt) {
		return fileNotFound;
	}

	EntryInfo entry = *entryOpt;
	size_t newSize = content.size();

	Errors err = reallocateTableEntry(entry, newSize);
	if (err != OK)
		return err;
	blkdevsim->write(entry.address, newSize, content.data());

	return save();
}

Errors MyFs::setContent(EntryInfo entry, const std::string& content) {
	size_t newSize = content.size();

	Errors err = reallocateTableEntry(entry, newSize);
	if (err != OK)
		return err;
	blkdevsim->write(entry.address, newSize, content.data());

	return save();
}

Errors MyFs::getContent(const EntryInfo& entry, std::string& input) {
	std::string content(entry.size, '\0');
	input.resize(entry.size);
	blkdevsim->read(entry.address, entry.size, input.data());
	return OK;
}

Errors MyFs::getContent(const std::string& filepath, std::string& input) {
	std::optional<EntryInfo> entryOpt = getEntryInfo(filepath);
	if (!entryOpt) {
		return fileNotFound;
	}

	const EntryInfo entry = *entryOpt;
	input.resize(entry.size);
	blkdevsim->read(entry.address, entry.size, input.data());
	return OK;
}

std::optional<EntryInfo> MyFs::getEntryInfo(const std::string& filename) {
	auto it =
		std::find_if(entries.begin(), entries.end(), [&](const EntryInfo& entry) { return entry.path == filename; });

	if (it != entries.end()) {
		return *it;
	}
	return std::nullopt;
}

Errors MyFs::addTableEntry(EntryInfo& entryToAdd) {
	// Insert the entry into the set
	if (totalFatSize >= FAT_SIZE - 1) {
		return fatPartitionFull;
	}
	totalFatSize += entryToAdd.serializedSize();

	entryToAdd.address = allocator.allocate(entryToAdd.size);
	assert(entryToAdd.address >= FAT_SIZE && entryToAdd.address < blkdevsim->DEVICE_SIZE);
	entries.insert(entryToAdd);
	return save();
}

Errors MyFs::removeTableEntry(EntryInfo& entryToRemove) {
	totalFatSize -= entryToRemove.serializedSize();
	assert(totalFatSize >= 1);

	allocator.deallocate(entryToRemove);
	entries.erase(entryToRemove);

	return save();
}

Errors MyFs::reallocateTableEntry(EntryInfo& entryToUpdate, size_t newSize) {
	entries.erase(entryToUpdate);
	allocator.reallocate(entryToUpdate, newSize);
	//assert(entryToUpdate.address + entryToUpdate.size == allocator.nextAvailableAddress);

	assert(entryToUpdate.address >= FAT_SIZE && entryToUpdate.address < blkdevsim->DEVICE_SIZE);
	assert(entryToUpdate.size == newSize);

	entries.insert(entryToUpdate);

	return save();
}

#pragma endregion

#pragma region fileIO

bool MyFs::isFileExists(const std::string& filepath) {
	return getEntryInfo(filepath).has_value();
}

Errors MyFs::createFile(const std::string& filepath, EntryInfo* result) {
	if (!isFileExists(MyFs::splitPath(filepath).first)) {
		//return fileCantExist;
	}
	if (filepath.empty()) {
		return invalidPath;
	}
	if (isFileExists(filepath)) {
		return fileAlreadyExists;
	}
	// Create the file entry
	EntryInfo newEntry;
	newEntry.path = filepath;
	newEntry.type = FILE_TYPE;
	newEntry.size = 0;
	newEntry.address = -1;

	std::pair<std::string, std::string> pathAndName = splitPath(filepath);

	// Add the entry to the file system
	Errors err = addFileToDirectory(pathAndName.first, pathAndName.second);
	if(err != OK) {
		return err;
	}
	err = addTableEntry(newEntry);
	if (err != OK)
		return err;
	if (result != nullptr) {
		*result = std::move(newEntry);
	}
	return OK;
}

#pragma endregion
#pragma region directoryIO

Errors MyFs::createDirectory(const std::string& filepath, EntryInfo* result) {
	if (!isFileExists(MyFs::splitPath(filepath).first)) {
		return fileCantExist;
	}
	if (filepath.empty()) {
		return invalidPath;
	}
	if (isFileExists(filepath)) {
		return fileAlreadyExists;
	}

	// Create the file entry
	EntryInfo newEntry;
	newEntry.path = filepath;
	newEntry.type = DIRECTORY_TYPE;
	newEntry.size = 0;
	newEntry.address = -1;

	// Add the entry to the file system
	std::pair<std::string, std::string> pathAndName = splitPath(filepath);
	Errors err = addFileToDirectory(pathAndName.first, pathAndName.second);
	if (err != OK) {
		return err;
	}
	err = addTableEntry(newEntry);
	if (err != OK)
		return err;

	if (result != nullptr) {
		result = &std::move(newEntry);
	}

	return save();
}

Errors MyFs::readDirectoryEntries(const EntryInfo& directoryEntry, std::vector<std::string>& directoryEntries) {
	assert(directoryEntries.empty());
	// Ensure the directoryEntry type is correct (e.g., directory type)
	if (directoryEntry.type != DIRECTORY_TYPE) {
		return invalidType;
	}

	// Read the directory content from the file system
	std::string content;
	Errors err = getContent(directoryEntry.path, content);
	if (err != OK)
		return OK;
	// Assuming entries are separated by new lines or some delimiter
	std::istringstream stream(content);
	std::string entry;
	while (std::getline(stream, entry)) {
		directoryEntries.push_back(entry);
	}
	return OK;
}

Errors MyFs::writeDirectoryEntries(const EntryInfo& directoryEntry, const std::vector<std::string>& directoryEntries) {
	if (directoryEntries.size() > MAX_DIRECTORY_SIZE) {
		return maxDirectoryCapacity;
	}
	Errors err;
	if (directoryEntries.empty()) {
		err = setContent(directoryEntry, "");
		if (err != OK)
			return err;
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
	return setContent(directoryEntry, content);
}

Errors MyFs::addFileToDirectory(const std::string& directoryPath, const std::string& filename) {
	std::optional<EntryInfo> directoryEntryOpt = getEntryInfo(directoryPath);
	if (!directoryEntryOpt) {
		return fileNotFound;
	}
	const EntryInfo& directoryEntry = *directoryEntryOpt;
	if (directoryEntry.type != DIRECTORY_TYPE) {
		return invalidType;
	}

	if ((filename.size() == 1 && (isspace(filename[0]) || filename[0] == '.')) || filename.empty() ||
		filename == "..") {
		return invalidPath;
	}
	// Read directory entries
	std::vector<std::string> directoryEntries;
	Errors err = readDirectoryEntries(directoryEntry, directoryEntries);
	if (err != OK)
		return err;
	// Modify directory entries
	auto it = std::find(directoryEntries.begin(), directoryEntries.end(), filename);
	if (it != directoryEntries.end()) {
		return fileAlreadyExists;
	}
	directoryEntries.push_back(filename);

	// Write updated directory entries back to disk
	return writeDirectoryEntries(directoryEntry, directoryEntries);
}

Errors MyFs::removeFileFromDirectory(const std::string& directoryPath, const std::string& filename) {
	std::optional<EntryInfo> directoryEntryOpt = getEntryInfo(directoryPath);
	if (!directoryEntryOpt){
		return fileNotFound;
	}
	if (directoryEntryOpt->type != DIRECTORY_TYPE) {
		return invalidType;
	}
	if (filename == "/" || (directoryPath == "/" && filename.empty())) {
		return rootIsContant;
	}
	const EntryInfo& directoryEntry = *directoryEntryOpt;

	// Read directory entries
	std::vector<std::string> directoryEntries;
	Errors err = readDirectoryEntries(directoryEntry, directoryEntries);
	if (err != OK)
		return err;
	// Modify directory entries
	auto it = std::find(directoryEntries.begin(), directoryEntries.end(), filename);
	if (it == directoryEntries.end()) {
		return fileNotFound;
	}
	directoryEntries.erase(it);

	// Write updated directory entries back to disk
	return writeDirectoryEntries(directoryEntry, directoryEntries);
}

#pragma endregion
#pragma region generalUtils

Errors MyFs::listDir(const std::string& currentDir, std::vector<EntryInfo>& result) {
	if (currentDir.empty()) {
		return {}; // List root directory if no path is provided
	}
	// use readDirectoryEntries
	std::optional<EntryInfo> directoryEntryOpt = getEntryInfo(currentDir);
	if (!directoryEntryOpt) { 
		return fileNotFound;
	}
	if (directoryEntryOpt->type != DIRECTORY_TYPE){
		return invalidType;
	}

	const EntryInfo& directoryEntry = *directoryEntryOpt;
	std::vector<std::string> directoryEntries;
	Errors err = readDirectoryEntries(directoryEntry, directoryEntries);
	if (err != OK)
		return err;
	for (const std::string& filename : directoryEntries) {
		std::optional<EntryInfo> entryOpt = getEntryInfo(addCurrentDir(filename, currentDir));
		if (entryOpt) {
			result.push_back(*entryOpt);
		}
	}
	return OK;
}

std::vector<EntryInfo> MyFs::listTree() {
	std::vector<EntryInfo> result;
	result.reserve(entries.size());
	for (const EntryInfo& entry : entries) {
		result.push_back(entry);
	}
	return result;
}

Errors MyFs::remove(const std::string& filepath) {
	std::optional<EntryInfo> entryOpt = getEntryInfo(filepath);
	if (!entryOpt) {
		return fileNotFound;
	}
	EntryInfo entry = *entryOpt;

	std::pair<std::string, std::string> pathAndName = splitPath(filepath);
	Errors err = OK;

	if (entry.type == FILE_TYPE) {
		err = removeTableEntry(entry);
		if (err != OK)
			return err;
	} else if (entry.type == DIRECTORY_TYPE) {
		std::vector<std::string> directoryEntries;
		err = readDirectoryEntries(entry, directoryEntries);
		if (err != OK)
			return err;
		for (const std::string& filename : directoryEntries) {
			Errors err = remove(addCurrentDir(filename, filepath));
			if (err != OK)
				return err;
		}
		if (filepath != "/") {
			err = removeTableEntry(entry);
			if (err != OK)
				return err;
		}
	}
	err = removeFileFromDirectory(pathAndName.first, pathAndName.second);
	// TODO: consider if this is really good idea since it makes resetting easy
	if (err == rootIsContant) {
		return OK;
	}
	return err;
}

Errors MyFs::move(const std::string& srcfilepath, const std::string& dstfilepath) {
	std::optional<EntryInfo> entryOpt = getEntryInfo(srcfilepath);
	if (!entryOpt) {
		return fileNotFound;
	}
	EntryInfo entry = *entryOpt;
	if (isFileExists(dstfilepath)) {
		return fileAlreadyExists;
	}
	if (dstfilepath == "/" || srcfilepath == "/") {
		return rootIsContant;
	}
	if (dstfilepath.find(srcfilepath) == 0 && dstfilepath[srcfilepath.length()] == '/') {
		return infiniteCall;
	}
	std::pair<std::string, std::string> dstPathAndName = splitPath(dstfilepath);
	std::pair<std::string, std::string> srcPathAndName = splitPath(srcfilepath);

	if (entry.type == DIRECTORY_TYPE) {
		std::vector<std::string> directoryEntries;
		Errors err = readDirectoryEntries(entry, directoryEntries);
		if (err != OK)
			return err;
		for (const std::string& filename : directoryEntries) {
			err = move(addCurrentDir(filename, srcfilepath), addCurrentDir(filename, dstfilepath));
			if (err != OK) {
				return err;
			}
		}
	}
	Errors err = removeFileFromDirectory(srcPathAndName.first, srcPathAndName.second);
	if (err != OK)
		return err;
	entries.erase(entry);
	totalFatSize -= entry.path.size();
	entry.path = dstfilepath;
	totalFatSize += entry.path.size();
	entries.insert(entry);
	return addFileToDirectory(dstPathAndName.first, dstPathAndName.second);
}

Errors MyFs::copy(const std::string& srcfilepath, const std::string& dstfilepath) {
	std::optional<EntryInfo> entryOpt = getEntryInfo(srcfilepath);
	if (!entryOpt) {
		return fileNotFound;
	}
	EntryInfo entry = *entryOpt;
	if (isFileExists(dstfilepath)) {
		return fileAlreadyExists;
	}
	// Check if destPath starts with srcPath and is immediately followed by a slash or nothing
	if (srcfilepath == "/" || dstfilepath.find(srcfilepath) == 0 && dstfilepath[srcfilepath.length()] == '/') {
		return infiniteCall;
	}
	std::pair<std::string, std::string> dstPathAndName = splitPath(dstfilepath);

	if (entry.type == FILE_TYPE) {
		EntryInfo dstEntry;
		Errors err = createFile(dstfilepath, &dstEntry); // Create the new file at dstfilepath and get its EntryInfo
		if (err != OK)
			return err;

		std::string content;
		err = getContent(entry, content);
		if (err != OK)
			return err;
		err = setContent(dstEntry, content);
		if (err != OK)
			return err;

	} else if (entry.type == DIRECTORY_TYPE) {
		Errors err = createDirectory(dstfilepath, nullptr); // Create the new directory at dstfilepath
		if (err != OK)
			return err;
		std::vector<std::string> directoryEntries;
		err = readDirectoryEntries(entry, directoryEntries);
		if (err != OK)
			return err;
		for (const std::string& filename : directoryEntries) {

			Errors err = copy(addCurrentDir(filename, srcfilepath), addCurrentDir(filename, dstfilepath));
			if (err != OK)
				return err;
		}
	}
	return OK;
}

std::pair<std::string, std::string> MyFs::splitPath(const std::string& filepath) {
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

std::string MyFs::addCurrentDir(const std::string& filename, const std::string& currentDir) {
	std::string currDir = currentDir;
	if (currDir.empty()) {
		currDir = "/";
	}
	if (filename.empty() || filename[0] == '/') {
		return filename;
	}
	if (currDir.back() == '/') {
		if (filename[0] == '/') {
			return currDir + filename.substr(1);
		}
		return currDir + filename;
	}
	if (filename[0] == '/') {
		return currDir + filename;
	}
	return currDir + "/" + filename;
}

const char* MyFs::MyFs::errToString(Errors error) {
	switch (error) {
	case OK:
		return "OK";
	case fileNotFound:
		return "File does not exist";
	case directoryNotFound:
		return "Directory does not exist";
	case fileAlreadyExists:
		return "File already exists";
	case fileCantExist:
		return "File cannot exist";
	case fatPartitionFull:
		return "FAT partition is full";
	case maxDirectoryCapacity:
		return "Maximum directory capacity reached";
	case invalidType:
		return "Invalid type for operation";
	case invalidPath:
		return "Invalid path";
	case infiniteCall:
		return "Infinite call detected";
	case fileSystemClosed:
		return "File system is closed";
	case invalidArguments:
		return "Invalid arguments";
	case invalidHeaderMagic:
		return "Invalid header magic";
	case invalidHeaderBlockSize:
		return "Invalid header block size";
	case invalidHeaderVersion:
		return "Invalid header version";
	case maxPathLengthReached:
		return "Maximum path length reached";
	case rootIsContant:
		return "Root cannot be changed";
	default:
		return "Unknown error";
	}
}

#pragma endregion