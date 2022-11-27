#include <solution.h>

#include <fuse.h>


#include <assert.h>
#include <ext2fs/ext2fs.h>
#include <fcntl.h>
#include <linux/fs.h>
#include <string.h>
#include <stdbool.h>
#include <sys/stat.h>
#include <unistd.h>

int img;

inline long min(long first, long second) {
	return (first < second ? first : second);
}

inline long max(long first, long second) {
	return (first > second ? first : second);
}


int read_sb(struct ext2_super_block* sb) {
	int res = pread(img, sb, sizeof(struct ext2_super_block), 1024);
	if (res < 0) {
		return -errno;
	}
	if (EXT2_SUPER_MAGIC != sb->s_magic) {
		return -EINVAL;
	}
	return 0;
}

int read_bg_header(struct ext2_group_desc* bg, struct ext2_super_block* sb, size_t inode_nr) {
	--inode_nr;
	size_t bg_number = inode_nr / sb->s_inodes_per_group;
	size_t block_size = EXT2_BLOCK_SIZE(sb);

	int res = pread(img, bg, sizeof(struct ext2_group_desc), (sb->s_first_data_block + 1) * block_size + sizeof(struct ext2_group_desc) * bg_number);
	return (res < 0 ? -errno : res);
}

int get_inode(struct ext2_inode* inode, struct ext2_super_block* sb, long inode_nr) {
	assert(inode_nr > 0);
	struct ext2_group_desc bg;
	int res = read_bg_header(&bg, sb, inode_nr);
	--inode_nr;
	if (res < 0) {
		return res;
	}
	inode_nr %= sb->s_inodes_per_group;
	off_t inode_offset = bg.bg_inode_table * EXT2_BLOCK_SIZE(sb) + inode_nr * sb->s_inode_size;
	res = pread(img, inode, sizeof(struct ext2_inode), inode_offset);
	return (res < 0 ? -errno : res);
}

const char* get_next_filename(const char* path) {
	path += 1;
	const char* pos = strchr(path, '/');
	if (pos == NULL) {
		pos = path + strlen(path);
	}
	return pos;
}


int handle_dir_block(uint32_t block_nr, struct ext2_super_block* sb, int* remained, int (*op_func)(char*, int, void*), void* op_info) {
	if (block_nr == 0) {
		return 1;
	}
  const int block_size = EXT2_BLOCK_SIZE(sb);
	const int size = min(*remained, block_size);
	char* block = malloc(size);
	if (pread(img, block, size, block_nr * block_size) < 0) {
		free(block);
		return -errno;
	}
	int res = (*op_func)(block, size, op_info);
	*remained -= size;
	free(block);
	return res;
}

int handle_ind_block(uint32_t block_nr, struct ext2_super_block* sb, int* remained, int (*op_func)(char*, int, void*), void* op_info) {
	if (block_nr == 0) {
		return 1;
	}
	const int block_size = EXT2_BLOCK_SIZE(sb);
	uint32_t* redir = malloc(block_size);
	int res = pread(img, redir, block_size, block_size * block_nr);
	if (res < 0) {
		free(redir);
		return -errno;
	}
	size_t MAX_REDIRECT_BLOCKS = block_size / sizeof(uint32_t);
	for (size_t i = 0; i < MAX_REDIRECT_BLOCKS; ++i) {
		if ((res = handle_dir_block(redir[i], sb, remained, op_func, op_info)) != 0) {
			free(redir);
			return res;
		}
	}
	free(redir);
	return 0;
}

int handle_d_ind_block(uint32_t block_nr, struct ext2_super_block* sb, int* remained, int (*op_func)(char*, int, void*), void* op_info) {
	if (block_nr == 0) {
		return 1;
	}
	const int block_size = EXT2_BLOCK_SIZE(sb);
	uint32_t* redir = malloc(block_size);
	int res = pread(img, redir, block_size, block_size * block_nr);
	if (res < 0) {
		free(redir);
		return -errno;
	}
	size_t MAX_REDIRECT_BLOCKS = block_size / sizeof(uint32_t);
	for (size_t i = 0; i < MAX_REDIRECT_BLOCKS; ++i) {
		if ((res = handle_ind_block(redir[i], sb, remained, op_func, op_info)) != 0) {
			free(redir);
			return res;
		}
	}
	free(redir);
	return 0;
}


struct find_inode_info {
	const char* path;
	struct ext2_super_block* sb;
	int inode_nr;
};

int find_inode(struct find_inode_info* info);

