## Process this file with automake to produce Makefile.in
#
# $Id: Makefile.am,v 1.4 1999-02-24 13:14:40 arensb Exp $
#

lib_LIBRARIES = libpconn.a

COMPAT = @COMPAT@

libpconn_a_SOURCES = \
	slp.c \
	padp.c \
	cmp.c \
	dlp.c dlp_cmd.c \
	util.c \
	PConnection.c \
	palm_errno.c \
	@COMPAT@

pkginclude_HEADERS = \
	slp.h \
	padp.h \
	cmp.h \
	dlp.h dlp_cmd.h \
	util.h \
	PConnection.h \
	palm_errno.h

# noinst_PROGRAMS = foo

EXTRA_DIST = \
	Makefile

## foo_SOURCES = foo.c $(libpalm_a_SOURCES)
#foo_LDADD = -L. -lpconn
