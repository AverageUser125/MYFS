#pragma once
#ifndef MYFS_H
#define MYFS_H

#include "blkdev.hpp"
#include "EntryInfo.hpp"
#include "config.hpp"
#include "allocator.hpp"
#include <set>
#include <optional>
#include <numeric>
#include <functional>
#include <sstream>
#include <array>
#include <iostream>

constexpr int HEADER_SIZE = (sizeof(uint8_t) + sizeof(size_t));
constexpr int ENTRY_BUFFER_SIZE = (HEADER_SIZE + MAX_PATH + sizeof(size_t) + sizeof(size_t));


enum Errors {
	OK,
	fileNotFound,
	directoryNotFound,
	fileAlreadyExists,
	fileCantExist,
	fatPartitionFull,
	maxDirectoryCapacity,
	invalidType,
	invalidPath,
	infiniteCall,
	fileSystemClosed,
	invalidArguments,
	invalidHeaderMagic,
	invalidHeaderBlockSize,
	invalidHeaderVersion,
	maxPathLengthReached,
	rootIsContant,
};

class MyFs {
  public:
	explicit MyFs(BlockDeviceSimulator* blkdevsim_);
	~MyFs();

	[[nodiscard]] Errors format();
	[[nodiscard]] Errors save();
	[[nodiscard]] Errors load();

	std::optional<EntryInfo> getEntryInfo(const std::string& fileName);


	bool isFileExists(const std::string& filepath);

	[[nodiscard]] Errors MyFs::createFile(const std::string& filepath, EntryInfo* result = nullptr);
	[[nodiscard]] Errors MyFs::createDirectory(const std::string& filepath, EntryInfo* result = nullptr);

	[[nodiscard]] Errors remove(const std::string& filepath);
	[[nodiscard]] Errors MyFs::move(const std::string& srcfilepath, const std::string& dstfilepath);
	[[nodiscard]] Errors copy(const std::string& srcfilepath, const std::string& dstfilepath);

	[[nodiscard]] Errors MyFs::readDirectoryEntries(const EntryInfo& directoryEntry,
													std::vector<std::string>& directoryEntries);

	[[nodiscard]] Errors MyFs::getContent(const std::string& filepath, std::string& input);
	[[nodiscard]] Errors MyFs::getContent(const EntryInfo& filepath, std::string& input);
	[[nodiscard]] Errors setContent(const std::string& filepath, const std::string& content);
	[[nodiscard]] Errors setContent(EntryInfo entry, const std::string& content);

	[[nodiscard]] Errors MyFs::listDir(const std::string& currentDir, std::vector<EntryInfo>& result);
	std::vector<EntryInfo> listTree();

	static std::pair<std::string, std::string> splitPath(const std::string& filepath);
	static std::string addCurrentDir(const std::string& filename, const std::string& currentDir);
	static const char* MyFs::errToString(Errors error);

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

	[[nodiscard]] Errors removeFileFromDirectory(const std::string& directoryPath, const std::string& filename);
	[[nodiscard]] Errors writeDirectoryEntries(const EntryInfo& directoryEntry,
											   const std::vector<std::string>& directoryEntries);
	[[nodiscard]] Errors addFileToDirectory(const std::string& directoryPath, const std::string& filename);

	[[nodiscard]] Errors addTableEntry(EntryInfo& entryToAdd);
	[[nodiscard]] Errors removeTableEntry(EntryInfo& entryToRemove);
	[[nodiscard]] Errors reallocateTableEntry(EntryInfo& entryToUpdate, size_t newSize);
	};
#endif