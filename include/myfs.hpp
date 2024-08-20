#pragma once
#ifndef MYFS_H
#define MYFS_H

#include "blkdev.hpp"
#include "EntryInfo.hpp"
#include "config.hpp"
#include "allocator.hpp"
#include <stdexcept>
#include <set>
#include <optional>
#include <numeric>
#include <functional>
#include <sstream>
#include <array>

constexpr int HEADER_SIZE = (sizeof(uint8_t) + sizeof(size_t));
constexpr int ENTRY_BUFFER_SIZE = (HEADER_SIZE + MAX_PATH + sizeof(size_t) + sizeof(size_t));

struct Errors {
	enum {
	fileNotExists,
	fielCantExist,
	fatPartitionFull,
	maxDirectoryCapacity,
	invalidType,
	};
};

class MyFs {
  public:
	explicit MyFs(BlockDeviceSimulator* blkdevsim_);
	~MyFs();

	void format();
	void save();
	void load();

	std::optional<EntryInfo> getEntryInfo(const std::string& fileName);


	bool isFileExists(const std::string& filepath);
	EntryInfo createFile(const std::string& filepath);
	EntryInfo createDirectory(const std::string& filepath);

	void remove(const std::string& filepath);
	void move(const std::string& srcfilepath, const std::string& dstfilepath);
	void copy(const std::string& srcfilepath, const std::string& dstfilepath);

	std::vector<std::string> readDirectoryEntries(const EntryInfo& directoryEntry);

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

		// Constructor to initialize the struct
		myfs_header(uint16_t blockSizeValue)
			: magic{MYFS_MAGIC[0], MYFS_MAGIC[1], MYFS_MAGIC[2], MYFS_MAGIC[3]}, version(CURR_VERSION),
			  blockSize(blockSizeValue), totalFatSize(FAT_SIZE) {
		}
	};

	std::set<EntryInfo> entries;
	BlockDeviceSimulator* blkdevsim;
	AddressAllocator allocator;
	size_t totalFatSize;
	uint16_t BLOCK_SIZE;

	void removeFileFromDirectory(const std::string& directoryPath, const std::string& filename);
	void writeDirectoryEntries(const EntryInfo& directoryEntry, const std::vector<std::string>& directoryEntries);
	void addFileToDirectory(const std::string& directoryPath, const std::string& filename);

	void addTableEntry(EntryInfo & entryToAdd);
	void removeTableEntry(EntryInfo & entryToRemove);
	void reallocateTableEntry(EntryInfo & entryToUpdate, size_t newSize);
	};
#endif