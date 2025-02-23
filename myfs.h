#pragma once

#include "blkdev.h"
#include <string>
#include <vector>
#include <optional>
#include <cstring>
#include <stdint.h>
#include <type_traits>
#include "config.h"

constexpr uint16_t EXT2_SUPER_MAGIC = 0xEF53;

constexpr uint16_t INODE_TYPE_SYMBOL_LINK = 0xA000;
constexpr uint16_t INODE_TYPE_FILE = 0x8000;
constexpr uint16_t INODE_TYPE_DEV = 0x6000;
constexpr uint16_t INODE_TYPE_DIRECTORY = 0x4000;
constexpr uint16_t INODE_TYPE_CHAR_DEV = 0x2000;
constexpr uint16_t INODE_TYPE_FIFO = 0x1000;
constexpr uint16_t INODE_TYPE_MASK = 0xF000;

constexpr uint32_t EXT2_BAD_INO = 1;		 //	bad blocks inode
constexpr uint32_t EXT2_ROOT_INO = 2;		 //	root directory inode
constexpr uint32_t EXT2_ACL_IDX_INO = 3;	 //	ACL index inode (deprecated?)
constexpr uint32_t EXT2_ACL_DATA_INO = 4;	 //	ACL data inode (deprecated?)
constexpr uint32_t EXT2_BOOT_LOADER_INO = 5; //	boot loader inode
constexpr uint32_t EXT2_UNDEL_DIR_INO = 6;	 //	undelete directory inode

constexpr uint8_t EXT2_FT_UNKNOWN = 0;	//Unknown File Type
constexpr uint8_t EXT2_FT_REG_FILE = 1; //	Regular File
constexpr uint8_t EXT2_FT_DIR = 2;		//	Directory File
constexpr uint8_t EXT2_FT_CHRDEV = 3;	//	Character Device
constexpr uint8_t EXT2_FT_BLKDEV = 4;	//	Block Device
constexpr uint8_t EXT2_FT_FIFO = 5;		//	Buffer File
constexpr uint8_t EXT2_FT_SOCK = 6;		//	Socket File
constexpr uint8_t EXT2_FT_SYMLINK = 7;	//	Symbolic Link

PACK(typedef struct {
	uint32_t s_inodes_count;	  /* Inodes count */
	uint32_t s_blocks_count;	  /* Blocks count */
	uint32_t s_r_blocks_count;	  /* Reserved blocks count */
	uint32_t s_free_blocks_count; /* Free blocks count */
	uint32_t s_free_inodes_count; /* Free inodes count */
	uint32_t s_first_data_block;  /* First Data Block */
	uint32_t s_log_block_size;	  /* Block size */
	int32_t s_log_frag_size;	  /* Fragment size */
	uint32_t s_blocks_per_group;  /* # Blocks per group */
	uint32_t s_frags_per_group;	  /* # Fragments per group */
	uint32_t s_inodes_per_group;  /* # Inodes per group */
	uint32_t s_mtime;			  /* Mount time */
	uint32_t s_wtime;			  /* Write time */
	uint16_t s_mnt_count;		  /* Mount count */
	int16_t s_max_mnt_count;	  /* Maximal mount count */
	uint16_t s_magic;			  /* Magic signature */
	uint16_t s_state;			  /* File system state */
	uint16_t s_errors;			  /* Behaviour when detecting errors */
	uint16_t s_pad;
	uint32_t s_lastcheck;	  /* time of last check */
	uint32_t s_checkinterval; /* max. time between checks */
	uint32_t s_creator_os;	  /* OS */
	uint32_t s_rev_level;	  /* Revision level */
	uint16_t s_def_resuid;	  /* Default uid for reserved blocks */
	uint16_t s_def_resgid;	  /* Default gid for reserved blocks */
	uint32_t s_first_inode;	  /* First inode */
	uint32_t s_inode_size;	  /* Inodes size */
							  // uint8_t s_reserved[932];  /* Padding to the end of the block */
})
Ext2SuperBlock;

PACK(typedef struct {
	uint32_t bg_block_bitmap;	   /* Blocks bitmap block */
	uint32_t bg_inode_bitmap;	   /* Inodes bitmap block */
	uint32_t bg_inode_table;	   /* Inodes table block */
	uint16_t bg_free_blocks_count; /* Free blocks count */
	uint16_t bg_free_inodes_count; /* Free inodes count */
	uint16_t bg_used_dirs_count;   /* Directories count */
	uint16_t bg_pad;
	uint32_t bg_reserved[3];
})
Ext2GroupDescriptor;

