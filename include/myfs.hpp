#pragma once
#ifndef MYFS_H
#define MYFS_H

#include "blkdev.hpp"
#include "EntryInfo.hpp"
#include "config.hpp"
#include "allocator.hpp"

#define HEADER_SIZE (sizeof(uint8_t) + sizeof(size_t))
#define ENTRY_BUFFER_SIZE (HEADER_SIZE + MAX_PATH_LENGTH + sizeof(size_t) + sizeof(size_t))

class MyFs {
  public:
	explicit MyFs(BlockDeviceSimulator* blkdevsim_);
	~MyFs();

	void format();
	void save();
	void load();

	std::optional<EntryInfo> getEntryInfo(const std::string& fileName);

	void addTableEntry(EntryInfo& entryToAdd);
	void removeTableEntry(EntryInfo& entryToRemove);
	void reallocateTableEntry(EntryInfo& entryToUpdate, size_t newSize);

	bool isFileExists(const std::string& filepath);
	EntryInfo createFile(const std::string& filepath);
	void remove(const std::string& filepath);
	void move(const std::string& srcfilepath, const std::string& dstfilepath);
	void copy(const std::string& srcfilepath, const std::string& dstfilepath);

	EntryInfo createDirectory(const std::string& filepath);
	void addFileToDirectory(const std::string& filepath);
	void removeFileFromDirectory(const std::string& directoryPath, const std::string& filename);
	void writeDirectoryEntries(const EntryInfo& directoryEntry, const std::vector<std::string>& directoryEntries);
	std::vector<std::string> readDirectoryEntries(const EntryInfo& directoryEntry);
	void addFileToDirectory(const std::string& directoryPath, const std::string& filename);

	std::string getContent(const std::string& filepath);
	std::string getContent(const EntryInfo& entry);
	void setContent(const std::string& filepath, const std::string& content);
	void setContent(EntryInfo entry, const std::string& content);

	std::vector<EntryInfo> listDir(const std::string& currentDir);
	std::vector<EntryInfo> listTree();

	static std::pair<std::string, std::string> splitPath(const std::string& filepath);
	static std::string addCurrentDir(const std::string& filename, const std::string& currentDir);

  private:
	struct myfs_header {
		std::array<char, 4> magic;
		uint8_t version;
		uint16_t blockSize;
		size_t totalFatSize;
	};

	std::set<EntryInfo> entries;
	BlockDeviceSimulator* blkdevsim;
	AddressAllocator allocator;
	size_t totalFatSize;
	uint16_t BLOCK_SIZE;
};

#endif