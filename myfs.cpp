#define _CRT_SECURE_NO_WARNINGS
#include "myfs.h"
#include <cstring>
#include <cerrno>
#include <stdexcept>
#include <iostream>
#include <cassert>
#include <malloc.h>
#include <time.h>
#include <string.h>
#include <limits.h>
#include <stdint.h>
#include "arena.h"
#include "config.h"

MyFs::MyFs(BlockDeviceSimulator&& deviceIn)
	: SuperBlock({}), device(std::move(deviceIn)), GrpDscrTbl(nullptr), BLOCK_SIZE(0), gd_count(0) {
	if (readSuperBlock(SuperBlock)) {
		readGroupDescriptorTable(GrpDscrTbl);
	} else {
		std::cerr << "Formatting drive...";
		format();
	}
}

MyFs::~MyFs() {
	sync();
	delete[] GrpDscrTbl;
}

bool MyFs::createFile(const std::string& filepath) {
	// Add directory entry
	char dir_path[256]{}, filename[256]{};
	splitPath(filepath.c_str(), dir_path, filename);

	uint32_t dir_inode = getDirInodeByName(dir_path);
	if (dir_inode == 0) {
		errno = ENOENT;
		return false;
	}

	uint32_t inode_num = 0;
	uint32_t data_block = 0;
	allocateInodeAndBlock(inode_num, data_block);
	if (inode_num == 0)
		return false;

	Ext2Inode dirInode;
	readInodeStruct(dir_inode, dirInode);
	if (!addDirectoryEntry(dir_inode, inode_num, dirInode, filename, EXT2_FT_REG_FILE)) {
		deallocateBlock(data_block);
		deallocateInode(inode_num);
		return false;
	}

	Ext2Inode file_inode = {};
	file_inode.i_mode = INODE_TYPE_FILE | 0644;
	file_inode.i_links_count = 1;
	file_inode.i_blocks = (BLOCK_SIZE / 512);
	file_inode.i_block[0] = data_block;
	file_inode.i_ctime = (uint32_t)time(nullptr);
	file_inode.i_atime = file_inode.i_ctime;
	file_inode.i_mtime = file_inode.i_ctime;
	file_inode.i_gid = 0;
	file_inode.i_uid = 0;

	writeInodeStruct(inode_num, file_inode);

	return true;
}

bool MyFs::deleteFile(const std::string& filepath) {
	char dir_path[256], filename[256];
	splitPath(filepath.c_str(), dir_path, filename);
	uint32_t parentInodeNum = getDirInodeByName(dir_path);
	if (parentInodeNum == 0) {
		errno = ENOENT;
		return false; // Parent directory not found
	}

	uint32_t fileInodeNum = getSubdirInode(parentInodeNum, filename);
	if (fileInodeNum == 0) {
		errno = ENOENT;
		return false; // File not found
	}

	Ext2Inode fileInode;
	readInodeStruct(fileInodeNum, fileInode);

	if ((fileInode.i_mode & INODE_TYPE_MASK) != INODE_TYPE_FILE) {
		if ((fileInode.i_mode & INODE_TYPE_MASK) == INODE_TYPE_DIRECTORY) {
			errno = EISDIR;
		}
		return false;
	}

	for (int i = 0; i < EXT2_DIRECT_BLOCKS; i++) {
		if (fileInode.i_block[i] == 0) {
			break;
		}
		deallocateBlock(fileInode.i_block[i]); // Free the file's data block
	}

	deallocateInode(fileInodeNum); // Free the inode

	Ext2Inode dirInode;
	readInodeStruct(parentInodeNum, dirInode);
	// Remove entry from parent directory
	removeDirEntry(dirInode, fileInodeNum);

	return true;
}

