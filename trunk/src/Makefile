# $Id: Makefile,v 2.4 2000-02-05 06:08:33 arensb Exp $

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
CXX_OBJS =	${CXX_SRCS:.cc:=.o}

SRCS =		${C_SRCS} ${CXX_SRCS}

HEADERS =	archive.h \
		coldsync.h \
		conduit.h \
		parser.h \
		GenericConduit.hh

OBJS =		${C_OBJS} ${CXX_OBJS} \
		lex.yy.o y.tab.o

LIBPCONN = 	-L${TOP}/libpconn -lpconn
LIBPDB = 	-L${TOP}/libpdb -lpdb
EXTRA_LIBS =	${LIBPDB} ${LIBPCONN} ${LIBYACC} ${LIBLEX}
CLEAN =		${PROG} ${CXXPROG} ${OBJS} \
		*.ln *.bak *~ core *.core .depend \
		lex.yy.c y.tab.c y.tab.h y.output

DISTFILES =	Makefile \
		${SRCS} ${HEADERS} \
		lexer.l parser.y

all::		${CXXPROG}

depend::
	${MKDEP} ${CPPFLAGS} ${C_SRCS} ${CXX_INCLUDEDIRS} ${CXX_SRCS}

include ../Make.rules

install::	${CXXPROG}
	${INSTALL_PROGRAM} ${CXXPROG} ${BINDIR}/${CXXPROG}

# Additional dependencies, maintained by hand 'cos I don't know how
# else to do it
coldsync:	${TOP}/libpconn/libpconn.a
coldsync:	${TOP}/libpdb/libpdb.a
