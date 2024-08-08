#include "allocator.hpp"
#include "EntryInfo.hpp"
#include "config.hpp"

AddressAllocatorBase::AddressAllocatorBase(size_t firstAddress_, size_t lastAddress_)
    : firstAddress(firstAddress_), lastAddress(lastAddress_) {}

AddressAllocator<UnInitialized>::AddressAllocator(size_t firstAddress_, size_t lastAddress_)
    : AddressAllocatorBase(firstAddress_, lastAddress_) {}

AddressAllocator<Initialized> AddressAllocator<UnInitialized>::initialize(const std::set<EntryInfo>& entries, uint16_t blockSize) const {
    AddressAllocator<Initialized> initializedAllocator(firstAddress, lastAddress, blockSize);
    initializeImpl(initializedAllocator, entries, blockSize);
    return initializedAllocator;
}

 void AddressAllocator<UnInitialized>::initializeImpl(AddressAllocator<Initialized>& allocator, const std::set<EntryInfo>& entries, uint16_t blockSize)  {
	allocator.BLOCK_SIZE = blockSize;
    allocator.initializeFreeSpaces(entries);
}

AddressAllocator<Initialized>::AddressAllocator(size_t firstAddress_, size_t lastAddress_, uint16_t blockSize)
    : AddressAllocatorBase(firstAddress_, lastAddress_), BLOCK_SIZE(blockSize) {}

void AddressAllocator<Initialized>::initializeFreeSpaces(const std::set<EntryInfo>& entries) {
    freeSpaces.clear();

    if (entries.empty()) {
        freeSpaces.emplace(firstAddress, lastAddress - firstAddress);
        return;
    }

    size_t currentAddress = firstAddress;

    for (const EntryInfo& entry : entries) {
        if (entry.address > currentAddress) {
            freeSpaces.emplace(currentAddress, entry.address - currentAddress);
        }
        currentAddress = entry.address + alignToBlockSize(entry.size);
    }

    if (currentAddress < lastAddress) {
        freeSpaces.emplace(currentAddress, lastAddress - currentAddress);
    }
}

size_t AddressAllocator<Initialized>::allocate(size_t requestedSize) {
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

	throw std::overflow_error("Insufficient space to allocate");
}

void AddressAllocator<Initialized>::deallocate(const EntryInfo& entry) {
	//if (entry.size == 0)
	//	return; // No need to deallocate zero-sized entries

	size_t startAddress = entry.address;
	size_t size = alignToBlockSize(entry.size);

	// Insert the freed block into the free spaces map
	freeSpaces.emplace(startAddress, size);
	// Merge adjacent free spaces
	mergeFreeSpaces(startAddress, size);
}

void AddressAllocator<Initialized>::reallocate(EntryInfo& entry, size_t newSize) {
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

inline size_t AddressAllocator<Initialized>::alignToBlockSize(const size_t size) const {
	if (size == 0) {
		return BLOCK_SIZE;
	}
	return ((size + BLOCK_SIZE - 1) / BLOCK_SIZE) * BLOCK_SIZE;
}

void AddressAllocator<Initialized>::mergeFreeSpaces(size_t startAddress, size_t size) {
	// merges ONLY adjacent free spaces
	// can be remove and it will work, just be less memory efficient
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

void AddressAllocator<Initialized>::defrag(std::set<EntryInfo>& entries, BlockDeviceSimulator* blkdevsim) {
	// assert(nullptr == "defrag not working and corrupting data");
	if (entries.empty()) {
		return; // Nothing to defrag
	}
	// Step 1: Collect all allocated entries
	std::vector<EntryInfo> allEntries(entries.begin(), entries.end());
	entries.clear(); // Clear existing entries

	// Step 2: Sort entries by their current address
	std::sort(allEntries.begin(), allEntries.end(),
			  [](const EntryInfo& a, const EntryInfo& b) { return a.address < b.address; });

	// Step 3: Make sure root is at the beginning
	auto it = std::find_if(allEntries.begin(), allEntries.end(), [](const EntryInfo& a) { return a.path == "/"; });
	assert(it != allEntries.end());
	EntryInfo rootEntry = *it; // root obviously must exist
	allEntries.erase(it);

	std::vector<char> buffer;
	buffer.resize(rootEntry.size);
	blkdevsim->read(rootEntry.address, rootEntry.size, buffer.data());
	rootEntry.address = firstAddress;
	blkdevsim->write(rootEntry.address, rootEntry.size, buffer.data());
	entries.insert(rootEntry);

	// Step 4: Reallocate entries to their new positions
	size_t nextAvailableAddress = firstAddress + alignToBlockSize(rootEntry.size);
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
