# $Id: Makefile,v 1.13 2001-11-20 14:40:58 arensb Exp $

TOP =		..
SUBDIR =	doc

# XXX - This ought to include 'libpalm.texi', or whatever it mutates
# into, at some point.
TEXIFILES =	conduits.texi

INFOFILES =	${TEXIFILES:.texi=.info}
# EXTRA_INFOFILES is split off from INFOFILES because under DU, when there
# are no existing foo.info-* files, /bin/sh just expands the glob to the
# string "foo.info-*". Then 'make' complains that it doesn't know how to
# build this file.
EXTRA_INFOFILES =	${TEXIFILES:.texi=.info-*}
DVIFILES =	${TEXIFILES:.texi=.dvi}
PSFILES =	${TEXIFILES:.texi=.ps}
PDFFILES =	${TEXIFILES:.texi=.pdf}
MAN3FILES =	libpconn.3 \
		DlpAddSyncLogEntry.3 \
		DlpCallApplication.3 \
		DlpCleanUpDataBase.3 \
		DlpDeleteRecord.3 \
		DlpEndOfSync.3 \
		DlpGetSysDateTime.3 \
		DlpMoveCategory.3 \
		DlpOpenConduit.3 \
		DlpOpenDB.3 \
		DlpRPC.3 \
		DlpReadAppBlock.3 \
		DlpReadAppPreference.3 \
		DlpReadDBList.3 \
		DlpReadFeature.3 \
		DlpReadNetSyncInfo.3 \
		DlpReadOpenDBInfo.3 \
		DlpReadRecordByID.3 \
		DlpReadRecordIDList.3 \
		DlpReadResourceByIndex.3 \
		DlpReadStorageInfo.3 \
		DlpReadSysInfo.3 \
		DlpReadUserInfo.3 \
		DlpResetRecordIndex.3 \
		DlpResetSyncFlags.3 \
		DlpResetSystem.3 \
		DlpWriteRecord.3 \
		DlpWriteResource.3 \
		PConnClose.3 \
		PConn_bind.3 \
		RDLP_Backlight.3 \
		RDLP_BatteryDialog.3 \
		RDLP_GetOSVersionString.3 \
		RDLP_MemHandleNew.3 \
		RDLP_PluggedIn.3 \
		RDLP_ROMToken.3 \
		new_PConnection.3 \
		time_dlp2palmtime.3
		libpdb.3 \
		new_Record.3 \
		new_pdb.3 \
		pdb_AppendRecord.3 \
		pdb_CopyRecord.3 \
		pdb_DeleteRecordByID.3 \
		pdb_FindRecordByID.3 \
		pdb_LoadHeader.3 \
		pdb_Read.3
MAN8FILES =	coldsync.8

DISTFILES =	Makefile \
		${MAN3FILES} ${MAN8FILES} \
		${TEXIFILES} \
		libpalm.texi \
		texinfo.tex \
		version.texi.in \
		sample.coldsync.rc

EXTRA_DISTFILES = \
		${INFOFILES} \
		${EXTRA_INFOFILES} \
		${PSFILES}

CLEAN =		${INFOFILES} ${EXTRA_INFOFILES} ${DVIFILES} ${PSFILES} \
		*.aux *.cp *.cps *.fn *.ky *.log *.pg *.toc *.tp *.ty *.vr \
		*.bak *~ errs errs.* \
		*.html
DISTCLEAN =	version.texi .depend
SPOTLESS =

include ${TOP}/Make.rules

all::	${INFOFILES}

install::	coldsync.8 ${INFOFILES}
	-${MKDIR} ${MAN3DIR}
	@for i in ${MAN3FILES}; do \
		echo ${INSTALL_MAN} $$i ${MAN3DIR}/$$i; \
		${INSTALL_MAN} $$i ${MAN3DIR}/$$i; \
	done
	-${MKDIR} ${MAN8DIR}
	@for i in ${MAN8FILES}; do \
		echo ${INSTALL_MAN} $$i ${MAN8DIR}/$$i; \
		${INSTALL_MAN} $$i ${MAN8DIR}/$$i; \
	done
	-${MKDIR} ${SYSCONFDIR}
	${INSTALL_DATA} sample.coldsync.rc ${SYSCONFDIR}/sample.coldsync.rc
	${MKDIR} ${INFODIR}
	@for file in ${INFOFILES} ${EXTRA_INFOFILES}; do \
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
