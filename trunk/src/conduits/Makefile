# $Id: Makefile,v 1.2 2002-12-10 16:55:31 azummo Exp $

TOP =		../..
SUBDIR =	src/conduits

C_SRCS =	dummy.c

C_OBJS =	${C_SRCS:.c=.o}

CXX_SRCS =	generic.cc

CXX_OBJS =	${CXX_SRCS:.cc=.o}

SRCS =		${C_SRCS} ${CXX_SRCS}

HEADERS =	dummy.h \
		generic.h \
		generic.hh

OBJS =		${C_OBJS} ${CXX_OBJS} \
		y.tab.o

INCLUDE =	-I ..

EXTRA_LIBS =	
CLEAN =		${CXXPROG} ${OBJS} \
		*.ln *.bak *~ core *.core .depend

DISTFILES =	Makefile \
		${SRCS} ${HEADERS}

all::		${C_OBJS} ${CXX_OBJS}

depend::
depend::
	${MKDEP} ${CPPFLAGS} ${C_SRCS} ${CXX_INCLUDEDIRS} ${CXX_SRCS}

include ${TOP}/Make.rules

# This is for Emacs's benefit:
# Local Variables:	***
# fill-column:	75	***
# End:			***
