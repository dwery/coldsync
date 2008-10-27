# $Id$

TOP =		..
SUBDIR =	include
SUBDIRS =	pconn

HEADERS =	pdb.h \
		palm.h
DISTFILES =	Makefile \
		${HEADERS}

include ${TOP}/Make.rules

install::	${HEADERS}
	${MKDIR} ${DESTDIR}/${INCLUDEDIR}
	${INSTALL_DATA} ${HEADERS} ${DESTDIR}/${INCLUDEDIR}

# This is for Emacs's benefit:
# Local Variables:	***
# fill-column:	75	***
# End:			***
