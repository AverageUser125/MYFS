#pragma once

#include "EntryInfo.hpp"
#include "blkdev.hpp"
#include "config.hpp"
#include <set>
#include <map>
#include <vector>
#include <algorithm>
#include <functional>

class AddressAllocator {
  public:
	AddressAllocator(size_t firstAddress_, size_t lastAddress_);

	void initialize(const std::set<EntryInfo>& entries, const uint16_t BLOCK_SIZE_);

	size_t allocate(size_t requestedSize);
	void deallocate(const EntryInfo& entry);
	void reallocate(EntryInfo& entry, size_t newSize);


	void defrag(std::set<EntryInfo>& entries, BlockDeviceSimulator* blkdevsim);

  private:
	// shared memory with file system

	size_t firstAddress;
	size_t lastAddress;
	uint16_t BLOCK_SIZE;
	std::map<size_t, size_t> freeSpaces; // key: starting address, value: size

	[[nodiscard]] size_t alignToBlockSize(const size_t size) const;
	void mergeFreeSpaces(size_t startAddress, size_t size);
};