int block_find_inode(char* buf, int buf_size, void* find_inode_info) {
	struct find_inode_info* info = find_inode_info;
	struct ext2_dir_entry_2* dir_entry = (struct ext2_dir_entry_2*)buf;
	while ((char*)dir_entry - buf < buf_size) {
		if (dir_entry->inode == 0) {
			info->inode_nr = 0;
			return -ENOENT;
		}
		const char* pos = get_next_filename(info->path);
    if (pos - info->path == dir_entry->name_len && !strncmp(info->path, dir_entry->name, dir_entry->name_len)) {
      long inode_nr = dir_entry->inode;
			if (pos[0] != '/') {
				info->inode_nr = inode_nr;
				return 1;
			}
			if (dir_entry->file_type == EXT2_FT_DIR) {
				info->path = pos;
				info->inode_nr = inode_nr;
				return find_inode(info);
			}
			return -ENOTDIR;
    }
		dir_entry = (struct ext2_dir_entry_2*)((char*)dir_entry + dir_entry->rec_len);
	}
	return 0;
}

int find_inode(struct find_inode_info* info) {
	if (info->inode_nr == 0) {
		return -ENOENT;
	}
	if (info->path[0] != '/') {
		return 0;
	}
	info->path += 1;
	long res;
	struct ext2_inode inode;
	if ((res = get_inode(&inode, info->sb, info->inode_nr)) < 0) {
		return res;
	}
	int remained = inode.i_size;
	for (size_t i = 0; i < EXT2_NDIR_BLOCKS; ++i) {
		if ((res = handle_dir_block(inode.i_block[i], info->sb, &remained, block_find_inode, info)) != 0) {
			return res;
		}
	}
	if ((res = handle_ind_block(inode.i_block[EXT2_IND_BLOCK], info->sb, &remained, block_find_inode, info)) != 0) {
		return res;
	}
	if ((res = handle_d_ind_block(inode.i_block[EXT2_DIND_BLOCK], info->sb, &remained, block_find_inode, info)) != 0) {
		return res;
	}
	return -ENOENT;
};


int my_getattr(const char* path, struct stat* st, struct fuse_file_info* fi) {
	(void)fi;
	// printf("getattr %s\n", path); fflush(stdout);
	memset(st, 0, sizeof(struct stat));
	if (strcmp(path, "/") == 0) {
		st->st_mode = S_IFDIR | 0755;
		st->st_nlink = 2;
		// printf("out in %d with 0\n", __LINE__); fflush(stdout);
		return 0;
	}
	struct ext2_super_block sb;
	int res = read_sb(&sb);
	if (res < 0) { /* printf("out in %d with %d\n", __LINE__, res); fflush(stdout); */ return res; }
	struct find_inode_info find_info = {.path = path, .inode_nr = 2, .sb = &sb};
	if ((res = find_inode(&find_info)) < 0) { /* printf("%s\n", path);  printf("out in %d with %d\n", __LINE__, res); fflush(stdout); */ return res; }
	struct ext2_inode inode;
	if ((res = get_inode(&inode, &sb, find_info.inode_nr)) < 0) { /* printf("out in %d with %d\n", __LINE__, res); fflush(stdout); */ return res; }
	// st->st_mode = 0777;
	// st->st_mode = ((~st->st_mode) & inode.i_mode) | S_IRUSR;
	st->st_mode = inode.i_mode;
	st->st_nlink = inode.i_links_count;
	st->st_size = inode.i_size;
	// printf("out in %d with %d\n", __LINE__, 0); fflush(stdout);
	return 0;
}


int my_open(const char* path, struct fuse_file_info* fi) {
	/* printf("open %s\n", path); fflush(stdout); */
	if ((fi->flags & O_ACCMODE) != O_RDONLY) { /* printf("out in %d with %d\n", __LINE__, -EROFS); fflush(stdout); */ return -EROFS; }
	struct ext2_super_block sb;
	int res = read_sb(&sb);
	if (res < 0) { /* printf("out in %d with %d\n", __LINE__, res); fflush(stdout); */ return res; }
	struct find_inode_info find_info = {.path = path, .inode_nr = 2, .sb = &sb};
	if ((res = find_inode(&find_info)) < 0) { /* printf("out in %d with %d\n", __LINE__, res); fflush(stdout); */ return res; }
	fi->fh = find_info.inode_nr;
	/* printf("out in %d with %d\n", __LINE__, 0); fflush(stdout); */
	return 0;
}


struct read_info {
	struct ext2_super_block* sb;
	char* buf;
	off_t offset;
	size_t size;
	int result;
};

