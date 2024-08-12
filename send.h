#ifndef MAILZ_SEND_H
#define MAILZ_SEND_H
struct sendmail_from {
	const char *addr;
	const char *name;
};

struct sendmail_header {
	const char *key;
	const char *val;
};

struct sendmail_subject {
	const char *s;
	int reply;
};

int sendmail_send(FILE *);
int sendmail_setup(const struct sendmail_subject *,
	const struct sendmail_from *, 
	const struct sendmail_from *, 
	const struct sendmail_header *, size_t, 
	FILE *);
#endif /* MAILZ_SEND_H */