bool MyFs::createDirectory(const std::string& filepath) {
	char dir_path[256], filename[256];
	splitPath(filepath.c_str(), dir_path, filename);

	uint32_t dir_inode = getDirInodeByName(dir_path);
	if (dir_inode == 0) {
		errno = ENOENT;
		return false;
	}

	uint32_t inode_num = 0;
	uint32_t data_block = 0;
	allocateInodeAndBlock(inode_num, data_block);
	if (inode_num == 0)
		return false;

	Ext2Inode dirInode;
	readInodeStruct(dir_inode, dirInode);
	dirInode.i_links_count++; // the ".." counts as hard link
	if (!addDirectoryEntry(dir_inode, inode_num, dirInode, filename, EXT2_FT_DIR)) {
		deallocateBlock(data_block);
		deallocateBlock(inode_num);
		errno = EIO;
		return false;
	}

	Ext2Inode file_inode = {};
	file_inode.i_mode = INODE_TYPE_DIRECTORY | 0644;
	file_inode.i_links_count = 2; // "." counts as a hard link
	file_inode.i_blocks = (BLOCK_SIZE / 512);
	file_inode.i_block[0] = data_block;
	file_inode.i_ctime = (uint32_t)time(nullptr);
	file_inode.i_atime = file_inode.i_ctime;
	file_inode.i_mtime = file_inode.i_ctime;
	file_inode.i_gid = 0;
	file_inode.i_uid = 0;

	// add "." and ".."
	initDirEntry(file_inode, inode_num, dir_inode);
	return true;
}

bool MyFs::deleteDirectory(const std::string& filepath) {
	char dir_path[256], filename[256];
	splitPath(filepath.c_str(), dir_path, filename);
	uint32_t parentInodeNum = getDirInodeByName(dir_path);
	if (parentInodeNum == 0) {
		errno = ENOENT;
		return false; // Parent directory not found
	}

	uint32_t fileInodeNum = getSubdirInode(parentInodeNum, filename);
	if (fileInodeNum == 0) {
		errno = ENOENT;
		return false; // File not found
	}

	Ext2Inode fileInode;
	readInodeStruct(fileInodeNum, fileInode);

	if ((fileInode.i_mode & INODE_TYPE_DIRECTORY) == 0) {
		errno = ENOTDIR;
		return false;
	}

	char* data = nullptr;
	uint32_t size = 0;
	readInodeData(fileInode, data, size);
	if (data == nullptr)
		return false;
	defer(delete[] data);


	if (data == nullptr)
		return false;
	Ext2DirEntry* direntry = (Ext2DirEntry*)data;

	// check if empty
	bool empty = true;
	int32_t currSize = size;
	while (currSize > 0) {
		if (direntry->inode == 0) {
			break;
		}
		// check if . or ..
		if (*direntry->name == '.' &&
			(direntry->name_len == 1 || (direntry->name_len == 2 && *(direntry->name + 1) == '.'))) {
			currSize -= direntry->rec_len;
			direntry = (Ext2DirEntry*)((char*)direntry + direntry->rec_len);
			continue;
		}
		empty = false;
		break;
	}
	if (!empty) {
		errno = ENOTEMPTY;
		return false;
	}

	for (int i = 0; i < EXT2_DIRECT_BLOCKS; i++) {
		if (fileInode.i_block[i] == 0) {
			break;
		}
		deallocateBlock(fileInode.i_block[i]);
	}
	deallocateInode(fileInodeNum);


	Ext2Inode dirInode;
	readInodeStruct(parentInodeNum, dirInode);
	dirInode.i_links_count--;
	// Remove entry from parent directory
	removeDirEntry(dirInode, fileInodeNum);
	writeInodeStruct(parentInodeNum, dirInode);

	return true;
}

bool MyFs::getContent(const std::string& filepath, std::string& content) {
	uint32_t inode_num = getDirInodeByName(filepath.c_str());
	if (inode_num == 0) {
		errno = ENOENT;
		return false;
	}

	Ext2Inode inode;
	readInodeStruct(inode_num, inode);
	if ((inode.i_mode & INODE_TYPE_MASK) == INODE_TYPE_DIRECTORY) {
		errno = EISDIR;
		return false;
	}

	char* data = nullptr;
	uint32_t size = 0;
	readInodeData(inode, data, size);
	if (data == nullptr) {
		return false;
	}

	// update acess time
	// inode.i_atime = time(nullptr);
	// writeInodeStruct(inode_num, inode)

	content.assign(data, size);
	delete[] data;
	return true;
}

bool MyFs::setContent(const std::string& filepath, const std::string& content) {
	uint32_t file_inode = getDirInodeByName(filepath.c_str());
	if (file_inode == 0) {
		errno = ENOENT;
		return false; // File not found
	}
	// TODO: this
	Ext2Inode inode;
	readInodeStruct(file_inode, inode);
	inode.i_mtime = (uint32_t)time(nullptr);
	return writeInodeData(file_inode, inode, content.c_str(), (uint32_t)content.size());
}

