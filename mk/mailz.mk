SRCS_MAILZ = cache.c commands.c imsg-util.c leak.c letter.c lex.c mailz.c
SRCS_MAILZ += parse.c printable.c read-letter.c read-letters.c send.c setup.c
OBJS_MAILZ = $(SRCS_MAILZ:.c=.o)
DEPS_MAILZ = $(SRCS_MAILZ:.c=.d)
LDFLAGS_MAILZ = -lutil

.PHONY: clean-mailz install-mailz

clean-mailz:
	rm -f ${DEPS_MAILZ} ${OBJS_MAILZ} lex.c mailz parse.c parse.h

install-mailz:
	$(INSTALL) -m 0755 mailz ${DESTDIR}${PREFIX}/bin

mailz: ${OBJS_MAILZ}
	$(CC) -o $@ ${LDFLAGS_MAILZ} ${OBJS_MAILZ}

lex.c: lex.l
	$(LEX) -o $@ ${.CURDIR}/lex.l

lex.o: parse.h

parse.c parse.h: parse.y
	$(YACC) -do parse.c ${.CURDIR}/parse.y
