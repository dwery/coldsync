# $Id: Makefile,v 1.3 2000-02-03 02:53:16 arensb Exp $

TOP =		..
SUBDIR =	libpconn

LIBNAME =	pconn

LIBSRCS =	cfmakeraw.c \
		dlp_cmd.c \
		util.c \
		dlp.c \
		palm_errno.c \
		padp.c \
		slp.c \
		cmp.c \
		PConnection.c \
		PConnection_serial.c \
		PConnection_usb.c

LIBOBJS =	cfmakeraw.o \
		dlp_cmd.o \
		util.o \
		dlp.o \
		palm_errno.o \
		padp.o \
		slp.o \
		cmp.o \
		PConnection.o \
		PConnection_serial.o \
		PConnection_usb.o

CLEAN =		${LIBOBJS} ${LIBRARY} \
		*.ln *.bak *~ core *.core .depend
DISTCLEAN =
SPOTLESS =

DISTFILES =	Makefile ${LIBSRCS}

include ${TOP}/Make.rules

depend::
	${MKDEP} ${CPPFLAGS} ${LIBSRCS}

all::	${LIBRARY}

# It might be a good idea later on to install the library, so that
# people can write other programs that communicate with the Palm, but
# not just yet.
# install::
