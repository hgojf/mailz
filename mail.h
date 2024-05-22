#ifndef MAILZ_MAIL_H
#define MAILZ_MAIL_H
struct header {
	char *key;
	char *val;
	RB_ENTRY(header) entries;
};

RB_HEAD(headers, header);

struct options {
	size_t msg;
	int view_seen;

	size_t nignore;
	char **ignore;

	size_t nunignore;
	char **unignore;
};

RB_PROTOTYPE(headers, header, entry, header_cmp);
#endif /* MAILZ_MAIL_H */
