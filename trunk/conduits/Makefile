# $Id: Makefile,v 1.8 2000-11-10 03:35:43 arensb Exp $

TOP =		..
SUBDIR =	conduits

# Conduits are grouped by interpreter, since the #! path is munged at
# install time.
PERL_CONDUITS =	send-mail \
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
		std-categories

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
	${MKDIR} ${CONDUITDIR} ${MAN1DIR}
	for i in ${PERL_CONDUITS}; do \
		${SED} 's:#! *[^ 	]*\(.*\):#!${PERL}\1:' \
			< $$i > $$i.inst; \
		${INSTALL_SCRIPT} $$i.inst ${CONDUITDIR}/$$i; \
		rm -f $$i.inst; \
	done
	for i in ${PERL_CONDUITS}; do \
		${INSTALL_DATA} $$i.1 ${MAN1DIR}/$$i.1; \
	done

# This is for Emacs's benefit:
# Local Variables:	***
# fill-column:	75	***
# End:			***
