#include <solution.h>

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fs_malloc.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

// FOR EXPERIMENT

bool is_number(char* str) {
	for (size_t i = 0; str[i] != '\0'; ++i) {
		if (!isdigit(str[i])) {
			return false;
		}
	}
	return true;
}

void lsof(void)
{
	DIR* proc_dir = opendir("/proc");
	if (proc_dir == NULL) {
		report_error("/proc", errno);
		return;
	}
	char* fd_dir_path = fs_xmalloc(PATH_MAX);
	char* link_path = fs_xmalloc(PATH_MAX);
	char* link_target = fs_xmalloc(PATH_MAX);
	struct dirent* pid_dirent;
	while ((pid_dirent = readdir(proc_dir))) {
		char* pid_dir_name = pid_dirent->d_name;
		if (!is_number(pid_dir_name)) {
			continue;
		}
		sprintf(fd_dir_path, "/proc/%s/fd", pid_dir_name);
		
		DIR* fd_dir = opendir(fd_dir_path);
		if (fd_dir == NULL) {
			report_error(fd_dir_path, errno);
			continue;
		}
		struct dirent* fd_dirent;
		while ((fd_dirent = readdir(fd_dir)))
		{
			if (!is_number(fd_dirent->d_name)) {
				continue;
			}
			sprintf(link_path, "/proc/%s/fd/%s", pid_dir_name, fd_dirent->d_name);
			memset(link_target, '\0', PATH_MAX);
			if (readlink(link_path, link_target, PATH_MAX) == -1) {
				report_error(link_path, errno);
				continue;
			}
			report_file(link_target);
		}
		closedir(fd_dir);
	}
	fs_xfree(fd_dir_path);
	fs_xfree(link_path);
	fs_xfree(link_target);
	closedir(proc_dir);
}
