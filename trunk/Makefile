CFLAGS +=	-g -Wall -ansi
# PALM_ROOT =	/usr/local/pilot/m68k-palmos-coff/include
# PALM_CPPFLAGS =	-I${PALM_ROOT}/PalmOS \
# 		-I${PALM_ROOT}/PalmOS/System \
# 		-I${PALM_ROOT}/PalmOS/UI \
# 		-I${PALM_ROOT}/PalmOS/Hardware
# CFLAGS +=	${PALM_CPPFLAGS}

# SLPOBJS =	slp.o palm_crc.o
# PADPOBJS =	padp.o
# CMPOBJS =	cmp.o
# DLPOBJS =	dlp.o dlp_cmd.o

LIBSRCS =	slp.c \
		padp.c \
		cmp.c \
		dlp.c dlp_cmd.c \
		util.c \
		PConnection.c
LIBOBJS =	${LIBSRCS:R:S/$/.o/g}

RM =		rm -f

# all:	libslp.a libpadp.a libcmp.a cmp.o foo
all:	libpalm.a foo

libpalm.a:	${LIBOBJS}
	${RM} ${.TARGET}
	ar cq ${.TARGET} `lorder ${LIBOBJS} | tsort -q`
	ranlib ${.TARGET}

clean::
	${RM} ${LIBOBJS} libpalm.a

#foo:	libslp.a libpadp.a libcmp.a libdlp.a foo.o
#	${CC} ${CFLAGS} -o ${.TARGET} foo.o -L. -ldlp -lcmp -lpadp -lslp
foo:	libpalm.a foo.o
	${CC} ${CFLAGS} -o ${.TARGET} foo.o -L. -lpalm

clean::
	${RM} foo.o foo

clean::
	${RM} *.bak core *.core