constexpr auto EXT2_DIRECT_BLOCKS = 13;
constexpr auto EXT2_N_BLOCKS = 15;
PACK(typedef struct {
	uint16_t i_mode;		/* File mode */
	uint16_t i_uid;			/* Owner Uid */
	uint32_t i_size;		/* Size in bytes */
	uint32_t i_atime;		/* Access time */
	uint32_t i_ctime;		/* Creation time */
	uint32_t i_mtime;		/* Modification time */
	uint32_t i_dtime;		/* Deletion Time */
	uint16_t i_gid;			/* Group Id */
	uint16_t i_links_count; /* Links count */
	uint32_t i_blocks;		/* Blocks count */
	uint32_t i_flags;		/* File flags */

	union {
		struct {
			uint32_t l_i_reserved1;
		} linux1;

		struct {
			uint32_t h_i_translator;
		} hurd1;

		struct {
			uint32_t m_i_reserved1;
		} masix1;
	} osd1; /* OS dependent 1 */

	uint32_t i_block[EXT2_N_BLOCKS]; /* Pointers to blocks */
	uint32_t i_version;				 /* File version (for NFS) */
	uint32_t i_file_acl;			 /* File ACL */
	uint32_t i_dir_acl;				 /* Directory ACL */
	uint32_t i_faddr;				 /* Fragment address */

	union {
		struct {
			uint8_t l_i_frag;  /* Fragment number */
			uint8_t l_i_fsize; /* Fragment size */
			uint16_t i_pad1;
			uint32_t l_i_reserved2[2];
		} linux2;

		struct {
			uint8_t h_i_frag;  /* Fragment number */
			uint8_t h_i_fsize; /* Fragment size */
			uint16_t h_i_mode_high;
			uint16_t h_i_uid_high;
			uint16_t h_i_gid_high;
			uint32_t h_i_author;
		} hurd2;

		struct {
			uint8_t m_i_frag;  /* Fragment number */
			uint8_t m_i_fsize; /* Fragment size */
			uint16_t m_pad1;
			uint32_t m_i_reserved2[2];
		} masix2;
	} osd2; /* OS dependent 2 */
})
Ext2Inode;

constexpr uint16_t EXT2_DIR_ENTRY_SIZE = 8;
PACK(typedef struct {
	uint32_t inode;	   /* Inode number */
	uint16_t rec_len;  /* Directory entry length */
	uint8_t name_len;  /* Name length */
	uint8_t file_type; /* File type */
	char name[];	   /* File name */
})
Ext2DirEntry;

class MyFs {
  public:
	explicit MyFs(BlockDeviceSimulator&& device);
	~MyFs();

	void sync();
	void format();
	static void rightsToString(uint16_t rights, char buf[9]);
	bool isFileExists(const std::string& filepath);
	bool createFile(const std::string& filepath);
	bool deleteFile(const std::string& filepath);
	bool createDirectory(const std::string& filepath);
	bool deleteDirectory(const std::string& filepath);
	bool getContent(const std::string& filepath, std::string& content);
	bool setContent(const std::string& filepath, const std::string& content);
	bool ls(const std::string& dirPath);
	bool getDirectoryContents(const std::string& dirPath, std::vector<std::string>& filenames);

  private:
	Ext2SuperBlock SuperBlock;
	BlockDeviceSimulator device;
	Ext2GroupDescriptor* GrpDscrTbl; // dynamic size
	uint32_t BLOCK_SIZE;
	uint32_t gd_count;

	uint32_t findFreeBit(uint32_t bitmap_block, uint32_t max_bits);
	void setBit(uint32_t bitmap_block, uint32_t bit, bool state);
	uint32_t getInodeAddress(uint32_t inode_num);
	bool allocateBlock(uint32_t& data_block);
	bool allocateInode(uint32_t& inode_num);
	bool allocateInodeAndBlock(uint32_t& inode_num, uint32_t& data_block);
	void deallocateInode(uint32_t inode_number);
	void deallocateBlock(uint32_t block_number);
	uint32_t findFreeGroup();
	static void splitPath(const char* filepath, char* dir_path, char* filename);

	bool readSuperBlock(Ext2SuperBlock& Super);
	void readGroupDescriptorTable(Ext2GroupDescriptor*& GrpDscrTbl);
	bool writeInodeData(uint32_t inode_num, const char* data, uint32_t size);
	bool writeInodeData(uint32_t inode_num, Ext2Inode& inode, const char* data, uint32_t size);
	void readInodeStruct(uint32_t inode_num, Ext2Inode& inode);
	void writeInodeStruct(uint32_t inode_num, const Ext2Inode& inode);
	void readInodeData(const Ext2Inode&, char*& data, uint32_t& d_size);
	void readInodeData(uint32_t inode_num, char*& data, uint32_t& d_size);
	uint32_t getSubdirInode(uint32_t dir_inode, const char* subdir_name);
	uint32_t getDirInodeByName(const char* path);
	uint32_t getFileInode(uint32_t dir_inode, char* fname);
	bool addDirectoryEntry(uint32_t dir_inode, uint32_t inode_num, Ext2Inode& inode, const char* name, uint8_t type);
	bool removeDirEntry(Ext2Inode& dirInode, uint32_t inode_num);
	void initDirEntry(Ext2Inode& inodeDir, uint32_t inode_num, uint32_t dir_inode);
};
