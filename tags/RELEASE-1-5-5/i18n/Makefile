# $Id: Makefile,v 1.7 2000-11-10 03:35:57 arensb Exp $

# XXX - It would be nice to arrange things so that the <lang>.po files
# aren't modified all the time (and cause CVS to think the file has
# changed, when all that has changed is some line numbers in the
# comments). Presumably, then, should either remove line numbers from
# the .po files (yuck!) or not feed changes back into .po files (but
# then this loses information about added and removed messages).

TOP =		..
SUBDIR =	i18n

# LANGS: list of all languages/locales for which there is a translation
LANGS =		fr de
POFILES =	${LANGS:=.po}
MOFILES =	${LANGS:=.mo}

DISTFILES =	Makefile \
		messages.po \
		${POFILES} \
		FILES

CLEAN =		core *.core *.bak *~ errs errs.* ${MOFILES}
SPOTLESS =	messages.po

include ${TOP}/Make.rules

all::	${MOFILES}

# XXX - Shouldn't rebuild messages.po every single time. Ideally,
# should rebuild it only if the files in 'FILES' have changed.
messages.po::
	${XGETTEXT} -D${TOP} -LC++   -f FILES        -s -k_ -kN_ -o $@

# I thought I needed to include this line, but things seems to work
# without it. Oh, well. Here it is, in case it turns out to be
# necessary after all.
#	${XGETTEXT} -D${TOP} -LC++ -f FILES.C++ -j -s -k_ -kN_ -o $@ -D`pwd`

# XXX - There's probably a better way to do this so that things work
# whether or not the user has i18n turned on or not.
${POFILES}:	messages.po
	if [ ! -f "$@" ]; then \
		touch "$@"; \
	fi
	${MSGMERGE} -v -o temp.pox "$@" messages.po
	if [ -f temp.pox ]; then \
		cp temp.pox "$@"; \
		rm -f temp.pox; \
	fi

install-i18n::	${MOFILES}
	langs="${LANGS}"; \
	for lang in $$langs; do \
		${MKDIR} "${LOCALEDIR}/$$lang"; \
		${INSTALL_DATA} "$$lang.mo" \
			"${LOCALEDIR}/$$lang/LC_MESSAGES/${PACKAGE}.mo"; \
	done

# This is for Emacs's benefit:
# Local Variables:	***
# fill-column:	75	***
# End:			***