bool MyFs::ls(const std::string& dirPath) {
	uint32_t dir_inode = getDirInodeByName(dirPath.c_str());
	if (dir_inode == 0) {
		fprintf(stderr, "Error. Directory \"%s\" not found.\n", dirPath.c_str());
		return false;
	}

	char* data = nullptr;
	Ext2DirEntry* direntry = nullptr;
	uint32_t size = 0;

	readInodeData(dir_inode, data, size);
	if (data == nullptr)
		return false;
	direntry = (Ext2DirEntry*)data;

	printf("\n\n\t\tFolder: %s\n\n\n", dirPath.c_str());
	printf("%10s%3s%10s%16s%28s  %s\n", "Rsights", "Lc", "Inode", "Size", "Modification time", "Name");
	printf("%10s%3s%10s%16s%28s  %s\n", "======", "==", "=====", "====", "=================", "====");
	printf("\n");
	while (direntry->name_len > 0) {
		char fname[255]{};
		Ext2Inode inode;

		memcpy(fname, direntry->name, direntry->name_len);
		fname[direntry->name_len] = 0;

		readInodeStruct(direntry->inode, inode);

		time_t timeVal = (time_t)inode.i_mtime;
		char* time = ctime(&timeVal);
		if (time != nullptr)
			time[strlen(time) - 1] = '\0';

		char rights[12]{};
		rightsToString(inode.i_mode, rights);

		printf("%10s%3hu%10u%16u%28s  %s\n", rights, inode.i_links_count, direntry->inode, inode.i_size, time, fname);

		if ((char*)direntry - data + direntry->rec_len >= size)
			break;
		direntry = (Ext2DirEntry*)((char*)direntry + direntry->rec_len);
	}

	delete[] data;
	return true;
}

bool MyFs::getDirectoryContents(const std::string& dirPath, std::vector<std::string>& filenames) {
	filenames.clear();

	uint32_t dir_inode = getDirInodeByName(dirPath.c_str());
	if (dir_inode == 0) {
		errno = ENOENT;
		return false; // Directory not found
	}

	Ext2Inode dirInode;
	readInodeStruct(dir_inode, dirInode);

	if ((dirInode.i_mode & INODE_TYPE_DIRECTORY) == 0) {
		errno = ENOTDIR;
		return false; // Not a directory
	}

	uint32_t size = 0;
	char* data = nullptr;
	readInodeData(dirInode, data, size);
	if (data == nullptr)
		return false;
	defer(delete[] data);

	uint32_t offset = 0;
	while (offset < size) {
		Ext2DirEntry* entry = reinterpret_cast<Ext2DirEntry*>(data + offset);
		if (entry->name_len == 0) {
			break;
		}
		filenames.emplace_back(entry->name, entry->name_len);
		offset += entry->rec_len;
	}

	return true; // Success
}

#pragma region bitmap

uint32_t MyFs::findFreeBit(uint32_t bitmap_block, uint32_t max_bits) {
	assert(max_bits <= BLOCK_SIZE * 8);
	char* bitmap = (char*)arena_alloc(&global_arena, BLOCK_SIZE);
	defer(arena_reset(&global_arena));

	device.read(bitmap_block * BLOCK_SIZE, BLOCK_SIZE, bitmap);

	for (uint32_t i = 0; i < max_bits; ++i) {
		if ((bitmap[i / 8] & (1 << (i % 8))) == 0) { // Check if bit is free
			arena_reset(&global_arena);
			return i + 1;
		}
	}

	return 0; // No free bit found
}

void MyFs::setBit(uint32_t bitmap_block, uint32_t bitIn, bool state) {
	char* bitmap = (char*)arena_alloc(&global_arena, BLOCK_SIZE);
	defer(arena_reset(&global_arena));
	device.read(bitmap_block * BLOCK_SIZE, BLOCK_SIZE, bitmap);
	uint16_t bit = (uint16_t)(bitIn)-1;

	if (state) {
		uint8_t mask = (uint8_t)((1 << (bit % 8)));
		bitmap[bit / 8] |= mask; // Set bit
	} else {
		uint8_t mask = (uint8_t)(~(1 << (bit % 8)));
		bitmap[bit / 8] &= mask; // Clear bit
	}

	device.write(bitmap_block * BLOCK_SIZE, BLOCK_SIZE, bitmap);
}

