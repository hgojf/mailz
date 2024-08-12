#ifndef MAILZ_CONF_H
#define MAILZ_CONF_H
struct address {
	char *addr;
	char *name;
};

struct ignore {
	size_t argc;
	char **argv;
	#define IGNORE_IGNORE 0
	#define IGNORE_RETAIN 1
	#define IGNORE_ALL 3
	int type;
};

struct reorder {
	size_t argc;
	char **argv;
};

struct mailz_conf {
	struct address address;
	struct ignore ignore;
	struct reorder reorder;
	int cache;
};

int configure(struct mailz_conf *);
void config_free(struct mailz_conf *);
#endif /* MAILZ_CONF_H */
