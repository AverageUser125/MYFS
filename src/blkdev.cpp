#include "blkdev.hpp"
#include <sys/mman.h>
#include "config.hpp"

BlockDeviceSimulator::BlockDeviceSimulator(const std::string& fname) : fd(-1), filemap(nullptr) {
	// Check if the file exists
	if (access(fname.c_str(), F_OK) == -1) {
		// File doesn't exist, create it
		fd = open(fname.c_str(), O_CREAT | O_RDWR | O_EXCL, NEW_FILE_PERMISSIONS);
		if (fd == -1) {
			throw std::system_error(errno, std::generic_category(), "Failed to create file");
		}

		if (lseek(fd, DEVICE_SIZE - 1, SEEK_SET) == -1) {
			close(fd);
			throw std::system_error(errno, std::generic_category(), "Failed to seek in file");
		}

		if (::write(fd, "\0", 1) == -1) {
			close(fd);
			throw std::system_error(errno, std::generic_category(), "Failed to write initial data to file");
		}
	} else {
		// File exists, open it
		fd = open(fname.c_str(), O_RDWR);
		if (fd == -1) {
			throw std::system_error(errno, std::generic_category(), "Failed to open file");
		}
	}

	filemap = static_cast<unsigned char*>(mmap(nullptr, DEVICE_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0));
	if (filemap == MAP_FAILED) {
		close(fd);
		throw std::system_error(errno, std::generic_category(), "Failed to mmap file");
	}
}

BlockDeviceSimulator::~BlockDeviceSimulator() {
	munmap(filemap, DEVICE_SIZE);
	close(fd);
}

void BlockDeviceSimulator::read(size_t addr, size_t size, char* ans) {
	memcpy(ans, filemap + addr, size);
}

void BlockDeviceSimulator::write(size_t addr, size_t size, const char* data) {
	memcpy(filemap + addr, data, size);
}
