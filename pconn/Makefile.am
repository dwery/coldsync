## Process this file with automake to produce Makefile.in
#
# $Id: Makefile.am,v 1.1 1999-02-19 22:51:53 arensb Exp $
#

lib_LIBRARIES = libpconn.a

libpconn_a_SOURCES = \
	slp.c \
	padp.c \
	cmp.c \
	dlp.c dlp_cmd.c \
	util.c \
	PConnection.c \
	palm_errno.c

pkginclude_HEADERS = \
	slp.h \
	padp.h \
	cmp.h \
	dlp.h dlp_cmd.h \
	util.h \
	PConnection.h \
	palm_errno.h

noinst_PROGRAMS = foo

EXTRA_DIST = \
	Makefile

## foo_SOURCES = foo.c $(libpalm_a_SOURCES)
foo_LDADD = -L. -lpconn
