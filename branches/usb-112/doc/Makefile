TOP =		..
SUBDIR =	doc

INFOFILES =	conduits.info \
		pdb.info

DVIFILES =	conduits.dvi \
		pdb.dvi

PSFILES =	conduits.ps \
		pdb.ps

DISTFILES =	Makefile \
		coldsync.8 \
		conduits.texi \
		libpalm.texi \
		pdb.texi \
		texinfo.tex \
		version.texi.in

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
	${INSTALL_MAN} coldsync.8 ${MANDIR}/coldsync.8
	@for file in ${INFOFILES}; do \
		echo "${INSTALL_DATA} $${file} ${INFODIR}/$${file}"; \
		${INSTALL_DATA} $${file} ${INFODIR}/$${file}; \
	done