void MyFs::splitPath(const char* filepath, char* dir_path, char* filename) {
	const char* last_slash = strrchr(filepath, '/');
	if (last_slash == nullptr) {
		strncpy(dir_path, "/", UINT8_MAX);
		strncpy(filename, filepath, UINT8_MAX);
		return;
	}
	// root
	if (last_slash == filepath) {
		strncpy(dir_path, "/", UINT8_MAX);
		strncpy(filename, last_slash + 1, UINT8_MAX);
		return;
	}

	size_t dir_len = last_slash - filepath;
	strncpy(dir_path, filepath, dir_len);
	dir_path[dir_len] = '\0';
	strncpy(filename, last_slash + 1, UINT8_MAX);
}

#pragma endregion

#pragma region EXT2 backend

void MyFs::sync() {
	// TODO: store backup of SuperBlock
	device.write(1024, sizeof(SuperBlock), (const char*)&SuperBlock);
	int pos = (SuperBlock.s_first_data_block + 1) * BLOCK_SIZE;
	device.write(pos, sizeof(Ext2GroupDescriptor), (const char*)GrpDscrTbl);
}

void MyFs::format() {
	BLOCK_SIZE = 1024;

	SuperBlock.s_inodes_count = device.DEVICE_SIZE / (BLOCK_SIZE * 8);
	SuperBlock.s_blocks_count = device.DEVICE_SIZE / BLOCK_SIZE;
	SuperBlock.s_r_blocks_count = 0;
	SuperBlock.s_free_blocks_count = SuperBlock.s_blocks_count - 1;
	SuperBlock.s_free_inodes_count = SuperBlock.s_inodes_count - 1;
	SuperBlock.s_first_data_block = 1;
	SuperBlock.s_log_block_size = 0;
	SuperBlock.s_log_frag_size = 0;
	SuperBlock.s_blocks_per_group = SuperBlock.s_blocks_count;
	SuperBlock.s_frags_per_group = SuperBlock.s_blocks_count;
	SuperBlock.s_inodes_per_group = SuperBlock.s_inodes_count;
	SuperBlock.s_mtime = 0;
	SuperBlock.s_wtime = 0;
	SuperBlock.s_mnt_count = 0;
	SuperBlock.s_max_mnt_count = -1;
	SuperBlock.s_magic = EXT2_SUPER_MAGIC;
	SuperBlock.s_state = 1;
	SuperBlock.s_errors = 1;
	SuperBlock.s_lastcheck = 0;
	SuperBlock.s_checkinterval = 0;
	SuperBlock.s_creator_os = 0;
	SuperBlock.s_rev_level = 0;
	SuperBlock.s_def_resuid = 0;
	SuperBlock.s_def_resgid = 0;
	SuperBlock.s_first_inode = 2;
	SuperBlock.s_inode_size = sizeof(Ext2Inode);

	GrpDscrTbl = new Ext2GroupDescriptor;
	GrpDscrTbl->bg_block_bitmap = 3;
	GrpDscrTbl->bg_inode_bitmap = 4;
	GrpDscrTbl->bg_inode_table = 5;
	GrpDscrTbl->bg_free_blocks_count = (uint16_t)SuperBlock.s_free_blocks_count;
	GrpDscrTbl->bg_free_inodes_count = (uint16_t)SuperBlock.s_free_inodes_count;
	GrpDscrTbl->bg_used_dirs_count = 1;

	sync();

	char* inode_bitmap = (char*)arena_alloc(&global_arena, BLOCK_SIZE);
	*inode_bitmap = 0x1;
	device.write(BLOCK_SIZE * GrpDscrTbl->bg_inode_bitmap, BLOCK_SIZE, inode_bitmap);

	// Initialize block bitmap (reserve blocks 0-4 + inode table)
	uint32_t inode_table_blocks =
		(SuperBlock.s_inodes_per_group * SuperBlock.s_inode_size + BLOCK_SIZE - 1) / BLOCK_SIZE;
	char* block_bitmap = (char*)arena_alloc(&global_arena, BLOCK_SIZE);
	uint32_t reserved_blocks = 5 + inode_table_blocks;
	for (uint32_t i = 0; i < reserved_blocks; ++i)
		block_bitmap[i / 8] |= (uint8_t)(1 << (i % 8)); // Mark reserved blocks as used

	device.write(BLOCK_SIZE * GrpDscrTbl->bg_block_bitmap, BLOCK_SIZE, block_bitmap);

	uint32_t root_inode_num = 0;
	uint32_t root_data_block = 0;
	allocateInodeAndBlock(root_inode_num, root_data_block);
	Ext2Inode new_inode = {};
	new_inode.i_mode = INODE_TYPE_DIRECTORY;
	new_inode.i_flags = 0644;
	new_inode.i_uid = 1000;
	new_inode.i_gid = 1000;
	new_inode.i_ctime = (uint32_t)time(nullptr);
	new_inode.i_atime = new_inode.i_ctime;
	new_inode.i_mtime = new_inode.i_ctime;
	new_inode.i_links_count = 1;
	new_inode.i_blocks = (BLOCK_SIZE / 512);
	new_inode.i_block[0] = root_data_block;
	initDirEntry(new_inode, EXT2_ROOT_INO, EXT2_ROOT_INO);
	arena_reset(&global_arena);
}

