#ifndef MAILZ_SENDMAIL_H
#define MAILZ_SENDMAIL_H
struct sendmail {
	struct {
		const char *addr;
		const char *name;
	} from;
	const char *subject; /* maybe NULL */
	const char *to;
	int re;
};

int sendmail(struct sendmail *);
#endif /* MAILZ_SENDMAIL_H */
