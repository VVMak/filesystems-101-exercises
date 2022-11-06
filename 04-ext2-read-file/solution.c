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

int write_data_block(uint32_t block_num, uint32_t block_size, int img, int out, size_t file_size) {
	int size = min(file_size, block_size);
	void* block = malloc(size);
	int res = pread(img, block, size, block_num * block_size);
	if (res < 0) {
		free(block);
		return -1;
	}
	res = write(out, block, size);
	free(block);
	return (res < 0 ? res : size);
}

int dump_file(int img, int inode_nr, int out)
{
	(void) img;
	(void) inode_nr;
	(void) out;

	struct ext2_super_block sb;
	int res = pread(img, &sb, sizeof(sb), 1024);
	if (res < 0) {
		return -errno;
	}
	if (EXT2_SUPER_MAGIC != sb.s_magic) {
		return -EINVAL;
	}
	size_t block_size = EXT2_BLOCK_SIZE(&sb);

	struct ext2_group_desc bg;
	--inode_nr;
	size_t bg_number = inode_nr / sb.s_inodes_per_group;
	inode_nr %= sb.s_inodes_per_group;

	res = pread(img, &bg, sizeof(struct ext2_group_desc), (1024 / block_size + 1) * block_size + sizeof(struct ext2_group_desc) * bg_number);
	if (res < 0) {
		return -errno;
	}

	off_t inode_offset = bg.bg_inode_table * block_size + inode_nr * sb.s_inode_size;
	struct ext2_inode inode;
	res = pread(img, &inode, sizeof(struct ext2_inode), inode_offset);
	if (res < 0) {
		return -errno;
	}


	size_t remained_bytes = inode.i_size;
	for (size_t i = 0; i < EXT2_NDIR_BLOCKS && remained_bytes > 0; ++i) {
		res = write_data_block(inode.i_block[i], block_size, img, out, remained_bytes);
		if (res < 0) {
			return -errno;
		}
		remained_bytes -= res;
	}
	if (remained_bytes <= 0) {
		return 0;
	}

	uint32_t* redir_1 = malloc(block_size);
	res = pread(img, redir_1, block_size, block_size * inode.i_block[EXT2_IND_BLOCK]);
	if (res < 0) {
		return -errno;
	}
	size_t MAX_REDIRECT_BLOCKS = block_size / sizeof(uint32_t);
	for (size_t i = 0; i < MAX_REDIRECT_BLOCKS && remained_bytes > 0; ++i) {
		res = write_data_block(redir_1[i], block_size, img, out, remained_bytes);
		if (res < 0) {
			free(redir_1);
			return -errno;
		}
		remained_bytes -= res;
	}
	if (remained_bytes <= 0) {
		free(redir_1);
		return 0;
	}


	res = pread(img, redir_1, block_size, inode.i_block[EXT2_DIND_BLOCK] * block_size);
	if (res < 0) {
		return -errno;
	}
	uint32_t* redir_2 = malloc(block_size);
	for (size_t i = 0; i < MAX_REDIRECT_BLOCKS && remained_bytes > 0; ++i) {
		res = pread(img, redir_2, block_size, redir_1[i] * block_size);
		if (res < 0) {
			free(redir_1);
			free(redir_2);
			return -errno;
		}
		for (size_t j = 0; j < MAX_REDIRECT_BLOCKS && remained_bytes > 0; ++j) {
			res = write_data_block(redir_2[j], block_size, img, out, remained_bytes);
			if (res < 0) {
				free(redir_1);
				free(redir_2);
				return -errno;
			}
			remained_bytes -= res;
		}
	}
	free(redir_1);
	free(redir_2);
	// handle indirect blocks
	return 0;
}
