# $Id: Makefile,v 1.2 2000-05-19 12:03:55 arensb Exp $

TOP =		..
SUBDIR =	conduits

CONDUITS =	memo-text \
		send-mail \
		todo-text

CLEAN =
DISTFILES =	Makefile \
		${CONDUITS}

all::

depend::

include ${TOP}/Make.rules

# XXX - Ought to replace #! line with the path to the interpreter
# XXX - Should extract documentation from PODs and install with man
# pages.
install::	${CONDUITS}
	${MKDIR} ${CONDUITDIR}
	for i in ${CONDUITS}; do \
		${INSTALL_SCRIPT} $$i ${CONDUITDIR}/$$i; \
	done
