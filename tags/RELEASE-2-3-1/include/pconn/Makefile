# $Id: Makefile,v 1.7 2002-03-10 23:04:59 arensb Exp $

TOP =		../..
SUBDIR =	include/pconn

HEADERS =	PConnection.h \
		netsync.h \
		cmp.h \
		dlp.h \
		dlp_cmd.h \
		dlp_rpc.h \
		padp.h \
		palm_errno.h \
		palm_types.h \
		pconn.h \
		slp.h \
		util.h \
		spc_client.h

DISTFILES =	Makefile \
		${HEADERS}

all::

include ${TOP}/Make.rules

# "spc.h" is a symlink because "coldsync" and "libpconn" are conceptually
# separate packages, but "spc.h" is used in both. But I don't want to
# maintain two identical copies of the same file. However, CVS doesn't
# handle symlinks, so instead we create it here.
${RECURSIVE_TARGETS}::	spc.h

spc.h:
	ln -s ${TOP}/src/spc.h spc.h

# This is for Emacs's benefit:
# Local Variables:	***
# fill-column:	75	***
# End:			***
