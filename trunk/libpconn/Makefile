# $Id: Makefile,v 1.5 2000-11-10 03:36:14 arensb Exp $

TOP =		..
SUBDIR =	libpconn

LIBNAME =	pconn

LIBSRCS =	cfmakeraw.c \
		dlp_cmd.c \
		dlp_rpc.c \
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
		dlp_rpc.o \
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

# This is for Emacs's benefit:
# Local Variables:	***
# fill-column:	75	***
# End:			***
