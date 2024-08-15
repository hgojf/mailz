SRCS_MAILDIR_READ_LETTER = header.c imsg-util.c leak.c maildir-read-letter.c
OBJS_MAILDIR_READ_LETTER = $(SRCS_MAILDIR_READ_LETTER:.c=.o)
DEPS_MAILDIR_READ_LETTER = $(SRCS_MAILDIR_READ_LETTER:.c=.d)
LDFLAGS_MAILDIR_READ_LETTER = -lutil

.PHONY: clean-maildir-read-letter install-maildir-read-letter

clean-maildir-read-letter:
	rm -f ${DEPS_MAILDIR_READ_LETTER} ${OBJS_MAILDIR_READ_LETTER} \
		maildir-read-letter

install-maildir-read-letter:
	$(INSTALL) -m 0755 maildir-read-letter ${DESTDIR}${PREFIX}/libexec

maildir-read-letter: ${OBJS_MAILDIR_READ_LETTER}
	$(CC) -o $@ ${LDFLAGS_MAILDIR_READ_LETTER} ${OBJS_MAILDIR_READ_LETTER}

-include ${DEPS_MAILDIR_READ_LETTER}
