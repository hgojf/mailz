#ifndef MAILZ_CONFIG_H
#define MAILZ_CONFIG_H
struct reorder {
	struct argv argv;
	struct argv_shm shm;
};

struct ignore {
	enum {
		IGNORE_IGNORE, IGNORE_RETAIN,
	} type;

	struct argv_shm shm;
	struct argv argv;
};

struct address {
	char *addr;
	char *name;
};

struct config {
	struct address address;

	enum edit_mode edit_mode;

	int cache;

	struct reorder reorder;
	struct ignore ignore;
};
#endif /* MAILZ_CONFIG_H */