uint32_t MyFs::getInodeAddress(uint32_t inode_num) {
	uint32_t group_num = (inode_num - 1) / SuperBlock.s_inodes_per_group;
	uint32_t inode_local_num = (inode_num - 1) % SuperBlock.s_inodes_per_group;
	uint32_t group_index = group_num % gd_count;
	uint32_t inode_tbl_block = GrpDscrTbl[group_index].bg_inode_table;

	return (inode_tbl_block * BLOCK_SIZE) + (inode_local_num * SuperBlock.s_inode_size);
}

uint32_t MyFs::findFreeGroup() {
	for (uint32_t i = 0; i < gd_count; i++) {
		if (GrpDscrTbl[i].bg_free_blocks_count > 0 || GrpDscrTbl[i].bg_free_inodes_count > 0) {
			return i;
		}
	}
	return UINT_MAX;
}

bool MyFs::allocateBlock(uint32_t& data_block) {
	if (SuperBlock.s_free_blocks_count == 0) {
		errno = ENOSPC;
		return false;
	}
	data_block = findFreeBit(GrpDscrTbl->bg_block_bitmap, SuperBlock.s_blocks_count);
	if (data_block == 0) {
		errno = ENOSPC;
		return false;
	}

	setBit(GrpDscrTbl->bg_block_bitmap, data_block, true);
	SuperBlock.s_free_blocks_count--;
	GrpDscrTbl->bg_free_blocks_count--;

	sync();
	return true;
}

bool MyFs::allocateInode(uint32_t& inode_num) {
	if (SuperBlock.s_free_inodes_count == 0) {
		errno = ENOSPC;
		return false;
	}

	inode_num = findFreeBit(GrpDscrTbl->bg_inode_bitmap, SuperBlock.s_inodes_count);
	if (inode_num == 0) {
		errno = ENOSPC;
		return false;
	}

	setBit(GrpDscrTbl->bg_inode_bitmap, inode_num, true);
	SuperBlock.s_free_inodes_count--;
	GrpDscrTbl->bg_free_inodes_count--;

	sync();
	return true;
}

bool MyFs::allocateInodeAndBlock(uint32_t& inode_num, uint32_t& data_block) {
	return allocateInode(inode_num) && allocateBlock(data_block);
}

void MyFs::deallocateInode(uint32_t inode_num) {
	assert(inode_num > 0 && inode_num < SuperBlock.s_inodes_count);

	setBit(GrpDscrTbl->bg_inode_bitmap, inode_num, false); // Mark inode as free
	SuperBlock.s_free_inodes_count++;
	GrpDscrTbl->bg_free_inodes_count++;

	sync();
}

void MyFs::deallocateBlock(uint32_t block_num) {
	assert(block_num > 0 && block_num < SuperBlock.s_blocks_count);

	setBit(GrpDscrTbl->bg_block_bitmap, block_num, false); // Mark block as free
	SuperBlock.s_free_blocks_count++;
	GrpDscrTbl->bg_free_blocks_count++;
}

void MyFs::rightsToString(uint16_t rights, char buf[9]) {
	//TODO: change this shit
	buf[0] = (rights & 0x4000) != 0 ? 'd' : '-';

	for (int i = 0; i < 9; ++i) {
		uint16_t fl = (rights >> (8 - i)) & 0x1;
		switch ((i) % 3) {
		case 0:
			buf[i + 1] = fl != 0U ? 'r' : '-';
			break;
		case 1:
			buf[i + 1] = fl != 0U ? 'w' : '-';
			break;
		case 2:
			buf[i + 1] = fl != 0U ? 'x' : '-';
			break;
		default:
			buf[i + 1] = '?';
		}
	}
}

