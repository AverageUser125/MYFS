#include "blkdev.hpp"
#include <sys/mman.h>

BlockDeviceSimulator::BlockDeviceSimulator(std::string& fname) {
	// if file doesn't exist, create it
	if (access(fname.c_str(), F_OK) == -1) {
		fd = open(fname.c_str(), O_CREAT | O_RDWR | O_EXCL, 0664);

		if (fd == -1) {
			throw std::runtime_error(std::string("open-create failed: ") + strerror(errno));
		}

		if (lseek(fd, DEVICE_SIZE - 1, SEEK_SET) == -1) {
			throw std::runtime_error("Could not seek");
		}

		::write(fd, "\0", 1);
	} else {
		fd = open(fname.c_str(), O_RDWR);
		if (fd == -1) {
			throw std::runtime_error(std::string("open failed: ") + strerror(errno));
		}
	}

	filemap = (unsigned char*)mmap(nullptr, DEVICE_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (filemap == (unsigned char*)-1) {
		throw std::runtime_error(strerror(errno));
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
