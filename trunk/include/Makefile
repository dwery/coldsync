# $Id: Makefile,v 1.5 2004-03-27 14:57:15 azummo Exp $

TOP =		..
SUBDIR =	include
SUBDIRS =	pconn

HEADERS =	pdb.h \
		palm.h
DISTFILES =	Makefile \
		${HEADERS}

include ${TOP}/Make.rules

install::	${HEADERS}
	${MKDIR} ${INCLUDEDIR}
	${INSTALL_DATA} ${HEADERS} ${INCLUDEDIR}

# This is for Emacs's benefit:
# Local Variables:	***
# fill-column:	75	***
# End:			***