int read_block(char* buf, int buf_size, void* read_block_info) {
	struct read_info* info = read_block_info;
	off_t offset = info->offset;
	const int size = min(info->size, buf_size);
	// printf("%ld %d\n", offset, size); fflush(stdout);
	if (offset >= buf_size) {
		return 0;
	}
	if (size <= 0) {
		return 1;
	}
	memcpy(info->buf, buf + offset, size);
	info->offset = 0;
	info->size -= size;
	info->buf += size;
	info->result += size;
	return 0;
}

int read_inode(struct ext2_inode* inode, struct read_info* info) {
	int remained = inode->i_size;
	int res;
	info->result = 0;
	for (size_t i = 0; i < EXT2_NDIR_BLOCKS; ++i) {
		if ((res = handle_dir_block(inode->i_block[i], info->sb, &remained, read_block, info)) != 0) {
			return res;
		}
	}
	if ((res = handle_ind_block(inode->i_block[EXT2_IND_BLOCK], info->sb, &remained, read_block, info)) != 0) {
		return res;
	}
	if ((res = handle_d_ind_block(inode->i_block[EXT2_DIND_BLOCK], info->sb, &remained, read_block, info)) != 0) {
		return res;
	}
	return 0;
}

int my_read(const char* path, char* buf, size_t size, off_t offset,
		     struct fuse_file_info* fi) {
	// printf("read %s\n", path); fflush(stdout);
	(void)path;
	struct ext2_super_block sb;
	int res = read_sb(&sb);
	struct ext2_inode inode;
	if ((res = get_inode(&inode, &sb, fi->fh)) < 0) { /* printf("out in %d with %d\n", __LINE__, res); fflush(stdout); */ return res; }
	struct read_info read_info = {.buf = buf, .size = size, .offset = offset, .sb = &sb};
	// printf("NODE: %ld\n", fi->fh); fflush(stdout);
	if ((res = read_inode(&inode, &read_info)) < 0) { /* printf("out in %d with %d\n", __LINE__, res); fflush(stdout); */ return res; }
	// printf("out in %d with %d\n", __LINE__, read_info.result); fflush(stdout);
	return read_info.result;
}


int my_opendir(const char* path, struct fuse_file_info* fi)
{
	// printf("opendir %s\n", path); fflush(stdout);
	if ((fi->flags & O_ACCMODE) != O_RDONLY) { /* printf("out in %d with %d\n", __LINE__, -EROFS); fflush(stdout); */ return -EROFS; }
	if (!strcmp(path, "/")) {
		fi->fh = 2;
		/* printf("out in %d with %d\n", __LINE__, 0); fflush(stdout);  */
		return 0;
	}
	struct ext2_super_block sb;
	int res = read_sb(&sb);
	if (res < 0) { /* printf("out in %d with %d\n", __LINE__, res); fflush(stdout); */ return res; }
	struct find_inode_info find_info = {.path = path, .inode_nr = 2, .sb = &sb};
	if ((res = find_inode(&find_info)) < 0) { /* printf("out in %d with %d\n", __LINE__, res); fflush(stdout); */ return res; }
	fi->fh = find_info.inode_nr;
	struct ext2_inode inode;
	if ((res = get_inode(&inode, &sb, fi->fh)) < 0) { printf("out in %d with %d\n", __LINE__, res); fflush(stdout); return res; }
	if (!S_ISDIR(inode.i_mode)) {
		// printf("out in %d with %d\n", __LINE__, -ENOTDIR); fflush(stdout); 
		return -ENOTDIR;
	}
	// printf("out in %d with %d\n", __LINE__, 0); fflush(stdout);
	return 0;
}


struct readdir_info {
	fuse_fill_dir_t filler;
	struct ext2_super_block* sb;
	void* buf;
};

int readdir_block(char* buf, int buf_size, void* readdir_block_info) {
	struct readdir_info* info = readdir_block_info;
	struct ext2_dir_entry_2* dir_entry = (struct ext2_dir_entry_2*)buf;
	while ((char*)dir_entry - buf < buf_size) {
		if (dir_entry->inode == 0) {
			return 1;
		}
		char* filename = malloc(dir_entry->name_len + 1);
		memcpy(filename, dir_entry->name, dir_entry->name_len);
		filename[dir_entry->name_len] = '\0';
		struct stat st;
		memset(&st, 0, sizeof(st));
		st.st_ino = dir_entry->inode;
		if (dir_entry->file_type == EXT2_FT_DIR) {
			st.st_mode = S_IFDIR | S_IRUSR | S_IRGRP | S_IROTH;
		} else {
			st.st_mode = S_IFREG | S_IRUSR | S_IXUSR | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH;
		}
		info->filler(info->buf, filename, &st, 0, 0);
		dir_entry = (struct ext2_dir_entry_2*)((char*)dir_entry + dir_entry->rec_len);
	}
	return 0;
}

