# $Id: Makefile,v 1.1 2000-05-06 11:19:46 arensb Exp $

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
	if test ! -d ${CONDUITDIR}; then \
		mkdir ${CONDUITDIR}; \
	fi
	for i in ${CONDUITS}; do \
		${INSTALL_SCRIPT} $$i ${CONDUITDIR}/$$i; \
	done
