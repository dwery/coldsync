bin_PROGRAMS = coldsync

coldsync_SOURCES = \
	coldsync.c \
	backup.c \
	handledb.c \
	pdb.c

INCLUDES = \
	-I$(top_srcdir)

EXTRA_DIST = \
	Makefile

## INCLUDES = -I$(srcdir)
LIBS = -L$(top_builddir)/pconn -lpconn
