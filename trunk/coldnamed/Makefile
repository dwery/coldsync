# $Id: Makefile,v 1.2 2004-11-02 04:22:19 christophe Exp $

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
	${MKDIR} ${DESTDIR}/${SBINDIR}
	${INSTALL_PROGRAM} ${PROG} ${DESTDIR}/${SBINDIR}/${PROG}

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