int readdir_inode(struct ext2_inode* inode, struct readdir_info* info) {
	long res;
	int remained = inode->i_size;
	for (size_t i = 0; i < EXT2_NDIR_BLOCKS; ++i) {
		if ((res = handle_dir_block(inode->i_block[i], info->sb, &remained, readdir_block, info)) != 0) {
			return res;
		}
	}
	if ((res = handle_ind_block(inode->i_block[EXT2_IND_BLOCK], info->sb, &remained, readdir_block, info)) != 0) {
		return res;
	}
	if ((res = handle_d_ind_block(inode->i_block[EXT2_DIND_BLOCK], info->sb, &remained, readdir_block, info)) != 0) {
		return res;
	}
	return 0;
}

int my_readdir(const char* path, void* buf, fuse_fill_dir_t filler, off_t d_off,
			struct fuse_file_info* fi, enum fuse_readdir_flags fl) {
  (void)path; (void)d_off; (void)fl;
	// printf("readdir %s\n", path); fflush(stdout);
	struct ext2_super_block sb;
	int res = read_sb(&sb);
	if (res < 0) { /* printf("out in %d with %d\n", __LINE__, res); fflush(stdout); */ return res; }
	struct ext2_inode inode;
	if ((res = get_inode(&inode, &sb, fi->fh)) < 0) { /* printf("out in %d with %d\n", __LINE__, res); fflush(stdout); */ return res; }
	struct readdir_info info = {.filler = filler, .sb = &sb, .buf = buf};
	if ((res = readdir_inode(&inode, &info)) < 0) { /* printf("out in %d with %d\n", __LINE__, res); fflush(stdout); */ return res; }
	// printf("out in %d with %d\n", __LINE__, 0); fflush(stdout);
	return 0;
}


int my_readlink(const char* path, char* buf, size_t size) {
	(void) path; (void)buf; (void)size;
	return -ENOENT;
}

int my_mknod(const char* path, mode_t mode, dev_t fl) {
	(void)path; (void)mode; (void)fl;
	return -EROFS;
}

int my_mkdir(const char* path, mode_t mode) {
	(void)path; (void)mode;
	return -EROFS;
}

int my_unlink(const char* path) {
	(void)path;
	return -EROFS;
}

int my_symlink(const char* target, const char* linkpath) {
	(void)target; (void)linkpath;
	return -EROFS;
}

int my_rename(const char* oldpath, const char* newpath, unsigned int fl) {
	(void)oldpath; (void)newpath; (void)fl;
	return -EROFS;
}

int my_link(const char* target, const char* linkpath) {
	(void)target; (void)linkpath;
	return -EROFS;
}

int my_chmod(const char* path, mode_t mode, struct fuse_file_info* fi) {
	(void)path; (void)mode; (void)fi;
	return -EROFS;
}

int my_chown(const char* path, uid_t uid, gid_t gid, struct fuse_file_info* fi) {
	(void)path; (void)uid; (void)gid; (void)fi;
	return -EROFS;
}

int my_truncate(const char* path, off_t size, struct fuse_file_info* fi) {
	(void)path; (void)size; (void)fi;
	return -EROFS;
}

int my_write(const char* path, const char* buf, size_t size, off_t offset,
		      struct fuse_file_info* fi) {
	(void)path; (void)buf; (void)size; (void)offset; (void)fi;
	return -EROFS;
}

void* my_init(struct fuse_conn_info* conn, struct fuse_config* cfg) {
	(void)conn; (void)cfg;
	return NULL;
}

int my_create(const char* path, mode_t mode, struct fuse_file_info* fi) {
	(void)path; (void)mode; (void)fi;
	return -EROFS;
}


static const struct fuse_operations ext2_ops = {
	.getattr = my_getattr,
	.readlink = my_readlink,
	.mknod = my_mknod,
	.mkdir = my_mkdir,
	.unlink = my_unlink,
	.symlink = my_symlink,
	.rename = my_rename,
	.link = my_link,
	.chmod = my_chmod,
	.chown = my_chown,
	.truncate = my_truncate,
	.open = my_open,
	.read = my_read,
	.write = my_write,
	.opendir = my_opendir,
	.readdir = my_readdir,
	.init = my_init,
	.create = my_create,
};

int ext2fuse(int ext2_img, const char *mntp)
{
	img = ext2_img;

	char *argv[] = {"exercise", "-f", (char *)mntp, NULL};
	return fuse_main(3, argv, &ext2_ops, NULL);
}
