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
