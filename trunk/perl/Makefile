# $Id: Makefile,v 1.9 2000-11-10 03:36:24 arensb Exp $

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

ColdSync/Makefile:	ColdSync/Makefile.PL
	cd ColdSync && ${PERL} Makefile.PL

# This is for Emacs's benefit:
# Local Variables:	***
# fill-column:	75	***
# End:			***
