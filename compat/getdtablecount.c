#include <dirent.h>
#include <err.h>
#include <limits.h>
#include <stdio.h>
#include <unistd.h>

int
getdtablecount(void)
{
	char path[PATH_MAX];
	DIR *dp;
	int n, nfd;

	n = snprintf(path, sizeof(path), "/proc/%d/fd", getpid());
	if (n < 0 || n >= sizeof(path))
		errx(1, "snprintf");

	if ((dp = opendir(path)) == NULL)
		err(1, "%s", path);

	nfd = 0;
	while (readdir(dp) != NULL)
		nfd++;

	closedir(dp);
	return nfd - 3; /* '.', '..' and our dp */
}
