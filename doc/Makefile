RM =		rm -f
INFO =		libpalm
FORMATS =	info dvi

# XXX - Add variables for concept and function (and other) indices.

TEXINDEX =	/usr/local/bin/texindex

# XXX - Fix this to work with multiple files in ${INFO}
CLEANSUFFX =	aux \
		cp \
		cps \
		dvi \
		fn \
		fns \
		ky \
		log \
		pg \
		toc \
		tp \
		ty \
		vr
.for _f in ${CLEANSUFFX}
CLEANFILES +=	${INFO:S/$/.${_f}/}
.endfor
CLEANFILES +=	*.bak core *.core

.include <bsd.info.mk>

# This is just the .texi->.dvi rule from bsd.info.mk, but with an
# additional line to allow you to run 'texinfo' between the two 'tex'
# runs, to create a proper index.
.texi.dvi .texinfo.dvi:
	env TEXINPUTS=${.CURDIR}:${SRCDIR}:$$TEXINPUTS \
		${TEX} ${.IMPSRC} </dev/null
.if exists(${TEXINDEX})
	env TEXINPUTS=${.CURDIR}:${SRCDIR}:$$TEXINPUTS \
		${TEXINDEX} ${.IMPSRC:R:S/$/.cp/} </dev/null
.endif
# Run again to reolve cross references.
	env TEXINPUTS=${.CURDIR}:${SRCDIR}:$$TEXINPUTS \
		${TEX} ${.IMPSRC} </dev/null
