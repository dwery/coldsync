# $Id: Makefile,v 1.6 2000-04-09 14:17:38 arensb Exp $

# XXX - This Makefile is temporary. It should be replaced by a Makefile.PL

TOP =		..
SUBDIR =	perl

SUBDIRS =	ColdSync

DISTFILES =	Makefile

CLEAN =		*.bak *~ core *.core

# Hack: ${RECURSIVE_TARGETS} is redefined here in order to correctly
# build ColdSync/Makefile when necessary.
RECURSIVE_TARGETS =	\
		all depend clean distclean spotless distfiles-core distfiles \
		install lint TAGS

${RECURSIVE_TARGETS}::	ColdSync/Makefile

include ${TOP}/Make.rules

ColdSync/Makefile:	ColdSync/Makefile.PL
	cd ColdSync && ${PERL} Makefile.PL
