# $Id: Makefile,v 1.2 2000-02-06 22:51:01 arensb Exp $

TOP =		..
SUBDIR =	i18n

LANGS =		fr
POFILES =	${LANGS:=.po}
MOFILES =	${LANGS:=.mo}

DISTFILES =	Makefile \
		messages.po \
		${POFILES} \
		FILES \
		FILES.C++

CLEAN =		core *.core *.bak *~ errs errs.* ${MOFILES}
SPOTLESS =	messages.po

include ../Make.rules

all::	${MOFILES}

messages.po::
	${XGETTEXT} -D${TOP} -LC++   -f FILES        -s -k_ -kN_ -o $@

# I thought I needed to include this line, but things seems to work
# without it. Oh, well. Here it is, in case it turns out to be
# necessary after all.
#	${XGETTEXT} -D${TOP} -LC++ -f FILES.C++ -j -s -k_ -kN_ -o $@ -D`pwd`

${POFILES}:	messages.po
	if [ ! -f "$@" ]; then \
		touch "$@"; \
	fi
	${MSGMERGE} -v -o temp.pox "$@" messages.po
	cp temp.pox "$@"
	-rm -f temp.pox

install-i18n::	${MOFILES}
	langs="${LANGS}"; \
	for lang in $$langs; do \
		if [ ! -d "${LOCALEDIR}/$$lang" ]; then \
			mkdir "${LOCALEDIR}/$$lang"; \
		fi; \
		${INSTALL_DATA} "$$lang.mo" \
			"${LOCALEDIR}/$$lang/LC_MESSAGES/${PACKAGE}.mo"; \
	done
