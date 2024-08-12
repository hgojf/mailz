#include <err.h>
#include <unistd.h>

#include "leak.h"

int
fd_leak_report(void)
{
	int d;

	if ((d = getdtablecount()) == 3)
		return 0;
	d -= 3;

	if (d > 0)
		warnx("%d extra fd open", d);
	else
		warnx("%d too few fd open", -d);
	return 1;
}
