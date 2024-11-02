#ifndef MAILZ_CONF_H
#define MAILZ_CONF_H
struct mailz_conf {
	char address[255];
	struct mailz_ignore {
		char **headers;
		size_t nheader;
		#define MAILZ_IGNORE_IGNORE 0
		#define MAILZ_IGNORE_RETAIN 1
		int type;
	} ignore;
};

void mailz_conf_free(struct mailz_conf *);
int mailz_conf_init(struct mailz_conf *);
#endif /* MAILZ_CONF_H */
