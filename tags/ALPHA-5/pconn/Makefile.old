CFLAGS +=	-g -Wall -ansi -pedantic
LIBSRCS =	slp.c \
		padp.c \
		cmp.c \
		dlp.c dlp_cmd.c \
		util.c \
		PConnection.c \
		palm_errno.c

LIBOBJS =	${LIBSRCS:R:S/$/.o/g}

SUBDIR =	doc

RM =		rm -f

# all:	libslp.a libpadp.a libcmp.a cmp.o foo
all:	libpalm.a foo
all:	${SUBDIR}

# XXX - Need some portable way of building libraries
libpalm.a:	${LIBOBJS}
	${RM} ${.TARGET}
	ar cq ${.TARGET} `lorder ${LIBOBJS} | tsort -q`
	ranlib ${.TARGET}
# XXX - Ought to build shared libraries, too

clean::
	${RM} ${LIBOBJS} libpalm.a

foo:	libpalm.a foo.o
	${CC} ${CFLAGS} -o ${.TARGET} foo.o -L. -lpalm

clean::
	${RM} foo.o foo

clean::
	${RM} *.bak core *.core

clean::
.for dir in ${SUBDIR}
	cd ${dir}; ${MAKE} ${.TARGET}
.endfor

tags:	TAGS
TAGS:
	${RM} ${.TARGET}
	find . -name '*.[ch]' -print | xargs etags -a -o ${.TARGET}

clean::
	${RM} TAGS

.include <bsd.subdir.mk>
