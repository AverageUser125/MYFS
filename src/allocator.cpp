#include "allocator.hpp"
#include "Helper.hpp"

AddressAllocator::AddressAllocator(size_t firstAddress_, size_t lastAddress_)
	: firstAddress(firstAddress_), lastAddress(lastAddress_), BLOCK_SIZE(DEFAULT_BLOCK_SIZE) {
	assert(lastAddress > firstAddress + BLOCK_SIZE);
}

void AddressAllocator::initialize(const std::set<EntryInfo>& entries, const uint16_t BLOCK_SIZE_) {
	BLOCK_SIZE = BLOCK_SIZE_;
	freeSpaces.clear();

	if (entries.empty()) {
		// If entries are empty, all space is free
		freeSpaces.emplace(firstAddress, lastAddress - firstAddress);
		return;
	}
	// Find free spaces between existing entries
	size_t currentAddress = firstAddress;

	for (const EntryInfo& entry : entries) {
		if (entry.address > currentAddress) {
			// There is a gap between the current address and the start of this entry
			freeSpaces.emplace(currentAddress, entry.address - currentAddress);
		}
		// Move current address to the end of this entry
		currentAddress = entry.address + alignToBlockSize(entry.size);
	}

	// Check for free space after the last entry
	if (currentAddress < lastAddress) {
		// emplace is just insert, but it constructs a key-value pair automatically
		// so I don't have to write std::pair every time
		freeSpaces.emplace(currentAddress, lastAddress - currentAddress);
	}
}

size_t AddressAllocator::allocate(size_t requestedSize) {
	requestedSize = alignToBlockSize(requestedSize);

	// Find a suitable free space block
	for (auto it = freeSpaces.begin(); it != freeSpaces.end(); ++it) {
		if (it->second >= requestedSize) {
			size_t allocatedAddress = it->first;
			size_t remainingSize = it->second - requestedSize;
			// Remove the free block from the map
			freeSpaces.erase(it);
			// If there is remaining space, add it back as a new free block
			if (remainingSize > 0) {
				freeSpaces.emplace(allocatedAddress + requestedSize, remainingSize);
			}
			// Return the allocated address
			return alignToBlockSize(allocatedAddress);
		}
	}

	for (const auto& entry : freeSpaces) {
		std::cout << entry.first << " " << entry.second << std::endl;
	}
	throw std::overflow_error("Insufficient space to allocate");
}

void AddressAllocator::deallocate(const EntryInfo& entry) {
	//if (entry.size == 0)
	//	return; // No need to deallocate zero-sized entries

	size_t startAddress = entry.address;
	size_t size = alignToBlockSize(entry.size);

	// Insert the freed block into the free spaces map
	freeSpaces.emplace(startAddress, size);
	// Merge adjacent free spaces
	mergeFreeSpaces(startAddress, size);
}

void AddressAllocator::reallocate(EntryInfo& entry, size_t newSize) {
	size_t oldSize = alignToBlockSize(entry.size);
	size_t newAlignedSize = alignToBlockSize(newSize);
	size_t oldBlockCount = (oldSize + BLOCK_SIZE - 1) / BLOCK_SIZE;
	size_t newBlockCount = (newAlignedSize + BLOCK_SIZE - 1) / BLOCK_SIZE;

	// special case
	if (oldSize == 0 && newAlignedSize == BLOCK_SIZE) {
		entry.size = newSize;
		return;
	}

	if (newBlockCount <= oldBlockCount) {
		// If the new size is smaller or equal within the same block, no reallocation is needed
		entry.size = newSize;
		return;
	}

	// Check if the block can be expanded in place
	auto it = freeSpaces.find(entry.address + oldSize);
	if (it != freeSpaces.end() && it->second >= newAlignedSize - oldSize) {
		// If there is enough contiguous free space, expand the block in place
		size_t remainingSize = it->second - (newAlignedSize - oldSize);
		// Remove the free block
		freeSpaces.erase(it);
		// If there is remaining space, add it back as a new free block
		if (remainingSize > 0) {
			freeSpaces.emplace(entry.address + newAlignedSize, remainingSize);
		}
		// Update the entry size
		entry.size = newSize;
		return;
	}

	// Otherwise, allocate a new block and deallocate the old one
	deallocate(entry);
	size_t newAddress = allocate(newSize);
	entry.address = newAddress;
	entry.size = newSize;
}

inline size_t AddressAllocator::alignToBlockSize(const size_t size) const {
	if (size == 0) {
		return BLOCK_SIZE;
	}
	return ((size + BLOCK_SIZE - 1) / BLOCK_SIZE) * BLOCK_SIZE;
}

void AddressAllocator::mergeFreeSpaces(size_t startAddress, size_t size) {
	// merges ONLY adjacent free spaces
	auto it = freeSpaces.find(startAddress);
	size_t prevAddress = std::prev(it)->first;
	size_t nextAddress = std::next(it)->first;

	if (nextAddress != freeSpaces.rbegin()->first && startAddress + freeSpaces[startAddress] == nextAddress) {
		size += freeSpaces[nextAddress];
		freeSpaces.erase(nextAddress);
	}

	if (prevAddress != freeSpaces.begin()->first && prevAddress + freeSpaces[prevAddress] == startAddress) {
		size += freeSpaces[prevAddress];
		freeSpaces.erase(startAddress);
		startAddress = prevAddress;
	}

	freeSpaces[startAddress] = size;
}

void AddressAllocator::defrag(std::set<EntryInfo>& entries, BlockDeviceSimulator* blkdevsim) {
	// assert(nullptr == "defrag not working and corrupting data");
	if (entries.empty()) {
		return; // Nothing to defrag
	}
	// Step 1: Collect all allocated entries
	std::vector<EntryInfo> allEntries(entries.begin(), entries.end());

	// Step 2: Sort entries by their current address
	std::sort(allEntries.begin(), allEntries.end(),
			  [](const EntryInfo& a, const EntryInfo& b) { return a.address < b.address; });

	std::vector<char> buffer;
	// Step 4: Reallocate entries to their new positions
	entries.clear(); // Clear existing entries

	size_t nextAvailableAddress = firstAddress;
	for (EntryInfo& entry : allEntries) {
		size_t alignedSize = alignToBlockSize(entry.size);
		buffer.resize(alignedSize);

		// Read data from the old location
		blkdevsim->read(entry.address, alignedSize, buffer.data());

		// Update entry's address
		entry.address = nextAvailableAddress;

		// Write data to the new location
		blkdevsim->write(entry.address, alignedSize, buffer.data());

		nextAvailableAddress += alignedSize;
		// Insert the updated entry into the set
		entries.insert(entry);
	}

	// Step 5: Set everything after all the entries to a free space
	freeSpaces.clear();

	// The first free space is from the end of the last entry to the last address
	freeSpaces.emplace(nextAvailableAddress, lastAddress - nextAvailableAddress);
}
