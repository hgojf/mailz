#ifndef MAILZ_CACHE_H
#define MAILZ_CACHE_H
#include "letter.h"

struct cache {
	struct letter *letters;
	size_t nletter;
	int view_all;
};

void cache_free(struct cache *);
int cache_read(FILE *, struct cache *);
int cache_take(struct cache *, const char *, struct letter *);
int cache_write(int, long long, FILE *, struct letter *, size_t);
#endif /* MAILZ_CACHE_H */
