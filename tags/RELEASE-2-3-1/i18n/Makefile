# $Id: Makefile,v 1.16 2002-01-23 15:09:07 arensb Exp $

# XXX - It would be nice to arrange things so that the <lang>.po files
# aren't modified all the time (and cause CVS to think the file has
# changed, when all that has changed is some line numbers in the
# comments). Presumably, then, should either remove line numbers from
# the .po files (yuck!) or not feed changes back into .po files (but
# then this loses information about added and removed messages).

TOP =		..
SUBDIR =	i18n

# LANGS: list of all languages/locales for which there is a translation
LANGS =		fr de it cs ru
POFILES =	${LANGS:=.po}
MOFILES =	${LANGS:=.mo}

# 'xgettext' gets confused by some of the lex regular expressions, so
# we get the strings from lex.yy.c instead of src/lexer.l
INPUTFILES =	\
	${TOP}/libpconn/PConnection.c \
	${TOP}/libpconn/PConnection_net.c \
	${TOP}/libpconn/PConnection_serial.c \
	${TOP}/libpconn/PConnection_usb.c \
	${TOP}/libpconn/cfmakeraw.c \
	${TOP}/libpconn/cmp.c \
	${TOP}/libpconn/dlp.c \
	${TOP}/libpconn/dlp_cmd.c \
	${TOP}/libpconn/dlp_rpc.c \
	${TOP}/libpconn/netsync.c \
	${TOP}/libpconn/padp.c \
	${TOP}/libpconn/palm_errno.c \
	${TOP}/libpconn/slp.c \
	${TOP}/libpconn/util.c \
	${TOP}/libpdb/pdb.c \
	${TOP}/src/archive.c \
	${TOP}/src/backup.c \
	${TOP}/src/coldsync.c \
	${TOP}/src/conduit.c \
	${TOP}/src/config.c \
	${TOP}/src/install.c \
	${TOP}/src/lex.yy.c \
	${TOP}/src/log.c \
	${TOP}/src/misc.c \
	${TOP}/src/parser.y \
	${TOP}/src/pref.c \
	${TOP}/src/restore.c \
	${TOP}/src/spalm.c \
	${TOP}/src/spc.c \
	${TOP}/src/net_compat.c \
	${TOP}/src/GenericConduit.cc

DISTFILES =	Makefile \
		messages.po \
		${POFILES}

CLEAN =		core *.core *.bak *~ errs errs.* ${MOFILES}
SPOTLESS =	messages.po

include ${TOP}/Make.rules

all::	${MOFILES}

messages.po::	${INPUTFILES}
	${XGETTEXT} -LC++ -s -k_ -kN_ -o $@ ${INPUTFILES}

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

# ${INSTALL_I18N} is either "yes" or "no", depending on whether 'configure'
# was run with "--without-i18n", and whether all the requisite tools could
# be found.
install::	install-i18n-${INSTALL_I18N}

# Install the message catalogs
install-i18n-yes:	${MOFILES}
	@langs="${LANGS}"; \
	for lang in $$langs; do \
		echo "${MKDIR} ${LOCALEDIR}/$$lang/LC_MESSAGES"; \
		${MKDIR} "${LOCALEDIR}/$$lang/LC_MESSAGES"; \
		${INSTALL_DATA} "$$lang.mo" \
			"${LOCALEDIR}/$$lang/LC_MESSAGES/${PACKAGE}.mo"; \
	done

# Do nothing.
install-i18n-no:

${TOP}/src/lex.yy.c:
	cd ${TOP}/src && ${MAKE} lex.yy.c

# This is for Emacs's benefit:
# Local Variables:	***
# fill-column:	75	***
# End:			***
