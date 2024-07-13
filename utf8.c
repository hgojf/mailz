#include <wchar.h>
#include <uchar.h>

#include "utf8.h"

/* 
 * decodes a single utf-8 codepoint, except for NUL
 * setlocale(LC_CTYPE, "UTF-8") *must* have been called
 */
int
utf8_decode(struct utf8_decode *state, char byte)
{
	char s[2];

	if (state->n == 4)
		return UTF8_DECODE_INVALID;
	s[0] = byte;
	s[1] = '\0';
	switch (mbrtowc(NULL, s, 1, &state->mbs)) {
	case -1:
	case -3:
	case 0:
		return UTF8_DECODE_INVALID;
	case -2:
		state->buf[state->n++] = byte;
		return UTF8_DECODE_MORE;
	default:
		state->buf[state->n++] = byte;
		return UTF8_DECODE_DONE;
	}
}
