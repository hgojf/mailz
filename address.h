#ifndef MAILZ_ADDRESS_H
#define MAILZ_ADDRESS_H
struct from_safe {
	/* 
	 * this is a valid email address for which 
	 * from_extract will always succeed
	 */
	char *str;
};

struct from {
	int al; /* address-length */
	int nl; /* name-length */
	char *addr; /* NOT NUL-terminated */
	char *name; /* possibly NULL, NOT NUL-terminated */
};

int from_safe_new(char *, struct from_safe *);
void from_extract(const struct from_safe *, struct from *);

#ifdef REGRESS
int from_test(void);
#endif /* REGRESS */
#endif /* MAILZ_ADDRESS_H */