bool MyFs::readSuperBlock(Ext2SuperBlock& super) {
	// skip MBR
	device.read(1024, sizeof(Ext2SuperBlock), (char*)&super);
	if (super.s_magic != EXT2_SUPER_MAGIC) {
		return false;
	}
	// The block size is computed using this 32bit value as the number of bits to shift left the value 1024
	BLOCK_SIZE = 1024 << super.s_log_block_size;
	return true;
}

void MyFs::readGroupDescriptorTable(Ext2GroupDescriptor*& GrpDscrTbls) {
	delete GrpDscrTbls;

	gd_count = SuperBlock.s_blocks_count / SuperBlock.s_blocks_per_group + 1;
	GrpDscrTbls = new Ext2GroupDescriptor[gd_count];

	//skip SuperBlock to archive right GroupDescriptor
	int pos = (SuperBlock.s_first_data_block + 1) * BLOCK_SIZE;
	device.read(pos, sizeof(Ext2GroupDescriptor) * gd_count, (char*)GrpDscrTbls);
}

bool MyFs::writeInodeData(uint32_t inode_num, const char* data, uint32_t size) {
	Ext2Inode inode;
	readInodeStruct(inode_num, inode);
	return writeInodeData(inode_num, inode, data, size);
}

bool MyFs::writeInodeData(uint32_t inode_num, Ext2Inode& inode, const char* data, uint32_t size) {
	if (size > 13 * BLOCK_SIZE) {
		errno = ENOSYS;
		return false;
	}

	uint32_t blocks_needed = (size + BLOCK_SIZE - 1) / BLOCK_SIZE;
	uint32_t existing_blocks = inode.i_blocks / (BLOCK_SIZE / 512);

	for (uint32_t i = 0; i < blocks_needed; ++i) {
		if (i >= existing_blocks) {
			uint32_t new_block = 0;
			if (!allocateBlock(new_block)) {
				return false; // No space left
			}
			inode.i_block[i] = new_block;
		}

		uint32_t block_size = BLOCK_SIZE;
		if (i == blocks_needed - 1) {
			block_size = (size % BLOCK_SIZE != 0U) ? size % BLOCK_SIZE : BLOCK_SIZE;
		}

		device.write(BLOCK_SIZE * inode.i_block[i], block_size, data + (i * BLOCK_SIZE));
	}

	inode.i_size = size;
	inode.i_blocks = blocks_needed * (BLOCK_SIZE / 512);
	writeInodeStruct(inode_num, inode);
	return true;
}

void MyFs::readInodeStruct(uint32_t inode_num, Ext2Inode& inode) {
	device.read(getInodeAddress(inode_num), sizeof(Ext2Inode), (char*)&inode);
}

void MyFs::writeInodeStruct(uint32_t inode_num, const Ext2Inode& inode) {
	device.write(getInodeAddress(inode_num), sizeof(Ext2Inode), (const char*)&inode);
}

void MyFs::readInodeData(const Ext2Inode& inode, char*& data, uint32_t& d_size) {
	uint32_t data_size = inode.i_size;
	data = new char[data_size];
	// could actually request too much memory
	if (data == nullptr) {
		errno = ENOMEM;
		data = nullptr;
		d_size = 0;
		return;
	}

	memset(data, 0, data_size);
	// Handle i_blocks, which is in 512-byte units (EXT2 standard)
	uint32_t blocks_to_read = inode.i_blocks / (BLOCK_SIZE / 512);

	uint32_t cur = 0, size = 0;
	for (uint32_t i = 0; i < blocks_to_read && cur < data_size; ++i) {
		uint32_t block = inode.i_block[i];
		if (block == 0) {
			break; // No more valid blocks
		}

		if (data_size - cur > BLOCK_SIZE) {
			size = BLOCK_SIZE;
		} else {
			size = data_size - cur;
		}

		// Read data from the device
		device.read(BLOCK_SIZE * block, size, data + cur);
		cur += size;
	}

	d_size = data_size;
}

void MyFs::readInodeData(uint32_t inode_num, char*& data, uint32_t& d_size) {
	Ext2Inode inode;
	readInodeStruct(inode_num, inode);
	return readInodeData(inode, data, d_size);
}

