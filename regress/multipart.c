#include <err.h>
#include <stdio.h>

#include "multipart.h"
#include "../multipart.h"

#if 0
void
multipart_test(void)
{
	struct multipart mp;
	FILE *fp;

	if ((fp = fopen("regress/letters/multipart_3", "r")) == NULL)
		err(1, NULL);

	if (multipart_init(&mp, fp, NULL, NULL, NULL) == -1)
		errx(1, "multipart_init");

	for (;;) {
		for (;;) {
			struct part part;
			int error;

			error = multipart_next(&mp, &part);
			if (error == -1)
				errx(1, "multipart_next");
			if (error == 0)
				break;
		}

		if (multipart_npart(&mp) == 0)
			break;
		if (multipart_choose(&mp, 0) == -1)
			errx(1, "multipart_choose");
	}

	for (;;) {
		int n;
		char buf[4];

		if ((n = multipart_getc(&mp, buf)) == -1)
			errx(1, "multipart_getc");
		if (n == 0)
			break;
	}

	multipart_free(&mp);
	fclose(fp);
}
#endif
