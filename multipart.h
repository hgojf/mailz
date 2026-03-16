#ifndef MULTIPART_H
#define MULTIPART_H

#include "charset.h"
#include "encoding.h"

#define MULTIPART_BOUNDARY_MAX 998

struct part_internal;

struct part {
	char name[255];
	char type[64];
	char subtype[64];
};

struct multipart {
	struct {
		#define MULTIPART_STATE_PARTS 0
		struct {
			struct part_internal *parts;
			size_t npart;
			char boundary[MULTIPART_BOUNDARY_MAX];
		} parts;
		#define MULTIPART_STATE_CHOOSE 1
		struct {
			struct part_internal *parts;
			size_t npart;
		} choose;
		#define MULTIPART_STATE_BODY 2
		struct {
			struct charset charset;
			struct encoding encoding;
		} body;
	} data;
	FILE *echo;
	FILE *fp;
	int state;
};

int multipart_choose(struct multipart *, size_t);
void multipart_free(struct multipart *);
int multipart_getc(struct multipart *, char [static 4]);
int multipart_init(struct multipart *, FILE *, FILE *,
		   int (*) (const char *, void *), void *);
int multipart_next(struct multipart *, struct part *);
size_t multipart_npart(struct multipart *);

#endif /* MULTIPART_H */