uint32_t MyFs::getSubdirInode(uint32_t dir_inode, const char* subdir_name) {
	char* data = nullptr;
	Ext2DirEntry* direntry = nullptr;
	uint32_t size = 0, res = 0;
	readInodeData(dir_inode, data, size);
	if (data == nullptr)
		return 0;
	defer(delete[] data);


	direntry = (Ext2DirEntry*)data;
	while (direntry->name_len > 0) {
		char dir_name[255];
		memcpy(dir_name, direntry->name, direntry->name_len);
		dir_name[direntry->name_len] = 0;
		if (strcmp(subdir_name, dir_name) == 0) {
			res = direntry->inode;
			break;
		}
		if ((char*)direntry - data + direntry->rec_len >= size)
			break;
		//go to the next dir entry in direntry linked list
		direntry = (Ext2DirEntry*)((char*)direntry + direntry->rec_len);
	}

	return res;
}

bool MyFs::isFileExists(const std::string& filepath) {
	char dir_path[256], filename[256];
	splitPath(filepath.c_str(), dir_path, filename);
	uint32_t parentInodeNum = getDirInodeByName(dir_path);
	if (parentInodeNum == 0) {
		return false;
	}
	if (parentInodeNum == EXT2_ROOT_INO) {
		return true;
	}
	uint32_t fileInodeNum = getSubdirInode(parentInodeNum, filename);
	return fileInodeNum != 0;
}

uint32_t MyFs::getDirInodeByName(const char* path) {
	char dir[255]{};
	const char *first = nullptr, *sec = nullptr;
	size_t len = 0;
	uint32_t dir_inode = EXT2_ROOT_INO;
	first = path;
	do {
		first = strchr(first, '/');
		if (first == nullptr) {
			dir_inode = 0;
			break;
		}
		//skip '/' char
		++first;
		//find the second '/' char position
		sec = strchr(first, '/');

		if (sec == nullptr)
			len = strlen(first);
		else
			len = sec - first;

		strncpy(dir, first, len);
		dir[len] = '\0';

		first = sec;
		if (strlen(dir) == 0)
			continue;

		dir_inode = getSubdirInode(dir_inode, dir);
	} while (dir_inode != 0 && sec != nullptr); //exit if no '/' char found or dir does not exist

	return dir_inode;
}

uint32_t MyFs::getFileInode(uint32_t dir_inode, char* fname) {
	Ext2DirEntry* direntry = nullptr;
	char* data = nullptr;
	uint32_t size = 0, res = 0;

	readInodeData(dir_inode, data, size);
	if (data == nullptr)
		return 0;
	defer(delete[] data);

	direntry = (Ext2DirEntry*)data;

	while (direntry->name_len > 0) {
		if (direntry->file_type == EXT2_FT_REG_FILE) {
			char file_name[255]{};
			memcpy(file_name, direntry->name, direntry->name_len);
			file_name[direntry->name_len] = 0;
			if (strcmp(file_name, fname) == 0) {
				res = direntry->inode;
				break;
			}
		}

		if ((char*)direntry - data + direntry->rec_len >= size)
			break;
		//go to the next dir entry in direntry linked list
		direntry = (Ext2DirEntry*)((char*)direntry + direntry->rec_len);
	}

	return res;
}

bool MyFs::addDirectoryEntry(uint32_t dir_inode, uint32_t inode_num, Ext2Inode& dirInode, const char* name,
							 uint8_t type) {
	uint32_t size = 0;

	char* data = nullptr;
	readInodeData(dirInode, data, size);
	if (data == nullptr)
		return false;
	defer(delete[] data);

	Ext2DirEntry* direntry = (Ext2DirEntry*)data;

	uint32_t available_space = 0;
	size_t fileNameLengthBig = strlen(name);
	if (fileNameLengthBig > 0xFFFF) {
		errno = ENAMETOOLONG;
		return false;
	}
	uint8_t fileNameLength = (uint8_t)fileNameLengthBig;
	uint16_t entry_size = (uint16_t)((EXT2_DIR_ENTRY_SIZE + fileNameLength + 3) & ~3); // Align to 4 bytes

	// Check the current size to determine whether last entry is the valid entry or the end marker
	int32_t currSize = size;
	Ext2DirEntry* last_entry = nullptr;
	while (currSize > 0) {
		if (direntry->name_len == 0 || direntry->inode == 0) {
			break;
		}
		last_entry = direntry;
		currSize -= direntry->rec_len;
		direntry = (Ext2DirEntry*)((char*)direntry + direntry->rec_len);
	}
	// empty directory... not possible
	if (last_entry == nullptr) {
		errno = EIO;
		return false;
	}

	// If currSize is 0, it means the last valid entry was the padded one, else the end entry is the padded one.
	if (currSize == 0) {
		uint16_t last_entry_min_size = (uint16_t)(EXT2_DIR_ENTRY_SIZE + last_entry->name_len + 3) & ~3;
		available_space = last_entry->rec_len - last_entry_min_size;
		last_entry->rec_len = last_entry_min_size;
	} else {
		available_space = direntry->rec_len;
	}

	// If there's not enough space for the new entry, return ENOSPC
	if (available_space < entry_size) {
		errno = ENOSPC;
		return false;
	}

	// Step 2: Add the new entry as the last entry
	Ext2DirEntry* new_entry = (Ext2DirEntry*)((char*)last_entry + last_entry->rec_len);
	new_entry->inode = inode_num;
	new_entry->rec_len = entry_size;
	new_entry->name_len = fileNameLength;
	new_entry->file_type = type;
	memcpy(new_entry->name, name, fileNameLength);

	// Step 3: Mark the next entry as the end by setting inode_num to 0
	Ext2DirEntry* end_marker = (Ext2DirEntry*)((char*)new_entry + new_entry->rec_len);
	if ((char*)end_marker < data + BLOCK_SIZE) {
		end_marker->inode = 0;
		end_marker->file_type = EXT2_FT_UNKNOWN;
		end_marker->name_len = 0;
		end_marker->rec_len = (uint16_t)(available_space - new_entry->rec_len);
	}

	// Step 4: Write the updated inode and data block back to disk

	writeInodeData(dir_inode, dirInode, data, size);

	return true;
}

