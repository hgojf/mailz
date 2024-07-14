#ifndef MAILZ_MAILDIR_CACHE_READ_H
#define MAILZ_MAILDIR_CACHE_READ_H
struct maildir_cache_entry {
	struct from_safe from;
	char *path;
	char *subject;
	time_t date;
};

struct maildir_cache {
	struct timespec mtime;

	struct maildir_cache_entry *letters;
	size_t nletters;
};

int maildir_cache_read(FILE *fp, struct maildir_cache *);
#endif /* MAILZ_MAILDIR_CACHE_READ_H */
