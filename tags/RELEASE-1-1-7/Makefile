# Makefile
#
# Top-level Makefile
#
# $Id: Makefile,v 1.12 2000-04-09 13:45:42 arensb Exp $

# In each Makefile, ${TOP} is the top of the source tree. ${SUBDIR} is the
# path to the current directory, relative to ${TOP}. These two variables
# are automatically set by the ${RECURSIVE_TARGETS}:: rule in "Make.rules".
# However, for the benefit of people who like running `make' from inside
# the source tree, they should also be set at the top of each Makefile.
TOP =		.
SUBDIR =	.

# List of subdirectories underneath this one.
SUBDIRS =	include libpconn libpdb src perl doc i18n

# Files to include in snapshots and distributions
DISTFILES =	README \
		INSTALL \
		Artistic \
		AUTHORS \
		NEWS \
		ChangeLog \
		ChangeLog.0 \
		HACKING \
		Makefile \
		Make.rules.in \
		configure.in \
		aclocal.m4 \
		config.h.in \
		TODO \
		install-sh \
		configure

# Files to include in distributions, but not in snapshots
EXTRA_DISTFILES =

# Files to delete when making `clean', `distclean' and `spotless',
# respectively. Each one deletes the ones before.
CLEAN =		core *.core *.bak *~ errs errs.*
DISTCLEAN =	config.cache Make.rules config.h config.log config.status \
		ID TAGS .depend
SPOTLESS =	configure

all::		Make.rules

# Rebuild the `configure' stuff, if necessary.
Make.rules config.h:	configure
	./config.status

configure:	configure.in
	autoconf

distfiles-core::
	if test ! -d "${TOPDISTDIR}"; then \
		mkdir "${TOPDISTDIR}"; \
	fi

# Emacs TAGS file
tags:	TAGS
TAGS::
	rm -f TAGS

include Make.rules

# This creates the ID file that lists where various identifiers are
# found. Used by 'lid', 'gid', etc., part of the id-utils package.
id:	ID

# The double-colon here is to force the file to be rebuilt every time.
ID::
	${MKID}

dist:	distfiles
	GZIP="--best" ${TAR} chozf ${TARBALL} ${TOPDISTDIR}
	rm -rf ${TOPDISTDIR}

snapshot:	distfiles-core
	date=`${DATE} "+%Y%m%d"`; \
	GZIP="--best" ${TAR} chozf "${PACKAGE}-${VERSION}-$${date}.tar.gz" \
		${TOPDISTDIR}
	rm -rf ${TOPDISTDIR}

spotless::
	rm -rf ${TOPDISTDIR}


# This is for Emacs's benefit:
# Local Variables:	***
# fill-column:	75	***
# End:			***
