#ifndef MAILZ_ARGV_H
#define MAILZ_ARGV_H
struct argv {
	long long argc;
	char **argv;
};

struct argv_shm {
	int fd;
	long long sz;
};

struct argv_mapped {
	long long sz;
	void *p;
};
#endif /* MAILZ_ARGV_H */
