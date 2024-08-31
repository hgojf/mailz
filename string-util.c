#include <ctype.h>
#include <stdlib.h>

#include "string-util.h"

int
bounded_strcmp(const char *one, const char *two, size_t two_len)
{
	size_t i;

	for (i = 0; i < two_len && one[i] != '\0'; i++) {
		if ((unsigned char)one[i] > (unsigned char)two[i])
			return 1;
		else if ((unsigned char)one[i] < (unsigned char)two[i])
			return -1;
	}

	if (one[i] == '\0' && i == two_len)
		return 0;
	else if (one[i] == '\0')
		return 1;
	else
		return -1;
}

int
bounded_strcasecmp(const char *one, const char *two, size_t two_len)
{
	size_t i;

	for (i = 0; i < two_len && one[i] != '\0'; i++) {
		int a, b;

		a = toupper((unsigned char)one[i]);
		b = toupper((unsigned char)two[i]);
		if (a > b)
			return 1;
		else if (a < b)
			return -1;
	}

	if (one[i] == '\0' && i == two_len)
		return 0;
	else if (one[i] == '\0')
		return 1;
	else
		return -1;
}
