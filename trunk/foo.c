#include <stdio.h>
#include <sys/types.h>	/* For write() */
#include <sys/uio.h>	/* For write() */
#include <unistd.h>	/* For write() */
#include <fcntl.h>	/* For open() */
#include <termios.h>	/* For tcmakeraw() */
#include <sys/time.h>
#include <signal.h>
#include "padp.h"
#include "cmp.h"
#include "dlp.h"

extern int slp_debug;
extern int dlp_debug;

long find_highest_speed(int fd, struct termios *term);
long negotiate_speed(int fd, struct termios *term);
void hup_handler(int value);
void int_handler(int value);
void stty_dump(struct termios *term);
int open_tty(char *fname, speed_t speed);

static long speeds[] = {
	B300,
	B600,
	B1200,
	B1800,
	B2400,
	B4800,
	B7200,
	B9600,
	B14400,
	B19200,
	B28800,
	B38400,
#if 0
	B57600,
	B76800,
	B115200,
	B230400,
#endif
};
#define NUM_SPEEDS	sizeof(speeds)/sizeof(long)

ubyte ack[] = {
	0xbe, 0xef, 0xed, 0x03, 0x03, 0x02, 0x00, 0x04,
	0xff, 0xa5, 0x02, 0xc0, 0x00, 0x0a, 0x1a, 0x85,
};
ubyte dlp[] = {
	0xbe, 0xef, 0xed, 0x03, 0x03, 0x02, 0x00, 0x0e,
	0x02, 0xb2, 0x01, 0xc0, 0x00, 0x0a, 0x16, 0x01,
	0xa0, 0x00, 0x00, 0x04, 0x80, 0x00, 0x00, 0x00,
	0xd3, 0xaa,
};
ubyte dlp_endOfSync[] = {
	0x2f, 0x01, 0x20, 0x02, 0x00, 0x00,
};

int
main(int argc, char *argv[])
{
	int i;
	int err;
	int fd;
	struct termios term;
	struct cmp_packet pack;
static ubyte inbuf[1024];
static ubyte outbuf[1024];
struct cmp_packet cmp_in;
struct cmp_packet cmp_out;
fd_set in_fds, out_fds;
	uword dlp_arg; 

	if (argc != 2)
	{
		fprintf(stderr, "Usage: %s device\n", argv[0]);
		exit(1);
	}

signal(SIGHUP, &hup_handler);
	if ((fd = open_tty(argv[1], B9600)) < 0)
	{
		fprintf(stderr, "Error: can't open connection.\n");
		exit(1);
	}

slp_debug = 2;
dlp_debug = 3;

	cmp_recv(fd, &cmp_in);
	cmp_out.type = CMP_TYPE_INIT;
	cmp_out.flags = 0/*CMP_INITFLAG_CHANGERATE*/;
	cmp_out.ver_major = 0/*1*/;
	cmp_out.ver_minor = 0/*1*/;
	cmp_out.rate = 0/*B9600*/;
	cmp_send(fd, &cmp_out);
	if ((err = tcdrain(fd)) < 0)
		perror("tcdrain");
	fprintf(stderr, "***** Sent cmp INIT\n");

/*	close(fd);
	if ((fd = open_tty(argv[1], B9600)) < 0)
	{
		fprintf(stderr, "Error: can't reopen connection\n");
		exit(1);
	}
*/
tcgetattr(fd, &term);
stty_dump(&term);
/*  sleep(2); */

fprintf(stderr, "Adding a log message.\n");
if ((err = DlpAddSyncLogEntry(fd, "Hello, world!")) < 0)
fprintf(stderr, "DlpAddSyncLogEntry failed: %d\n", err);

	fprintf(stderr, "writing DLP\n");
	if ((err = DlpEndOfSync(fd, 0)) < 0)
		fprintf(stderr, "DlpEndOfSync failed: %d\n", err);
#if 0
	if ((err = dlp_send_req(fd, DLPREQ_ENDOFSYNC, 1)) < 0)
		fprintf(stderr, "Error writing request header\n");
	dlp_arg = 0;
	if ((err = dlp_send_arg(fd, 0x20, (ubyte *) &dlp_arg, 2)) < 0)
		fprintf(stderr, "Error writing request arg\n");
/*  	if ((err = padp_send(fd, dlp_endOfSync, sizeof(dlp))) < 0) */
/*  		perror("write(DLP)"); */
#endif	/* 0 */
	fprintf(stderr, "wrote DLP command\n");
/*	padp_recv(fd, inbuf, 1023);*/	/* PADP ACK */

fprintf(stderr, "Waiting for anything else\n");
while ((err = read(fd, inbuf, 1)) == 1)
{
	fprintf(stderr, "got 0x%02x\n", inbuf[0]);
}
fprintf(stderr, "read() returned %d\n", err);

	exit(0);
}

