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

	long block_size = EXT2_BLOCK_SIZE(&sb);
	char* block = malloc(block_size);
	int remained_bytes = inode.i_size;
	for (size_t i = 0; i < EXT2_NDIR_BLOCKS && remained_bytes > 0; ++i) {
		if (pread(img, block, block_size, inode.i_block[i] * block_size) < 0) {
			free(block);
			return -errno;
		}
		struct ext2_dir_entry_2* dir_entry = (struct ext2_dir_entry_2*)block;
		while ((char*)dir_entry - block < block_size) {
			if (dir_entry->inode == 0) {
				break;
			}
			char file_type = (dir_entry->file_type == EXT2_FT_DIR ? 'd' : 'f');
			report_file(dir_entry->inode, file_type, dir_entry->name);
			dir_entry = (struct ext2_dir_entry_2*)((char*)dir_entry + dir_entry->rec_len);
		}
		remained_bytes -= block_size;
	}
	// indirect blocks
	return 0;
}

// void report_file(int inode_nr, char type, const char *name) {
// 	printf("Inode %d, type: %c, name: %s\n", inode_nr, type, name);
// }
