# $Id: Makefile,v 1.7 2000-05-20 16:31:15 arensb Exp $

# XXX - This Makefile is temporary. It should be replaced by a Makefile.PL

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