long
find_highest_speed(int fd, struct termios *term)
{
	int i;
	int err;
	long bestspeed = -1;

	for (i = NUM_SPEEDS-1; i >= 0; i--)
	{
		printf("Trying %ld baud...", speeds[i]);
		err = cfsetspeed(term, speeds[i]);
		if (err < 0)
		{
			perror("cfsetspeed");
			continue;
		}
		err = tcsetattr(fd, TCSANOW, term);
		if (err < 0)
		{
			perror("tcsetattr");
			continue;
		}
		printf("ok\n");
		if (bestspeed < 0)
			bestspeed = speeds[i];
	}
	return bestspeed;
}

long
negotiate_speed(int fd, struct termios *term)
{
	int i;
	int err;
	struct cmp_packet pack;
	struct timeval tp;

gettimeofday(&tp, NULL);
fprintf(stderr, "===== 1 %ld:%ld\n", tp.tv_sec, tp.tv_usec);

cmp_recv(fd, &pack);

gettimeofday(&tp, NULL);
fprintf(stderr, "===== 2 %ld:%ld\n", tp.tv_sec, tp.tv_usec);

pack.type = CMP_TYPE_INIT;
pack.flags = CMP_INITFLAG_CHANGERATE;
pack.rate = 57600/*9600*/;
pack.ver_major = 0/*1*/;
pack.ver_minor = 0;

gettimeofday(&tp, NULL);
fprintf(stderr, "===== 3 %ld:%ld\n", tp.tv_sec, tp.tv_usec);

cmp_send(fd, &pack);

gettimeofday(&tp, NULL);
fprintf(stderr, "===== 4 %ld:%ld\n", tp.tv_sec, tp.tv_usec);

cmp_recv(fd, &pack);
err = cfsetspeed(term, 57600);
if (err < 0)
{
	perror("cfsetspeed");
	return -1;
}
err = tcsetattr(fd, TCSANOW, term);
if (err < 0)
{
	perror("tcsetattr");
	return -1;
}

gettimeofday(&tp, NULL);
fprintf(stderr, "===== 5 %ld:%ld\n", tp.tv_sec, tp.tv_usec);

#if 0
	cmp_recv(fd, &pack);		/* Get the wakeup packet */
	for (i = NUM_SPEEDS-1; i >= 0; i--)
	{
		pack.type = CMP_TYPE_INIT;
		pack.flags = CMP_INITFLAG_CHANGERATE;
		pack.rate = speeds[i];
		cmp_send(fd, &pack);	/* Make a speed counteroffer */
		cmp_recv(fd, &pack);	/* Await response */

		if (pack.type == CMP_TYPE_INIT)
			return speeds[i];
	}
#endif
	return -1;
}

void
hup_handler(int value)
{
	fprintf(stderr, "===== hup_handler: received %d\n", value);
}

void
int_handler(int value)
{
	fprintf(stderr, "===== int_handler: received %d\n", value);
}

