# $Id: Makefile,v 1.9 2001-10-12 02:17:59 arensb Exp $

TOP =		..
SUBDIR =	libpconn

LIBNAME =	pconn

LIBSRCS =	cfmakeraw.c \
		crc.c \
		dlp_cmd.c \
		dlp_rpc.c \
		util.c \
		dlp.c \
		netsync.c \
		palm_errno.c \
		padp.c \
		slp.c \
		cmp.c \
		PConnection.c \
		PConnection_serial.c \
		PConnection_usb.c \
		PConnection_net.c \
		PConnection_usbm50x.c \
		spc_client.c 

LIBOBJS =	${LIBSRCS:.c=.o}

CLEAN =		${LIBOBJS} ${LIBRARY} \
		*.ln *.bak *~ core *.core .depend
DISTCLEAN =
SPOTLESS =

DISTFILES =	Makefile ${LIBSRCS}

OTHERTAGFILES =	${LIBSRCS}

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
