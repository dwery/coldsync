# $Id: Makefile,v 1.9 2002-03-10 23:05:03 arensb Exp $

TOP =		..
SUBDIR =	libpdb

LIBNAME =	pdb
SHLIB_MAJOR =	0
SHLIB_MINOR =	0

LIBSRCS =	pdb.c util.c
LIBOBJS =	${LIBSRCS:.c=.o}
SHLIBOBJS =	${LIBSRCS:.c=.So}

CLEAN =		${LIBOBJS} ${SHLIBOBJS} ${LIBRARY} \
		*.ln *.bak *~ core *.core .depend
DISTCLEAN =
SPOTLESS =

DISTFILES =	Makefile ${LIBSRCS}

OTHERTAGFILES =	${LIBSRCS}

include ${TOP}/Make.rules

# "util.c" is a symlink because both "libpdb" and "libpconn" use it, but I
# don't want to maintain two identical copies of the same file. However,
# CVS doesn't handle symlinks, so instead we create it here.
${RECURSIVE_TARGETS}::	util.c
util.c:
	ln -s ${TOP}/libpconn/util.c util.c

depend::
	${MKDEP} ${CPPFLAGS} ${LIBSRCS}

all::	${LIBRARY}

# It might be a good idea later on to install the library, so that
# people can write other programs that communicate with the Palm, but
# not just yet.
# install::

# This is for Emacs's benefit:
# Local Variables:	***
# fill-column:	75	***
# End:			***
