# $Id: Makefile,v 2.10 2000-05-21 08:06:15 arensb Exp $

TOP =		..
SUBDIR =	src

CXXPROG =	coldsync

C_SRCS =	coldsync.c \
		archive.c \
		backup.c \
		restore.c \
		install.c \
		config.c \
		handledb.c \
		log.c \
		conduit.c
C_OBJS =	${C_SRCS:.c=.o}

CXX_SRCS =	GenericConduit.cc
CXX_OBJS =	${CXX_SRCS:.cc=.o}

SRCS =		${C_SRCS} ${CXX_SRCS}

HEADERS =	archive.h \
		coldsync.h \
		conduit.h \
		cs_error.h \
		parser.h \
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
