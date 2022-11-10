#include <solution.h>

#include <assert.h>
#include <ext2fs/ext2fs.h>
#include <fcntl.h>
#include <linux/fs.h>
#include <string.h>
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
	inode_nr %= sb->s_inodes_per_group;
	size_t block_size = EXT2_BLOCK_SIZE(sb);

	int res = pread(img, bg, sizeof(struct ext2_group_desc), (1024 / block_size + 1) * block_size + sizeof(struct ext2_group_desc) * bg_number);
	return (res < 0 ? -errno : res);
}

int get_inode(struct ext2_inode* inode, int img, struct ext2_super_block* sb, size_t inode_nr) {
	struct ext2_group_desc bg;
	int res = read_bg_header(&bg, img, sb, inode_nr);
	if (res < 0) {
		return res;
	}
	off_t inode_offset = bg.bg_inode_table * EXT2_BLOCK_SIZE(sb) + (inode_nr - 1) * sb->s_inode_size;
	res = pread(img, inode, sizeof(struct ext2_inode), inode_offset);
	return (res < 0 ? -errno : res);
}

bool get_next_filename(const char* path, char* filename) {
	if (path[0] != '/') {
		return false;
	}
	path += 1;
	memset(filename, 0, EXT2_NAME_LEN + 1);
	const char* pos = strchr(path, '/');
	if (pos == NULL) {
		pos = path + strlen(path);
	}
	strncpy(filename, path, pos - path);
	return true;
}

int handle_dir_block(int img, size_t block_nr, long block_size, const char* file, int* remained_bytes) {
	char* block = malloc(block_size);
	if (pread(img, block, block_size, block_nr * block_size) < 0) {
		free(block);
		return -errno;
	}
	struct ext2_dir_entry_2* dir_entry = (struct ext2_dir_entry_2*)block;
	while ((char*)dir_entry - block < block_size && dir_entry->inode > 0) {
		if (strlen(file) == dir_entry->name_len && !strncmp(file, dir_entry->name, dir_entry->name_len)) {
			int inode_nr = dir_entry->inode;
			free(block);
			return inode_nr;
		}
		dir_entry = (struct ext2_dir_entry_2*)((char*)dir_entry + dir_entry->rec_len);
	}
	*remained_bytes -= block_size;
	free(block);
	return -ENOENT;
}

int handle_ind_block(int img, size_t block_nr, long block_size, const char* file, int* remained_bytes) {
	if (*remained_bytes <= 0) {
		return -ENOENT;
	}
	uint32_t* redir = malloc(block_size);
	int res = pread(img, redir, block_size, block_size * block_nr);
	if (res < 0) {
		free(redir);
		return -errno;
	}
	size_t MAX_REDIRECT_BLOCKS = block_size / sizeof(uint32_t);
	for (size_t i = 0; i < MAX_REDIRECT_BLOCKS && *remained_bytes > 0; ++i) {
		if ((res = handle_dir_block(img, redir[i], block_size, file, remained_bytes)) != -ENOENT) {
			free(redir);
			return res;
		}
	}
	free(redir);
	return -ENOENT;
}

int handle_d_ind_block(int img, size_t block_nr, long block_size, const char* file, int* remained_bytes) {
	if (*remained_bytes <= 0) {
		return -ENOENT;
	}
	uint32_t* redir = malloc(block_size);
	int res = pread(img, redir, block_size, block_size * block_nr);
	if (res < 0) {
		free(redir);
		return -errno;
	}
	size_t MAX_REDIRECT_BLOCKS = block_size / sizeof(uint32_t);
	for (size_t i = 0; i < MAX_REDIRECT_BLOCKS && *remained_bytes > 0; ++i) {
		if ((res = handle_ind_block(img, redir[i], block_size, file, remained_bytes)) != -ENOENT) {
			free(redir);
			return res;
		}
	}
	free(redir);
	return -ENOENT;
}

int find_inode(int img, struct ext2_super_block* sb, int inode_nr, const char* path) {
	int res;
	struct ext2_inode inode;
	int block_size = EXT2_BLOCK_SIZE(sb);
	char filename[EXT2_NAME_LEN + 1];
	while (get_next_filename(path, filename)) {
		path += strlen(filename) + 1;
		assert(*path == '\0' || *path == '/');
		if ((res = get_inode(&inode, img, sb, inode_nr)) < 0) {
			return res;
		}
		if (!S_ISDIR(inode.i_mode)) {
			assert(inode_nr > 0);
			assert(inode_nr != 2);
			return -ENOTDIR;
		}
		int remained_bytes = inode.i_size;
		bool found = false;
		for (size_t i = 0; i < EXT2_NDIR_BLOCKS && remained_bytes > 0; ++i) {
			if ((res = handle_dir_block(img, inode.i_block[i], block_size, filename, &remained_bytes)) >= 0) {
				inode_nr = res;
				assert(inode_nr > 2);
				found = true;
				break;
			} else if (res != -ENOENT) {
				return res;
			}
		}
		if (found) { continue; }
		if ((res = handle_ind_block(img, inode.i_block[EXT2_IND_BLOCK], block_size, filename, &remained_bytes)) >= 0) {
			inode_nr = res;
			continue;
		} else if (res != -ENOENT) {
			return res;
		}
		if ((res = handle_d_ind_block(img, inode.i_block[EXT2_DIND_BLOCK], block_size, filename, &remained_bytes)) >= 0) {
			inode_nr = res;
			continue;
		} else if (res != -ENOENT) {
			return res;
		}
		
		return -ENOENT;
	}
	return inode_nr;
}

int write_data_block(uint32_t block_nr, uint32_t block_size, int img, int out, size_t file_size) {
	int size = min(file_size, block_size);
	void* block = malloc(size);
	int res = pread(img, block, size, block_nr * block_size);
	if (res < 0) {
		free(block);
		return -errno;
	}
	res = write(out, block, size);
	free(block);
	return (res < 0 ? -errno : size);
}

int copy_file(int img, struct ext2_super_block* sb, int inode_nr, int out) {
	int block_size = EXT2_BLOCK_SIZE(sb);
	struct ext2_inode inode;
	int res = get_inode(&inode, img, sb, inode_nr);
	if (res < 0) {
		return res;
	}
	size_t remained_bytes = inode.i_size;
	for (size_t i = 0; i < EXT2_NDIR_BLOCKS && remained_bytes > 0; ++i) {
		if ((res = write_data_block(inode.i_block[i], block_size, img, out, remained_bytes)) < 0) {
			return res;
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
	return 0;
}

int dump_file(int img, const char *path, int out)
{
	int res;
	struct ext2_super_block sb;
	if ((res = read_sb(&sb, img)) < 0) {
		return res;
	}
	if ((res = find_inode(img, &sb, 2, path)) < 0) {
		assert(res != -ENOTDIR);
		return res;
	}
	if ((res = copy_file(img, &sb, res, out)) < 0) {
	  return res;
	}
	return 0;
}
