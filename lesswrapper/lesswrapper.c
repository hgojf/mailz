#include <err.h>
#include <unistd.h>

int
main(int argc, char *argv[])
{
	if (argc != 2)
		errx(1, "usage: lesswrapper <file>");
	execl("/usr/bin/less", "less", "--", argv[1], NULL);
	err(1, "execl");
}
