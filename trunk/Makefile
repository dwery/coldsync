CFLAGS +=	-g -Wall -ansi

SLPOBJS =	slp.o palm_crc.o
PADPOBJS =	padp.o
CMPOBJS =	cmp.o
DLPOBJS =	dlp.o dlp_cmd.o

RM =		rm -f

all:	libslp.a libpadp.a libcmp.a cmp.o foo

libslp.a:	${SLPOBJS}
	${RM} libslp.a
	ar cq ${.TARGET} `lorder ${SLPOBJS} | tsort -q`
	ranlib ${.TARGET}

clean::
	${RM} ${SLPOBJS} libslp.a

libpadp.a:	libslp.a ${PADPOBJS}
	${RM} libpadp.a
	ar cq ${.TARGET} `lorder ${PADPOBJS} | tsort -q`
	ranlib ${.TARGET}

clean::
	${RM} ${PADPOBJS} libpadp.a

libcmp.a:	${CMPOBJS}
	${RM} libcmp.a
	ar cq ${.TARGET} `lorder ${CMPOBJS} | tsort -q`
	ranlib ${.TARGET}

clean::
	${RM} ${CMPOBJS} libcmp.a

libdlp.a:	${DLPOBJS}
	${RM} libdlp.a
	ar cq ${.TARGET} `lorder ${DLPOBJS} | tsort -q`
	ranlib ${.TARGET}

clean::
	${RM} ${DLPOBJS} libdlp.a

foo:	libslp.a libpadp.a libcmp.a libdlp.a foo.o
	${CC} ${CFLAGS} -o ${.TARGET} foo.o -L. -ldlp -lcmp -lpadp -lslp

clean::
	${RM} foo.o foo

clean::
	${RM} *.bak core *.core
