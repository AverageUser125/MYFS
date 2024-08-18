#pragma once

#include <string>
#include <cstring>

enum EntryTypes {
	FILE_TYPE = 1,
	DIRECTORY_TYPE
};

using EntryInfo = struct EntryInfo {
	std::string path = "/";
	size_t size = 0;
	size_t address = 0;
	EntryTypes type = EntryTypes::FILE_TYPE;

	bool operator<(const EntryInfo& other) const {
		return path < other.path;
	}

	// Serialization
	void serialize(char* buffer) const {
		memcpy(buffer, &type, sizeof(type));
		size_t pathLength = path.length();
		memcpy(buffer + sizeof(type), &pathLength, sizeof(pathLength));
		memcpy(buffer + sizeof(type) + sizeof(pathLength), path.c_str(), pathLength);
		memcpy(buffer + sizeof(type) + sizeof(pathLength) + pathLength, &size, sizeof(size));
		memcpy(buffer + sizeof(type) + sizeof(pathLength) + pathLength + sizeof(size), &address, sizeof(address));
	}

	// Deserialization
	void deserialize(const char* buffer) {
		memcpy(&type, buffer, sizeof(type));
		size_t pathLength = 0;
		memcpy(&pathLength, buffer + sizeof(type), sizeof(pathLength));
		path = std::string(buffer + sizeof(type) + sizeof(pathLength), pathLength);
		memcpy(&size, buffer + sizeof(type) + sizeof(pathLength) + pathLength, sizeof(size));
		memcpy(&address, buffer + sizeof(type) + sizeof(pathLength) + pathLength + sizeof(size), sizeof(address));
	}

	// Get the size needed for serialization
	[[nodiscard]] size_t serializedSize() const {
		return sizeof(type) + sizeof(size_t) + path.length() + sizeof(size) + sizeof(address);
	}
};
