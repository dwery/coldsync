# $Id: Makefile,v 1.1 2002-03-19 12:05:26 arensb Exp $

TOP =		..
SUBDIR =	coldnamed

PROG =		coldnamed

C_SRCS =	coldnamed.c
C_OBJS =	${C_SRCS:.c=.o}

SRCS =		${C_SRCS}

OBJS =		${C_OBJS}

CLEAN =		${PROG} ${OBJS} \
		*.ln *.bak *~ core *.core .depend

DISTFILES =	Makefile \
		${SRCS} ${HEADERS}

all::		${PROG}

depend::
	${MKDEP} ${CPPFLAGS} ${C_SRCS}

include ${TOP}/Make.rules

install::	${PROG}
	${MKDIR} ${SBINDIR}
	${INSTALL_PROGRAM} ${PROG} ${SBINDIR}/${PROG}

#manifest:
#	@for i in ${DISTFILES}; do \
#		echo "${SUBDIR}/$$i"; \
#	done

# XXX - Experimental. This doesn't play well with ${LDFLAGS}, so probably
# best to move coldnamed into its own directory, with a separate Makefile.
# Or perhaps fix LDFLAGS and friends to support multiple programs.
#coldnamed:	coldnamed.o
#	${CC} ${CFLAGS} coldnamed.o -o $@ ${LDFLAGS}

# This is for Emacs's benefit:
# Local Variables:	***
# fill-column:	75	***
# End:			***
