#pragma once

#include "EntryInfo.hpp"
#include "blkdev.hpp"
#include "config.hpp"
#include <set>
#include <map>
#include <vector>
#include <algorithm>
#include <functional>

// Define State Types
struct UnInitialized;
struct Initialized;

class AddressAllocatorBase {
protected:
    AddressAllocatorBase(size_t firstAddress_, size_t lastAddress_);

    size_t firstAddress;
    size_t lastAddress;
};

template <typename State>
class AddressAllocator;

// Specialization for UnInitialized
template <>
class AddressAllocator<UnInitialized> : public AddressAllocatorBase {
public:
    AddressAllocator(size_t firstAddress_, size_t lastAddress_);

    [[nodiscard]] AddressAllocator<Initialized> initialize(const std::set<EntryInfo>& entries, uint16_t blockSize) const;

private:
    static void initializeImpl(AddressAllocator<Initialized>& allocator, const std::set<EntryInfo>& entries, uint16_t blockSize) ;
};

// Specialization for Initialized
template <>
class AddressAllocator<Initialized> : public AddressAllocatorBase {
public:
    AddressAllocator(size_t firstAddress_, size_t lastAddress_, uint16_t blockSize);

    size_t allocate(size_t requestedSize);
    void deallocate(const EntryInfo& entry);
    void reallocate(EntryInfo& entry, size_t newSize);
    void defrag(std::set<EntryInfo>& entries, BlockDeviceSimulator* blkdevsim);

private:
    uint16_t BLOCK_SIZE;
    std::map<size_t, size_t> freeSpaces;

    [[nodiscard]] size_t alignToBlockSize(const size_t size) const;
    void mergeFreeSpaces(size_t startAddress, size_t size);
    void initializeFreeSpaces(const std::set<EntryInfo>& entries);

    // Allow AddressAllocator<UnInitialized> to access private methods
    friend class AddressAllocator<UnInitialized>;
};

class AddressAllocatorFactory {
public:
    static AddressAllocator<Initialized> create(size_t firstAddress, size_t lastAddress,
                                                const std::set<EntryInfo>& entries, uint16_t blockSize);
};