bool MyFs::removeDirEntry(Ext2Inode& dirInode, uint32_t inode_num) {
	// Read the first direct block (ignoring indirects)
	uint32_t block_addr = dirInode.i_block[0];
	if (block_addr == 0) {
		return false; // No data block assigned
	}

	uint32_t size = 0;
	char* data = nullptr;
	readInodeData(dirInode, data, size);
	if (data == nullptr)
		return false;
	defer(delete[] data);

	Ext2DirEntry* direntry = (Ext2DirEntry*)data;
	Ext2DirEntry* prevEntry = nullptr;
	uint32_t currSize = 0;

	while (currSize < size) {
		if (direntry->inode == inode_num) {
			// TODO: perhaps shift all entries instead of exapnding the current one
			if (prevEntry != nullptr) {
				prevEntry->rec_len += direntry->rec_len; // Merge with the next entry
			} else {
				direntry->inode = 0; // Mark as deleted
			}

			// Write the modified directory block back
			device.write(block_addr * BLOCK_SIZE, size, data);

			return true;
		}

		currSize += direntry->rec_len;
		prevEntry = direntry;
		direntry = (Ext2DirEntry*)((char*)direntry + direntry->rec_len);
	}

	return false; // Entry not found
}

void MyFs::initDirEntry(Ext2Inode& inodeDir, uint32_t inode_num, uint32_t dir_inode) {
	// Allocate memory for directory entries dynamically
	Ext2DirEntry* dot = reinterpret_cast<Ext2DirEntry*>(arena_alloc(&global_arena, 12));
	dot->inode = inode_num;
	dot->rec_len = 12;
	dot->name_len = 1;
	dot->file_type = EXT2_FT_DIR;
	memcpy(dot->name, ".", 1);

	Ext2DirEntry* dotdot = reinterpret_cast<Ext2DirEntry*>(arena_alloc(&global_arena, 12));
	dotdot->inode = dir_inode;
	dotdot->rec_len = (uint16_t)(BLOCK_SIZE - 12);
	dotdot->name_len = 2;
	dotdot->file_type = EXT2_FT_DIR;
	memcpy(dotdot->name, "..", 2);

	Ext2DirEntry* end = reinterpret_cast<Ext2DirEntry*>(arena_alloc(&global_arena, EXT2_DIR_ENTRY_SIZE));
	end->inode = 0;
	end->rec_len = 0;
	end->name_len = 0;
	end->file_type = EXT2_FT_UNKNOWN;

	// Allocate memory for directory block
	char* dir_data = (char*)arena_alloc(&global_arena, BLOCK_SIZE);
	assert(BLOCK_SIZE >= 512); // to remove warning
	memcpy(dir_data, dot, 12);
	memcpy(dir_data + 12, dotdot, 12);
	memcpy(dir_data + 24, end, 8);

	writeInodeData(inode_num, inodeDir, dir_data, BLOCK_SIZE);

	// Clean up
	arena_reset(&global_arena);
}

#pragma endregion
