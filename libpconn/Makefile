# $Id: Makefile,v 1.13 2002-11-23 16:32:40 azummo Exp $

TOP =		..
SUBDIR =	libpconn

LIBNAME =	pconn
SHLIB_MAJOR =	0
SHLIB_MINOR =	0

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
		PConnection_libusb.c \
		PConnection_net.c \
		spc_client.c 

LIBOBJS =	${LIBSRCS:.c=.o}
SHLIBOBJS =	${LIBSRCS:.c=.So}

CLEAN =		${LIBOBJS} ${SHLIBOBJS} ${LIBRARY} \
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

# XXX - Building shared libraries:
#  FreeBSD:
#	${CC} -fpic -DPIC ${CFLAGS} -c foo.c -o foo.So
#	ld -o foo.So.tmp -x -r foo.So
#	mv foo.So.tmp foo.So
#
#	rm -f libfoo.so.1 libfoo.so
#	ln -sf libfoo.so.1 libfoo.so
#	${CC} -shared -Wl,-x -o libfoo.so.1 -Wl,-soname,libfoo.so.1 \
#		`lorder *.So | tsort -q`

# Solaris: has 'lorder' and 'tsort', but linker doesn't support "-x"
# (delete local symbols).
#	'ln' is picky: you need to say 'ln -f -s foo bar', not 'ln -fs'
# or 'ln -sf' :-(
#	'tsort' doesn't take "-q" option.

# DU: has 'lorder' and 'tsort'
#	'ld' recognizes "-x" option.
#	'tsort' doesn't take "-q" option.

# This is for Emacs's benefit:
# Local Variables:	***
# fill-column:	75	***
# End:			***
