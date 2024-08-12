#ifndef MAILZ_CACHE_H
#define MAILZ_CACHE_H
struct cache {
	size_t nentry;
	struct letter *entries;
	int view_all;
};

#define CACHE_VERSION_MISMATCH -2
void cache_free(struct cache *);
int cache_read(FILE *, struct cache *);
struct letter *cache_take(struct cache *, const char *);
int cache_write(int, int, struct letter *, size_t);
#endif /* MAILZ_CACHE_H */
