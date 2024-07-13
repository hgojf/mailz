#ifndef MAILZ_CONFIG_H
#define MAILZ_CONFIG_H
struct argv {
	size_t argc;
	char **argv;
};

enum edit_mode {
	EDIT_MODE_MANUAL,
	EDIT_MODE_VI,
};

struct ignore {
	enum {
		IGNORE_IGNORE, IGNORE_RETAIN,
	} type;

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

	struct argv reorder;
	struct ignore ignore;
};
#endif /* MAILZ_CONFIG_H */
