#include <err.h>
#include <string.h>
#include <unistd.h>

/* 
 * Allows mailz to execute programs in specifically the way it needs to.
 * Not to be confused with the traditional mailwrapper
 */
int
main(int argc, char *argv[])
{
	if (strcmp(argv[0], "vi") == 0) {
		if (argc != 2)
			errx(1, "invalid usage");
		execl("/usr/bin/vi", "vi", "--", argv[1], NULL);
		err(1, "execl");
	}
	else if (strcmp(argv[0], "sendmail") == 0) {
		if (argc != 2)
			errx(1, "invalid usage");
		execl("/usr/sbin/sendmail", "sendmail", "-t", "--", argv[1], NULL);
		err(1, "execl");
	}
	else if (strcmp(argv[0], "less") == 0) {
		execl("/usr/bin/less", "less", "--", "-", NULL);
		err(1, "execl");
	}
}
