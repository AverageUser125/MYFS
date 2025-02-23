#include "blkdev.h"

#include <string>
#include <system_error>
#include <cstring>

#ifdef _WIN32

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

class WindowsCategory : public std::error_category {
  public:
	const char* name() const noexcept override {
		return "windows";
	}

	std::string message(int errorCode) const override {
		char* msgBuffer = nullptr;
		size_t size =
			FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
						   nullptr, errorCode, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPSTR)&msgBuffer, 0, nullptr);
		std::string message(msgBuffer, size);
		LocalFree(msgBuffer);
		return message;
	}
};

BlockDeviceSimulator::BlockDeviceSimulator(BlockDeviceSimulator&& other) noexcept
	: filemap(other.filemap), fMap(other.fMap), fd(other.fd), DEVICE_SIZE(other.DEVICE_SIZE) {
	other.filemap = nullptr;
	other.fMap = nullptr;
	other.fd = INVALID_HANDLE_VALUE;
	other.DEVICE_SIZE = 0;
}

BlockDeviceSimulator::BlockDeviceSimulator(const std::string& fname) : fd(INVALID_HANDLE_VALUE), filemap(nullptr) {
	fd = CreateFileA(fname.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL,
					 nullptr);
	if (fd == INVALID_HANDLE_VALUE) {
		throw std::system_error(GetLastError(), WindowsCategory(), "Failed to open or create file");
	}

	LARGE_INTEGER size{};
	if (GetFileSizeEx(fd, &size) == 0 || size.QuadPart == 0) {
		size.QuadPart = 1024 * 1024;
		if (SetFilePointerEx(fd, size, nullptr, FILE_BEGIN) == 0 || SetEndOfFile(fd) == 0) {
			CloseHandle(fd);
			throw std::system_error(GetLastError(), WindowsCategory(), "Failed to set file size");
		}
	}
	// TODO: allow bigger files
	DEVICE_SIZE = (int)size.QuadPart;

	fMap = CreateFileMappingA(fd, nullptr, PAGE_READWRITE, 0, DEVICE_SIZE, nullptr);
	if (fMap == nullptr) {
		CloseHandle(fd);
		throw std::system_error(GetLastError(), WindowsCategory(), "Failed to create file mapping");
	}

	filemap = static_cast<unsigned char*>(MapViewOfFile(fMap, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, DEVICE_SIZE));
	if (filemap == nullptr) {
		CloseHandle(fMap);
		CloseHandle(fd);
		throw std::system_error(GetLastError(), WindowsCategory(), "Failed to map view of file");
	}
}

BlockDeviceSimulator::~BlockDeviceSimulator() {
	if (filemap == nullptr)
		return;
	UnmapViewOfFile(filemap);
	CloseHandle(fMap);
	CloseHandle(fd);
}
#else
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
class ErrnoCategory : public std::error_category {
  public:
	const char* name() const noexcept override {
		return "errno";
	}

	std::string message(int ev) const override {
		return std::strerror(ev);
	}
};

BlockDeviceSimulator::BlockDeviceSimulator(const std::string& fname) : DEVICE_SIZE(0), fd(-1), filemap(nullptr) {
	fd = open(fname.c_str(), O_CREAT | O_RDWR, 0666);
	if (fd == -1) {
		throw std::system_error(errno, ErrnoCategory(), "Failed to open or create file");
	}

	off_t size = lseek(fd, 0, SEEK_END);
	if (size == 0) {
		size = 1024 * 1024;
		if (lseek(fd, size - 1, SEEK_SET) == -1 || ::write(fd, "\0", 1) == -1) {
			close(fd);
			throw std::system_error(errno, ErrnoCategory(), "Failed to set file size");
		}
	}
	DEVICE_SIZE = (unsigned)size;

	filemap = static_cast<unsigned char*>(mmap(nullptr, DEVICE_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0));
	if (filemap == MAP_FAILED) {
		close(fd);
		throw std::system_error(errno, ErrnoCategory(), "Failed to mmap file");
	}
}

BlockDeviceSimulator::BlockDeviceSimulator(BlockDeviceSimulator&& other) noexcept
	: DEVICE_SIZE(other.DEVICE_SIZE), fd(other.fd), filemap(other.filemap) {
	other.filemap = nullptr;
	other.fd = -1;
}

BlockDeviceSimulator::~BlockDeviceSimulator() {
	if (filemap == nullptr)
		return;
	munmap(filemap, DEVICE_SIZE);
	close(fd);
}
#endif

void BlockDeviceSimulator::read(size_t addr, size_t size, char* ans) const {
	memcpy(ans, filemap + addr, size);
}

void BlockDeviceSimulator::write(size_t addr, size_t size, const char* data) {
	memcpy(filemap + addr, data, size);
}

void BlockDeviceSimulator::fill(size_t addr, size_t size, uint8_t value) {
	memset(filemap + addr, value, size);
}
