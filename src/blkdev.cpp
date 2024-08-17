#include "blkdev.hpp"
#include "config.hpp"

BlockDeviceSimulator::BlockDeviceSimulator(const std::string& fname) : fd(INVALID_HANDLE_VALUE), filemap(nullptr) {
	// Check if the file exists
	fd = CreateFileA(fname.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL,
					 nullptr);

	if (fd == INVALID_HANDLE_VALUE) {
		throw std::system_error(GetLastError(), std::system_category(), "Failed to open or create file");
	}

	// If the file was newly created, set its size
	if (GetLastError() == ERROR_ALREADY_EXISTS) {
		LARGE_INTEGER size{};
		size.QuadPart = DEVICE_SIZE;
		if (SetFilePointerEx(fd, size, nullptr, FILE_BEGIN) == 0 || SetEndOfFile(fd) == 0) {
			CloseHandle(fd);
			throw std::system_error(GetLastError(), std::system_category(), "Failed to set file size");
		}
	}

	HANDLE hFileMapping = CreateFileMappingA(fd, nullptr, PAGE_READWRITE, 0, DEVICE_SIZE, nullptr);

	if (hFileMapping == nullptr) {
		CloseHandle(fd);
		throw std::system_error(GetLastError(), std::system_category(), "Failed to create file mapping");
	}

	filemap =
		static_cast<unsigned char*>(MapViewOfFile(hFileMapping, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, DEVICE_SIZE));

	if (filemap == nullptr) {
		CloseHandle(hFileMapping);
		CloseHandle(fd);
		throw std::system_error(GetLastError(), std::system_category(), "Failed to map view of file");
	}

	CloseHandle(hFileMapping); // We can close the mapping handle after mapping the view
}

BlockDeviceSimulator::~BlockDeviceSimulator() {
	if (filemap) {
		UnmapViewOfFile(filemap);
	}
	if (fd != INVALID_HANDLE_VALUE) {
		CloseHandle(fd);
	}
}

void BlockDeviceSimulator::read(size_t addr, size_t size, char* ans) const {
	memcpy(ans, filemap + addr, size);
}

void BlockDeviceSimulator::write(size_t addr, size_t size, const char* data) {
	memcpy(filemap + addr, data, size);
}