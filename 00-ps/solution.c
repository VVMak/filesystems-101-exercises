#include <solution.h>
#include <fs_malloc.h>

#include <assert.h>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const size_t argv_pos = 48; // numeration from 1

bool is_pid(char* str, pid_t* pid) {
	*pid = 0;
	for (size_t i = 0; str[i] != '\0'; ++i) {
		if (!isdigit(str[i])) {
			return false;
		}
		*pid = *pid * 10 + str[i] - '0';
	}
	return true;
}

char** read_string_file(char* path, char* buf, size_t* size) {
	int fd = open(path, O_RDONLY);
	if (fd == -1) {
		return NULL;
	}

	*size = 0;
	for (size_t bytes_read = 0; (bytes_read = read(fd, buf + *size, sysconf(_SC_ARG_MAX) - *size)); *size += bytes_read) {}

	size_t strs_count = 0; // for NULL
	for (size_t i = 0; i < *size; ++i) {
		strs_count += (buf[i] == '\0');
	}

	char** ptrs = fs_xmalloc(sizeof(char*) * (strs_count + 1)); // + NULL
	ptrs[0] = buf;
	for (size_t i = 0, k = 1; i < *size; ++i, ++k) {
		for (; buf[i] != '\0' && i < *size; ++i) {}
		ptrs[k] = buf + i + 1;
	}
	ptrs[strs_count] = NULL;

	close(fd);
	return ptrs;
}

void ps(void) {
	DIR* proc = opendir("/proc");
	if (proc == NULL) {
		report_error("/proc", errno);
		return;
	}
	char* path = fs_xmalloc(PATH_MAX);
	char* exe = fs_xmalloc(PATH_MAX);
	char* buf = fs_xmalloc(sysconf(_SC_ARG_MAX));
	struct dirent* file;
	while ((file = readdir(proc))) {
		char* filename = file->d_name;
		pid_t pid;
		if (!is_pid(filename, &pid)) {
			continue;
		}
		sprintf(path, "/proc/%s/exe", filename);
		memset(exe, '\0', PATH_MAX);
		if (readlink(path, exe, PATH_MAX) == -1) {
			report_error(path, errno);
			continue;
		}

		sprintf(path, "/proc/%s/cmdline", filename);
		char** argv;
		size_t size = 0;
		if (!(argv = read_string_file(path, buf, &size))) {
			report_error(path, errno);
			continue;
		}

		char** envp;
		sprintf(path, "/proc/%s/environ", filename);
		if (!(envp = read_string_file(path, buf + size, &size))) {
			report_error(path, errno);
			fs_xfree(argv);
			continue;
		}
		report_process(pid, exe, argv, envp);
		fs_xfree((void*)argv);
		fs_xfree((void*)envp);
	}
	fs_xfree(path);
	fs_xfree(exe);
	fs_xfree(buf);
	closedir(proc);
}
