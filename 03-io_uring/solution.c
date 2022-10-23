#include <solution.h>

#include <liburing.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define IO_BLOCK_SIZE 3 // 1 << 18; // 256 KB in bytes

#define NUM_OF_READS 4
#define QUEUE_SIZE 4096

struct common_info {
	int in;
	int out;
	struct io_uring ring;
	size_t offset;
	int reads;
	int writes;
	bool end;
};

enum operation {
	READ,
	WRITE
};

struct io_data {
	enum operation op;
	size_t offset;
	int res;
	char data[IO_BLOCK_SIZE];
};

int send_read(struct common_info * info) {
	struct io_uring_sqe * sqe = io_uring_get_sqe(&info->ring);
	struct io_data * data = malloc(sizeof(struct io_data));
	data->op = READ;
	data->offset = info->offset;
	info->offset += IO_BLOCK_SIZE;
	io_uring_prep_read(sqe, info->in, data->data, IO_BLOCK_SIZE, data->offset);
	io_uring_sqe_set_data(sqe, data);
	++info->reads;
	io_uring_submit(&info->ring);
	return 0;
}
int send_write(struct common_info * info, struct io_data * data) {
	struct io_uring_sqe * sqe = io_uring_get_sqe(&info->ring);
	data->op = WRITE;
	io_uring_prep_write(sqe, info->out, data->data, data->res, data->offset);
	io_uring_sqe_set_data(sqe, data);
	++info->writes;
	io_uring_submit(&info->ring);
	return 0;
}

struct io_data * wait_operation(struct common_info * info) {
	struct io_uring_cqe * cqe;
	io_uring_wait_cqe(&info->ring, &cqe);
	struct io_data * data = io_uring_cqe_get_data(cqe);
	data->res = cqe->res;
	io_uring_cqe_seen(&info->ring, cqe);
	return data;
}

int handle_read(struct common_info * info, struct io_data * data) {
	--info->reads;
	if (data->res < 0) {
		return data->res;
	}
	if (data->res == 0) {
		info->end = true;
		return 0;
	}
	send_write(info, data);
	if (!info->end) {
		send_read(info);
	}
	return 0;
}
int handle_write(struct common_info * info, struct io_data * data) {
	--info->writes;
	int res = data->res;
	free(data);
	return res;
}

int copy(int in, int out)
{
	(void) in;
	(void) out;


	struct common_info info;
	memset(&info, 0, sizeof(info));
	info.in = in;
	info.out = out;
	info.end = false;
	io_uring_queue_init(QUEUE_SIZE, &info.ring, 0);

	for (size_t i = 0; i < NUM_OF_READS; ++i) {
		send_read(&info);
	}
	while (info.reads > 0 || info.writes > 0) {
		struct io_data * data = wait_operation(&info);
		int res = (data->op == READ ? handle_read(&info, data) : handle_write(&info, data));
		if (res < 0) {
			io_uring_queue_exit(&info.ring);
			return res;
		}
	}
	io_uring_queue_exit(&info.ring);
	return 0;
}