#define DUMPFLAG(field, flag)	\
	if (term->field & flag)	\
		printf(#flag " ");	\
	else				\
		printf("-" #flag " ");
void
stty_dump(struct termios *term)
{
	printf("termios:\n");
	printf("  c_iflag:\n\t");
	DUMPFLAG(c_iflag, IGNBRK);
	DUMPFLAG(c_iflag, BRKINT);
	DUMPFLAG(c_iflag, IGNPAR);
	DUMPFLAG(c_iflag, PARMRK);
	DUMPFLAG(c_iflag, INPCK);
	DUMPFLAG(c_iflag, ISTRIP);
	DUMPFLAG(c_iflag, INLCR);
	DUMPFLAG(c_iflag, IGNCR);
	DUMPFLAG(c_iflag, ICRNL);
	DUMPFLAG(c_iflag, IXON);
	DUMPFLAG(c_iflag, IXOFF);
	DUMPFLAG(c_iflag, IXANY);
	DUMPFLAG(c_iflag, IMAXBEL);
	printf("\n");
	printf("  c_oflag:\n\t");
	DUMPFLAG(c_oflag, OPOST);
	DUMPFLAG(c_oflag, ONLCR);
	DUMPFLAG(c_oflag, OXTABS);
	DUMPFLAG(c_oflag, ONOEOT);
	printf("\n");
	printf("  c_cflag:\n\t");
	DUMPFLAG(c_cflag, CIGNORE);
	DUMPFLAG(c_cflag, CS5);
	DUMPFLAG(c_cflag, CS6);
	DUMPFLAG(c_cflag, CS7);
	DUMPFLAG(c_cflag, CS8);
	DUMPFLAG(c_cflag, CSTOPB);
	DUMPFLAG(c_cflag, CREAD);
	DUMPFLAG(c_cflag, PARENB);
	DUMPFLAG(c_cflag, PARODD);
	DUMPFLAG(c_cflag, HUPCL);
	DUMPFLAG(c_cflag, CLOCAL);
	DUMPFLAG(c_cflag, CCTS_OFLOW);
	DUMPFLAG(c_cflag, CRTS_IFLOW);
	DUMPFLAG(c_cflag, CDTR_IFLOW);
	DUMPFLAG(c_cflag, CDSR_OFLOW);
	DUMPFLAG(c_cflag, MDMBUF);
	printf("\n");
	printf("  c_lflag:\n\t");
	DUMPFLAG(c_lflag, ECHOKE);
	DUMPFLAG(c_lflag, ECHOE);
	DUMPFLAG(c_lflag, ECHOK);
	DUMPFLAG(c_lflag, ECHO);
	DUMPFLAG(c_lflag, ECHONL);
	DUMPFLAG(c_lflag, ECHOPRT);
	DUMPFLAG(c_lflag, ECHOCTL);
	DUMPFLAG(c_lflag, ISIG);
	DUMPFLAG(c_lflag, ICANON);
	DUMPFLAG(c_lflag, ALTWERASE);
	DUMPFLAG(c_lflag, IEXTEN);
	DUMPFLAG(c_lflag, EXTPROC);
	DUMPFLAG(c_lflag, TOSTOP);
	DUMPFLAG(c_lflag, FLUSHO);
	DUMPFLAG(c_lflag, NOKERNINFO);
	DUMPFLAG(c_lflag, PENDIN);
	DUMPFLAG(c_lflag, NOFLSH);
	printf("\n");
	printf("  c_cc:\n");
	printf("  c_ispeed: %d\n", term->c_ispeed);
	printf("  c_ospeed: %d\n", term->c_ospeed);
}

int
open_tty(char *fname, speed_t speed)
{
	int fd;
	struct termios term;

	if (!strcmp(fname, "-"))
		fd = 0;
	else
		if ((fd = open(fname, O_RDWR)) < 0)
		{
			perror("open");
			return -1;
		}

	/* Set up the terminal _just so_. */
	tcgetattr(fd, &term);

	cfsetspeed(&term, speed);

	cfmakeraw(&term);

	term.c_cc[VMIN] = 10/*0*/;
	term.c_cc[VTIME] = 20/*0*/;
	term.c_lflag |= NOFLSH;
	term.c_iflag &= ~IGNBRK;
	term.c_iflag |= BRKINT;
	tcsetattr(fd, TCSANOW, &term);
tcgetattr(fd, &term);
stty_dump(&term);

	return fd; 
}
