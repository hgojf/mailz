PROG = regress
LDADD = -lutil
CFLAGS += -DREGRESS
SRCS = cache.c charset.c content-type.c date.c encoding.c extract.c
SRCS += from.c header.c ibuf-util.c imsg-sync.c letter.c lex.l
SRCS += maildir.c parse.y printable.c read-letter.c read-letters.c
SRCS += regress.c string-util.c
NOMAN = 1

.include <bsd.prog.mk>
