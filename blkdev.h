#ifndef __BLKDEVSIM__H__
#define __BLKDEVSIM__H__

#include <string>
#include <stdint.h>

class BlockDeviceSimulator {
  public:
	BlockDeviceSimulator(BlockDeviceSimulator&& other) noexcept;
	explicit BlockDeviceSimulator(const std::string& fname);
	~BlockDeviceSimulator();

	void read(size_t addr, size_t size, char* ans) const;
	void write(size_t addr, size_t size, const char* data);
	void fill(size_t addr, size_t size, uint8_t value);

	unsigned int DEVICE_SIZE;

  private:
#ifdef _WIN32
	void* fd;
	void* fMap;
#else
	int fd;
#endif
	unsigned char* filemap;
};

#endif // __BLKDEVSIM__H__
