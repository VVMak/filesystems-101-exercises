#include <solution.h>

#include <assert.h>
#include <ext2fs/ext2fs.h>
#include <linux/fs.h>
#include <stdbool.h>
#include <sys/stat.h>
#include <unistd.h>


inline size_t min(size_t first, size_t second) {
	return (first < second ? first : second);
}

int read_bg_header(struct ext2_group_desc* bg, int img, struct ext2_super_block* sb, size_t inode_num) {
	--inode_num;
	size_t bg_number = inode_num / sb->s_inodes_per_group;
	inode_num %= sb->s_inodes_per_group;
	size_t block_size = EXT2_BLOCK_SIZE(sb);

	return pread(img, bg, sizeof(struct ext2_group_desc), (1024 / block_size + 1) * block_size + sizeof(struct ext2_group_desc) * bg_number);
}

int get_inode(struct ext2_inode* inode, int img, struct ext2_super_block* sb, struct ext2_group_desc* bg, size_t inode_num) {
	off_t inode_offset = bg->bg_inode_table * EXT2_BLOCK_SIZE(sb) + (inode_num - 1) * sb->s_inode_size;
	return pread(img, inode, sizeof(struct ext2_inode), inode_offset);
}

int dump_dir(int img, int inode_nr)
{
	(void) img;
	(void) inode_nr;

	struct ext2_super_block sb;
	int res = pread(img, &sb, sizeof(sb), 1024);
	if (res < 0) {
		return -errno;
	}
	if (EXT2_SUPER_MAGIC != sb.s_magic) {
		return -EINVAL;
	}

	struct ext2_group_desc bg;
	if (read_bg_header(&bg, img, &sb, inode_nr) < 0) {
		return -errno;
	}
	struct ext2_inode inode;
	if (get_inode(&inode, img, &sb, &bg, inode_nr) < 0) {
		return -errno;
	}

	size_t block_size = EXT2_BLOCK_SIZE(&sb);
	int remained_bytes = inode.i_size;
	for (size_t i = 0; i < EXT2_NDIR_BLOCKS && remained_bytes > 0; ++i) {
		off_t offset = inode.i_block[i] * block_size;
		for (size_t file_num = 0; file_num * sizeof(struct ext2_dir_entry_2) < block_size && remained_bytes > 0; ++file_num) {
			int size = offsetof(struct ext2_dir_entry_2, name);
			struct ext2_dir_entry_2 dir_entry;
			if (pread(img, &dir_entry, size, offset) < 0) {
				return -errno;
			}
			if (dir_entry.inode == 0) {
				assert(false);
				offset += dir_entry.rec_len;
				remained_bytes -= (dir_entry.rec_len ? dir_entry.rec_len : size);
				continue;
			}
			memset(dir_entry.name, 0, EXT2_NAME_LEN);
			if (pread(img, dir_entry.name, dir_entry.name_len, offset + size) < 0) {
				return -errno;
			}
			char file_type = (S_ISDIR(dir_entry.file_type) ? 'd' : 'f');
			report_file(dir_entry.inode, file_type, dir_entry.name);
			remained_bytes -= dir_entry.rec_len;
			offset += dir_entry.rec_len;
		}
	}
	// indirect blocks
	return 0;
}

// void report_file(int inode_nr, char type, const char *name) {
// 	printf("Inode %d, type: %c, name: %s\n", inode_nr, type, name);
// }
