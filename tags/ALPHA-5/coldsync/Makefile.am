bin_PROGRAMS = coldsync

coldsync_SOURCES = \
	coldsync.c \
	backup.c \
	handledb.c \
	upload.c \
	pdb.c \
	sync.c \
	config.c \
	download.c

coldsync_DEPENDENCIES = $(top_builddir)/pconn/libpconn.a

INCLUDES = \
	-I$(top_srcdir)

# EXTRA_DIST = \
# 	coldsync.h
pkginclude_HEADERS =
	coldsync.h \
	pdb.h

## INCLUDES = -I$(srcdir)
LIBS = -L$(top_builddir)/pconn -lpconn
