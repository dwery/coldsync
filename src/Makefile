# $Id: Makefile,v 2.27 2002-03-30 16:12:18 azummo Exp $

TOP =		..
SUBDIR =	src

CPROG =		
CXXPROG =	coldsync

C_SRCS =	coldsync.c \
		archive.c \
		backup.c \
		restore.c \
		install.c \
		config.c \
		misc.c \
		lexhack.c \
		log.c \
		conduit.c \
		spalm.c \
		palment.c \
		pref.c \
		spc.c \
		symboltable.c \
		net_compat.c \
		ap_snprintf.c
C_OBJS =	${C_SRCS:.c=.o}

CXX_SRCS =	GenericConduit.cc

CXX_OBJS =	${CXX_SRCS:.cc=.o}

SRCS =		${C_SRCS} ${CXX_SRCS}

HEADERS =	archive.h \
		coldsync.h \
		conduit.h \
		cs_error.h \
		spalm.h \
		palment.h \
		parser.h \
		symboltable.h \
		spc.h \
		pref.h \
		net_compat.h \
		GenericConduit.hh

OBJS =		${C_OBJS} ${CXX_OBJS} \
		y.tab.o

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

all::		${CXXPROG} ${CPROG}

depend::	lex.yy.c
depend::
	${MKDEP} ${CPPFLAGS} ${C_SRCS} ${CXX_INCLUDEDIRS} ${CXX_SRCS}

include ${TOP}/Make.rules

install::	${CXXPROG}
	${MKDIR} ${BINDIR}
	${INSTALL_PROGRAM} ${CXXPROG} ${BINDIR}/${CXXPROG}

#manifest:
#	@for i in ${DISTFILES}; do \
#		echo "${SUBDIR}/$$i"; \
#	done

# Additional dependencies, maintained by hand 'cos I don't know how
# else to do it
coldsync:	${TOP}/libpconn/libpconn.a
coldsync:	${TOP}/libpdb/libpdb.a

lexhack.o:	lex.yy.c
lex.yy.c:	parser.h lexer.l
y.tab.c y.tab.h:	parser.h parser.y

# This is for Emacs's benefit:
# Local Variables:	***
# fill-column:	75	***
# End:			***
