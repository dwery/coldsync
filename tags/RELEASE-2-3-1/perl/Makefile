# $Id: Makefile,v 1.11 2002-03-10 23:11:53 arensb Exp $

TOP =		..
SUBDIR =	perl

SUBDIRS =	ColdSync

DISTFILES =	Makefile

CLEAN =		*.bak *~ core *.core

# Hack: ${RECURSIVE_TARGETS} is redefined here in order to correctly
# build ColdSync/Makefile when necessary.
# DU 'make' complains that RECURSIVE_TARGETS is redefined, but it
# works anyway.
RECURSIVE_TARGETS =	\
		all depend clean distclean spotless distfiles-core distfiles \
		install lint TAGS

${RECURSIVE_TARGETS}::	ColdSync/Makefile

include ${TOP}/Make.rules

# XXX - This is needed for 'make all' after a 'make clean', so leave it in.
# But this needs to match what was used in 'configure' to build the Perl
# Makefile.
ColdSync/Makefile:	ColdSync/Makefile.PL
	cd ColdSync && ${PERL} Makefile.PL PREFIX="${PREFIX}"

# This is for Emacs's benefit:
# Local Variables:	***
# fill-column:	75	***
# End:			***
