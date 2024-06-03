#ifndef MAILZ_MAIL_H
#define MAILZ_MAIL_H
struct options {
	long long msg;
	int view_seen;

	size_t nignore;
	char **ignore;

	size_t nunignore;
	char **unignore;

	size_t nreorder;
	char **reorder;
};
#endif /* MAILZ_MAIL_H */
