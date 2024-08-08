#ifndef __BLKDEVSIM__H__
#define __BLKDEVSIM__H__

#include <string>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdexcept>
#include <cerrno>
#include <cstdio>
#include <unistd.h>
#include <cstdlib>
#include <cstring>
#include <system_error>

#define NEW_FILE_PERMISSIONS 0644

class BlockDeviceSimulator {
  public:
	explicit BlockDeviceSimulator(const std::string& fname);
	~BlockDeviceSimulator();

	void read(size_t addr, size_t size, char* ans);
	void write(size_t addr, size_t size, const char* data);

	static constexpr int DEVICE_SIZE = 1024 * 1024;

  private:
	int fd;
	unsigned char* filemap;
};

#endif // __BLKDEVSIM__H__
