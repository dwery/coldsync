# $Id: Makefile,v 1.9 2000-11-10 03:35:50 arensb Exp $

TOP =		..
SUBDIR =	doc

# XXX - This ought to include 'libpalm.texi', or whatever it mutates
# into, at some point.
TEXIFILES =	conduits.texi

INFOFILES =	${TEXIFILES:.texi=.info}
DVIFILES =	${TEXIFILES:.texi=.dvi}
PSFILES =	${TEXIFILES:.texi=.ps}
PDFFILES =	${TEXIFILES:.texi=.pdf}

DISTFILES =	Makefile \
		coldsync.8 \
		${TEXIFILES} \
		libpalm.texi \
		texinfo.tex \
		version.texi.in \
		sample.coldsync.rc

EXTRA_DISTFILES = \
		${INFOFILES} \
		${PSFILES}

CLEAN =		${INFOFILES} ${DVIFILES} ${PSFILES} \
		*.aux *.cp *.cps *.fn *.ky *.log *.pg *.toc *.tp *.ty *.vr \
		*.bak *~ errs errs.* \
		*.html
DISTCLEAN =	version.texi .depend
SPOTLESS =

include ${TOP}/Make.rules

all::	${INFOFILES}

install::	coldsync.8 ${INFOFILES}
	${MKDIR} ${MAN8DIR} ${INFODIR} ${SYSCONFDIR}
	${INSTALL_MAN} coldsync.8 ${MAN8DIR}/coldsync.8
	${INSTALL_DATA} sample.coldsync.rc ${SYSCONFDIR}/sample.coldsync.rc
	@for file in ${INFOFILES}; do \
		echo "${INSTALL_DATA} $${file} ${INFODIR}/$${file}"; \
		${INSTALL_DATA} $${file} ${INFODIR}/$${file}; \
	done

dvi:	${DVIFILES}
ps:	${PSFILES}
html:	${HTMLFILES}
pdf:	${PDFFILES}

# This is for Emacs's benefit:
# Local Variables:	***
# fill-column:	75	***
# End:			***
