# $Id: Makefile,v 1.22 2005-08-15 01:35:18 christophe Exp $

TOP =		..
SUBDIR =	conduits

# Conduits are grouped by interpreter, since the #! path is munged at
# install time.
PERL_CONDUITS =	copy-appinfo \
		send-mail \
		send-mail-2 \
		mbox-inbox \
		todo-text \
		archive-sync \
		purge-todo

C_CONDUITS = time-conduit

# XXX - memo-text and std-categories aren't ready for prime time, so
# they're listed under DISTFILES so they won't be installed.

CONDUITS =	${PERL_CONDUITS} $(C_CONDUITS)

# XXX - The *.1 is bad: it will nuke separate man pages, if there are ever any
CLEAN =		*.1 \
		pod2html-dircache pod2html-itemcache
DISTFILES =	Makefile \
		${PERL_CONDUITS} \
		memo-text \
		std-categories \
		dump-headers \
		dump-env \
		dump-dbs \
		dump-expslots \
		dump-vfs \
		zire71-photo \
		recat \
		dump-dlpdbs \
		dump-vfs-root-dir \
		vfs-read-test \
		vfs-write-test \
		archive-sync \
		memo-conduit.c \
		time-conduit.c

LIBPCONN =  -L${TOP}/libpconn -lpconn
LIBPDB =    -L${TOP}/libpdb -lpdb
EXTRA_LIBS =   ${LIBPDB} ${LIBPCONN}

all::

depend::

include ${TOP}/Make.rules

# Extract man pages from Perl pods.
manifypods:	${PERL_CONDUITS}
	for i in ${PERL_CONDUITS}; do \
		${POD2MAN_LOCAL} $$i > $$i.1; \
	done

# The sed script below replaces the path to the interpreter in the #!
# line with the local path. That is, if the conduit begins with
# "#!/bin/perl", but your Perl is "/usr/local/bin/perl5", then the sed
# script below will create a temporary <conduit>.inst file that begins
# with "#!/usr/local/bin/perl5" and will install that.

install::	${CONDUITS} manifypods
	@for i in ${CONDUITDIR} ${MAN1DIR}; do \
		echo "${MKDIR} ${DESTDIR}/$$i"; \
		${MKDIR} "${DESTDIR}/$$i"; \
	done
	for i in ${PERL_CONDUITS}; do \
		${SED} 's:#! *[^ 	]*\(.*\):#!${PERL}\1:' \
			< $$i > $$i.inst; \
		${INSTALL_SCRIPT} $$i.inst ${DESTDIR}/${CONDUITDIR}/$$i; \
		rm -f $$i.inst; \
	done
	for i in ${C_CONDUITS}; do \
		${INSTALL_PROGRAM} $$i ${DESTDIR}/${CONDUITDIR}/$$i; \
	done
	@for i in ${PERL_CONDUITS}; do \
		echo "${INSTALL_DATA} $$i.1 ${DESTDIR}/${MAN1DIR}/$$i.1"; \
		${INSTALL_DATA} $$i.1 ${DESTDIR}/${MAN1DIR}/$$i.1; \
	done

time-conduit:   ${TOP}/libpconn/libpconn.a
time-conduit:   ${TOP}/libpdb/libpdb.a

# This is for Emacs's benefit:
# Local Variables:	***
# fill-column:	75	***
# End:			***
