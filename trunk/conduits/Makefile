# $Id: Makefile,v 1.14 2003-06-28 14:31:43 azummo Exp $

TOP =		..
SUBDIR =	conduits

# Conduits are grouped by interpreter, since the #! path is munged at
# install time.
PERL_CONDUITS =	copy-appinfo \
		send-mail \
		send-mail-2 \
		mbox-inbox \
		todo-text

# XXX - memo-text and std-categories aren't ready for prime time, so
# they're listed under DISTFILES so they won't be installed.

CONDUITS =	${PERL_CONDUITS}

# XXX - The *.1 is bad: it will nuke separate man pages, if there are ever any
CLEAN =		*.1 \
		pod2html-dircache pod2html-itemcache
DISTFILES =	Makefile \
		${CONDUITS} \
		memo-text \
		std-categories \
		dump-headers \
		dump-env \
		dump-dbs \
		dump-expslots \
		dump-vfs \
		zire71-photo \
		recat \
		dump-dlpdbs

all::

depend::

include ${TOP}/Make.rules

# Extract man pages from Perl pods.
manifypods:	${PERL_CONDUITS}
	for i in ${PERL_CONDUITS}; do \
		${POD2MAN_EXE} $$i > $$i.1; \
	done

# The sed script below replaces the path to the interpreter in the #!
# line with the local path. That is, if the conduit begins with
# "#!/bin/perl", but your Perl is "/usr/local/bin/perl5", then the sed
# script below will create a temporary <conduit>.inst file that begins
# with "#!/usr/local/bin/perl5" and will install that.

install::	${CONDUITS} manifypods
	@for i in ${CONDUITDIR} ${MAN1DIR}; do \
		echo "${MKDIR} $$i"; \
		${MKDIR} "$$i"; \
	done
	for i in ${PERL_CONDUITS}; do \
		${SED} 's:#! *[^ 	]*\(.*\):#!${PERL}\1:' \
			< $$i > $$i.inst; \
		${INSTALL_SCRIPT} $$i.inst ${CONDUITDIR}/$$i; \
		rm -f $$i.inst; \
	done
	@for i in ${PERL_CONDUITS}; do \
		echo "${INSTALL_DATA} $$i.1 ${MAN1DIR}/$$i.1"; \
		${INSTALL_DATA} $$i.1 ${MAN1DIR}/$$i.1; \
	done

# This is for Emacs's benefit:
# Local Variables:	***
# fill-column:	75	***
# End:			***
