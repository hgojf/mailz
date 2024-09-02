SUBDIR = maildir-extract maildir-read-letter mailz regress

.ifmake(install)
SKIPDIR = regress
.endif

.include <bsd.subdir.mk>

.PHONY: test tidy

test: maildir-extract maildir-read-letter regress
	./regress/obj/regress

tidy:
	@make MAKE_FLAGS=tidy
