# $Id: Makefile,v 1.3 2000-02-03 02:53:24 arensb Exp $

TOP =		..
SUBDIR =	libpdb

LIBNAME =	pdb

LIBSRCS =	pdb.c
LIBOBJS =	pdb.o

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
