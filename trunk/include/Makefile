# $Id: Makefile,v 1.6 2004-11-02 04:22:19 christophe Exp $

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
