# $Id: Makefile,v 1.6 2001-06-26 05:47:56 arensb Exp $

TOP =		..
SUBDIR =	libpdb

LIBNAME =	pdb

LIBSRCS =	pdb.c util.c
LIBOBJS =	${LIBSRCS:.c=.o}

CLEAN =		${LIBOBJS} ${LIBRARY} \
		*.ln *.bak *~ core *.core .depend
DISTCLEAN =
SPOTLESS =

DISTFILES =	Makefile ${LIBSRCS}

include ${TOP}/Make.rules

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
