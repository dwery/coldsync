bin_PROGRAMS = coldsync

coldsync_SOURCES = \
	coldsync.c \
	backup.c \
	handledb.c \
	upload.c \
	pdb.c \
	sync.c \
	config.c \
	archive.c \
	conduit.c \
	GenericConduit.cc

coldsync_DEPENDENCIES = $(top_builddir)/pconn/libpconn.a

INCLUDES = \
	-I$(top_srcdir)

EXTRA_DIST = \
	coldsync.h \
	conduit.h \
	pdb.h \
	archive.h \
	GenericConduit.hh

## INCLUDES = -I$(srcdir)
LIBS = -L$(top_builddir)/pconn -lpconn
