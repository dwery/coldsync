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

CXX_SRCS =	GenericConduit.cc

SRCS =		${C_SRCS} ${CXX_SRCS}

HEADERS =	archive.h \
		coldsync.h \
		conduit.h \
		parser.h \
		GenericConduit.hh

OBJS =		coldsync.o \
		archive.o \
		backup.o \
		restore.o \
		install.o \
		config.o \
		handledb.o \
		log.o \
		conduit.o \
		GenericConduit.o \
		lex.yy.o \
		y.tab.o
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
	${INSTALL_PROGRAM} ${CXXPROG} ${BINDIR} ${BINDIR}/${CXXPROG}

# Additional dependencies, maintained by hand 'cos I don't know how
# else to do it
coldsync:	${TOP}/libpconn/libpconn.a
coldsync:	${TOP}/libpdb/libpdb.a
