#include <solution.h>

#include <assert.h>
#include <ext2fs/ext2fs.h>
#include <fcntl.h>
#include <linux/fs.h>
#include <stdbool.h>
#include <sys/stat.h>
#include <unistd.h>


inline size_t min(size_t first, size_t second) {
	return (first < second ? first : second);
}

int read_sb(struct ext2_super_block* sb, int img) {
	int res = pread(img, sb, sizeof(struct ext2_super_block), 1024);
	if (res < 0) {
		return -errno;
	}
	if (EXT2_SUPER_MAGIC != sb->s_magic) {
		return -EINVAL;
	}
	return 0;
}

int read_bg_header(struct ext2_group_desc* bg, int img, struct ext2_super_block* sb, size_t inode_nr) {
	--inode_nr;
	size_t bg_number = inode_nr / sb->s_inodes_per_group;
	size_t block_size = EXT2_BLOCK_SIZE(sb);

	int res = pread(img, bg, sizeof(struct ext2_group_desc), (sb->s_first_data_block + 1) * block_size + sizeof(struct ext2_group_desc) * bg_number);
	return (res < 0 ? -errno : res);
}

int get_inode(struct ext2_inode* inode, int img, struct ext2_super_block* sb, long inode_nr) {
	assert(inode_nr > 0);
	struct ext2_group_desc bg;
	int res = read_bg_header(&bg, img, sb, inode_nr);
	--inode_nr;
	if (res < 0) {
		return res;
	}
	inode_nr %= sb->s_inodes_per_group;
	off_t inode_offset = bg.bg_inode_table * EXT2_BLOCK_SIZE(sb) + inode_nr * sb->s_inode_size;
	res = pread(img, inode, sizeof(struct ext2_inode), inode_offset);
	return (res < 0 ? -errno : res);
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
	return (res < 0 ? -errno : size);
}

int handle_direct_block(size_t inode_nr, struct ext2_super_block* sb, int img, int out, int* remained_size) {
	if (*remained_size <= 0) {
		return 0;
	}
  const int block_size = EXT2_BLOCK_SIZE(sb);
	int size = min(*remained_size, block_size);
	*remained_size -= size;
	int res;
	if (inode_nr == 0) {
		char* filler = malloc(size);
		memset(filler, 0, size);
		res = (write(out, filler, size) < 0 ? -errno : 0);
		free(filler);
	} else {
		res = write_data_block(inode_nr, block_size, img, out, size);
	}
	return (res < 0 ? res : (*remained_size != 0));
}

int handle_ind_block(size_t inode_nr, struct ext2_super_block* sb, int img, int out, int* remained_size) {
	size_t block_size = EXT2_BLOCK_SIZE(sb);
	uint32_t* redir = malloc(block_size);
	int res = pread(img, redir, block_size, block_size * inode_nr);
	if (res < 0) {
		return -errno;
	}
	size_t MAX_REDIRECT_BLOCKS = block_size / sizeof(uint32_t);
	for (size_t i = 0; i < MAX_REDIRECT_BLOCKS && *remained_size > 0; ++i) {
		if ((res = handle_direct_block(redir[i], sb, img, out, remained_size)) <= 0) {
			return res;
		}
	}
	free(redir);
	return 1;
}

int handle_d_ind_block(size_t inode_nr, struct ext2_super_block* sb, int img, int out, int* remained_size) {
	size_t block_size = EXT2_BLOCK_SIZE(sb);
	uint32_t* redir = malloc(block_size);
	int res = pread(img, redir, block_size, block_size * inode_nr);
	if (res < 0) {
		return -errno;
	}
	size_t MAX_REDIRECT_BLOCKS = block_size / sizeof(uint32_t);
	for (size_t i = 0; i < MAX_REDIRECT_BLOCKS && *remained_size > 0; ++i) {
		if ((res = handle_ind_block(redir[i], sb, img, out, remained_size)) <= 0) {
			return res;
		}
	}
	free(redir);
	return 1;
}

int dump_file(int img, int inode_nr, int out)
{
	(void) img;
	(void) inode_nr;
	(void) out;

	struct ext2_super_block sb;
	int res = read_sb(&sb, img);
	if (res < 0) {
		return res;
	}

	struct ext2_inode inode;
	if ((res = get_inode(&inode, img, &sb, inode_nr)) < 0) {
		return res;
	}

	int remained_bytes = inode.i_size;
	for (size_t i = 0; i < EXT2_NDIR_BLOCKS && remained_bytes > 0; ++i) {
		if ((res = handle_direct_block(inode.i_block[i], &sb, img, out, &remained_bytes)) <= 0) {
			return res;
		}
	}
	if ((res = handle_ind_block(inode.i_block[EXT2_IND_BLOCK], &sb, img, out, &remained_bytes)) <= 0) {
		return res;
	}
	return handle_d_ind_block(inode.i_block[EXT2_DIND_BLOCK], &sb, img, out, &remained_bytes);
}
