# $Id: Makefile,v 2.14 2000-11-10 03:36:29 arensb Exp $

TOP =		..
SUBDIR =	src

CXXPROG =	coldsync

C_SRCS =	coldsync.c \
		archive.c \
		backup.c \
		restore.c \
		install.c \
		config.c \
		misc.c \
		log.c \
		conduit.c \
		pref.c \
		spc.c
C_OBJS =	${C_SRCS:.c=.o}

CXX_SRCS =	GenericConduit.cc
CXX_OBJS =	${CXX_SRCS:.cc=.o}

SRCS =		${C_SRCS} ${CXX_SRCS}

HEADERS =	archive.h \
		coldsync.h \
		conduit.h \
		cs_error.h \
		parser.h \
		spc.h \
		pref.h \
		GenericConduit.hh

OBJS =		${C_OBJS} ${CXX_OBJS} \
		lex.yy.o y.tab.o

LIBPCONN = 	-L${TOP}/libpconn -lpconn
LIBPDB = 	-L${TOP}/libpdb -lpdb
EXTRA_LIBS =	${LIBPDB} ${LIBPCONN} ${LIBYACC} ${LIBLEX}
CLEAN =		${CXXPROG} ${OBJS} \
		*.ln *.bak *~ core *.core .depend \
		lex.yy.c y.tab.c y.tab.h y.output \
		parser.tab.c parser.tab.h parser.output

DISTFILES =	Makefile \
		${SRCS} ${HEADERS} \
		lexer.l parser.y

all::		${CXXPROG}

depend::
	${MKDEP} ${CPPFLAGS} ${C_SRCS} ${CXX_INCLUDEDIRS} ${CXX_SRCS}

include ${TOP}/Make.rules

install::	${CXXPROG}
	${MKDIR} ${BINDIR}
	${INSTALL_PROGRAM} ${CXXPROG} ${BINDIR}/${CXXPROG}

# Additional dependencies, maintained by hand 'cos I don't know how
# else to do it
coldsync:	${TOP}/libpconn/libpconn.a
coldsync:	${TOP}/libpdb/libpdb.a

lex.yy.c:	parser.h
y.tab.c:	parser.h

# This is for Emacs's benefit:
# Local Variables:	***
# fill-column:	75	***
# End:			***
