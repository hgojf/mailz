#ifndef MAILZ_CONTENT_TYPE_H
#define MAILZ_CONTENT_TYPE_H
struct content_type {
	size_t type_len;
	size_t subtype_len;
	const char *type;
	const char *subtype;

	const char *rest;
};

struct content_type_var {
	size_t key_len;
	size_t val_len;
	const char *key;
	const char *val;
};

int content_type_init(struct content_type *, const char *);
int content_type_next(struct content_type *, 
	struct content_type_var *);
#endif /* MAILZ_CONTENT_TYPE_H */
