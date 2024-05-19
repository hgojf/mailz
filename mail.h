#ifndef MAILZ_MAIL_H
#define MAILZ_MAIL_H
struct header {
	char *key;
	char *val;
	RB_ENTRY(header) entries;
};

struct letter {
	RB_HEAD(headers, header) headers;
	char *text;
	time_t sent;
};

struct mail {
	struct letter *letters;
	size_t nletters;
};

RB_PROTOTYPE(headers, header, entry, header_cmp);
#endif /* MAILZ_MAIL_H */
