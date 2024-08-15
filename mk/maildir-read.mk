SRCS_MAILDIR_READ = header.c leak.c maildir-read.c
OBJS_MAILDIR_READ = $(SRCS_MAILDIR_READ:.c=.o)
DEPS_MAILDIR_READ = $(SRCS_MAILDIR_READ:.c=.d)
LDFLAGS_MAILDIR_READ = -lutil

.PHONY: clean-maildir-read install-maildir-read

clean-maildir-read:
	rm -f ${DEPS_MAILDIR_READ} ${OBJS_MAILDIR_READ} maildir-read

install-maildir-read:
	$(INSTALL) -m 0755 maildir-read ${DESTDIR}${PREFIX}/libexec

maildir-read: ${OBJS_MAILDIR_READ}
	$(CC) -o $@ ${LDFLAGS_MAILDIR_READ} ${OBJS_MAILDIR_READ}

-include ${DEPS_MAILDIR_READ}
