# MYFS
1. This is a pet project to learn how file systems work.
2. This is an implementation of a subset of EXT2 features.
3. This program should work on both Windows and Linux.

# Todos
- [ ] Make ReadInodeData and WriteInodeData not require allocating all the file data to RAM
- [ ] Add support for indirect, doubly-indirect and triply-indirect block pointers
- [x] FIX THE FORMAT FUNCTION
- [ ] Make splitPath more sophisticated
- [ ] Clean up if move or copy fails half way through

# Todo Later
- [ ] Set deletion time and maybe clear the inode on delete
- [ ] Support directorys bigger than the block size
- [ ] Make and use backups of the super block and group descriptor tables
- [ ] The bitmap allocator should try to find blocks near the current block to improve locality (expanding file size), perhaps create a function to allocate many blocks also
- [ ] Make full use of 256 bits inodes by storing the high of many things, such as time_t
- [ ] Add UID, GID to API
- [x] Make the "ls" command somehow use only external API
- [ ] Fully support multiple group descriptor tables to support bigger devices

# To consider
- [ ] Add a VFS system to improve the file system API
- [ ] Add cache
- [ ] Make functions return errno instead of setting it
- [ ] Out of storage panic
- [ ] Checksums, everywhere
- [ ] Maybe set the access time on read
- [ ] Store small files directly in the i_block of the inode (less than 60 bytes), idk if only in 256 bits inode or only in EXT3

# Sources
- https://github.com/tsoding/arena
- https://github.com/SoSlow/Ext2/blob/master/Ext2/
- https://www.nongnu.org/ext2-doc/ext2.html

# License
Do whatever you want, no need to give me any credit, you can relicese and whatever you want.
Just know that I am not responsible for any damages this may cause. and I will not give an warranty for this.
just notice that the sources that this project uses/used have there own licenses which you need to follow.