#ifndef MAILZ_CONF_H
#define MAILZ_CONF_H
struct address {
	char *addr;
	char *name;
};

struct cache_conf {
	size_t min; /* letters */
	long long max; /* bytes */
	int enabled;
};

struct ignore {
	size_t argc;
	char **argv;
	enum {
		IGNORE_IGNORE,
		IGNORE_RETAIN,
		IGNORE_ALL,
	} type;
};

struct reorder {
	size_t argc;
	char **argv;
};

struct mailz_conf {
	struct address address;
	struct cache_conf cache;
	struct ignore ignore;
	struct reorder reorder;
	int linewrap;
};

void config_default(struct mailz_conf *);
FILE *config_file(void);
void config_free(struct mailz_conf *);
int config_init(struct mailz_conf *, FILE *, const char *);
#endif /* MAILZ_CONF_H */
