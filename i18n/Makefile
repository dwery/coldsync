# $Id: Makefile,v 1.3 2000-02-07 00:51:11 arensb Exp $

TOP =		..
SUBDIR =	i18n

LANGS =		fr
POFILES =	${LANGS:=.po}
MOFILES =	${LANGS:=.mo}

DISTFILES =	Makefile \
		messages.po \
		${POFILES} \
		FILES

CLEAN =		core *.core *.bak *~ errs errs.* ${MOFILES}
SPOTLESS =	messages.po

include ../Make.rules

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
		if [ ! -d "${LOCALEDIR}/$$lang" ]; then \
			mkdir "${LOCALEDIR}/$$lang"; \
		fi; \
		${INSTALL_DATA} "$$lang.mo" \
			"${LOCALEDIR}/$$lang/LC_MESSAGES/${PACKAGE}.mo"; \
	done
