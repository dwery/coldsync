bin_PROGRAMS = coldsync

coldsync_SOURCES = \
	coldsync.c \
	backup.c \
	handledb.c \
	upload.c \
	pdb.c \
	sync.c \
	config.c \
	download.c \
	archive.c

coldsync_DEPENDENCIES = $(top_builddir)/pconn/libpconn.a

INCLUDES = \
	-I$(top_srcdir)

EXTRA_DIST = \
	coldsync.h \
	pdb.h \
	archive.h

## INCLUDES = -I$(srcdir)
LIBS = -L$(top_builddir)/pconn -lpconn
