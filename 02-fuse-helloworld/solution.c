#include <solution.h>

#include <fuse.h>

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>

const char* hello_file_path = "/hello";
const char* hello_content_format = "hello, %d\n";

int hello_getattr(const char* path, struct stat* st, struct fuse_file_info* fi) {
	(void)fi;
	memset(st, 0, sizeof(struct stat));
	if (strcmp(path, "/") == 0) {
		st->st_mode = S_IFDIR | 0755;
		st->st_nlink = 2;
		return 0;
	}
	if (strcmp(path, hello_file_path) == 0) {
		st->st_mode = S_IFREG | 0400;
		st->st_nlink = 1;
		st->st_size = 20;
		return 0;
	}
	return -ENOENT;
}

int hello_readlink(const char* path, char* buf, size_t size) {
	(void)buf; (void)size;
	if (strcmp(path, "/") || strcmp(path, hello_file_path)) {
		return -EINVAL;
	}
	return -ENOENT;
}

int hello_mknod(const char* path, mode_t mode, dev_t fl) {
	(void)path; (void)mode; (void)fl;
	return -EROFS;
}

int hello_mkdir(const char* path, mode_t mode) {
	(void)path; (void)mode;
	return -EROFS;
}

int hello_unlink(const char* path) {
	(void)path;
	return -EROFS;
}

int hello_symlink(const char* target, const char* linkpath) {
	(void)target; (void)linkpath;
	return -EROFS;
}

int hello_rename(const char* oldpath, const char* newpath, unsigned int fl) {
	(void)oldpath; (void)newpath; (void)fl;
	return -EROFS;
}

int hello_link(const char* target, const char* linkpath) {
	(void)target; (void)linkpath;
	return -EROFS;
}

int hello_chmod(const char* path, mode_t mode, struct fuse_file_info* fi) {
	(void)path; (void)mode; (void)fi;
	return -EROFS;
}

int hello_chown(const char* path, uid_t uid, gid_t gid, struct fuse_file_info* fi) {
	(void)path; (void)uid; (void)gid; (void)fi;
	return -EROFS;
}

int hello_truncate(const char* path, off_t size, struct fuse_file_info* fi) {
	(void)path; (void)size; (void)fi;
	return -EROFS;
}

int hello_open(const char* path, struct fuse_file_info* fi) {
	if (strcmp(path, hello_file_path) != 0)
		return -ENOENT;
	if ((fi->flags & O_ACCMODE) != O_RDONLY)
		return -EACCES;
	return 0;
}

int hello_read(const char* path, char* buf, size_t size, off_t offset,
		     struct fuse_file_info* fi) {
	(void)fi;
	if (strcmp(path, hello_file_path) != 0) {
		return -ENOENT;
	}
	char hello_content[20];
	memset(hello_content, 0, sizeof(hello_content));
	if (offset > (off_t)size) {
		return 0;
	}
	sprintf(hello_content, hello_content_format, fuse_get_context()->pid);
	size = (size <= strlen(hello_content) - offset ? size : strlen(hello_content) - offset);
	memcpy(buf, hello_content + offset, size);
	return size;
}

int hello_write(const char* path, const char* buf, size_t size, off_t offset,
		      struct fuse_file_info* fi) {
	(void)path; (void)buf; (void)size; (void)offset; (void)fi;
	return -EROFS;
}

int hello_readdir(const char* path, void* buf, fuse_fill_dir_t filler, off_t d_off,
			struct fuse_file_info* fi, enum fuse_readdir_flags fl) {
  (void)d_off; (void)fi; (void)fl;
	if (strcmp(path, "/") != 0) {
		return -ENOENT;
	}
	filler(buf, ".", NULL, 0, 0);
	filler(buf, "..", NULL, 0, 0);
	filler(buf, hello_file_path + 1, NULL, 0, 0);
	return 0;
}

void* hello_init(struct fuse_conn_info* conn, struct fuse_config* cfg) {
	(void)conn; (void)cfg;
	return NULL;
}

int hello_create(const char* path, mode_t mode, struct fuse_file_info* fi) {
	(void)path; (void)mode; (void)fi;
	return -EROFS;
}

static const struct fuse_operations hellofs_ops = {
	.getattr = hello_getattr,
	.readlink = hello_readlink,
	.mknod = hello_mknod,
	.mkdir = hello_mkdir,
	.unlink = hello_unlink,
	.rmdir = hello_unlink,
	.symlink = hello_symlink,
	.rename = hello_rename,
	.link = hello_link,
	.chmod = hello_chmod,
	.chown = hello_chown,
	.truncate = hello_truncate,
	.open = hello_open,
	.read = hello_read,
	.write = hello_write,
	.readdir = hello_readdir,
	.init = hello_init,
	.create = hello_create,
};

int helloworld(const char *mntp)
{
	char *argv[] = {"exercise", "-f", (char *)mntp, NULL};
	return fuse_main(3, argv, &hellofs_ops, NULL);
}
