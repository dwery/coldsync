/*
 * USB system call interface under Linux.
 */

#include "config.h"
#if defined(__linux__)

#include <sys/types.h>
#include <stdio.h>
#include <fcntl.h>
#include <termios.h>
#include <errno.h>

#include "usb_generic.h"

#include "pconn/PConnection.h"

#if HAVE_LIBINTL_H
#include <libintl.h>
#endif /* HAVE_LIBINTL_H */


int
linux_usb_open(const char *device, int prompt, void **private)
{
	int fd;
	struct termios term;

        /* Open the device.
         * This is rather funky, due to the fact that the Palm as
         * a USB device doesn't exist until the sync starts, but
         * under Linux, you use it as if it were a serial port.
         *
         * Under Linux, open() returns ENXIO if the device doesn't
         * exist at all, and ENODEV if it doesn't exist at the
         * moment. I don't think open() ever returns ENODEV under
         * any other OS, so this should be okay. Otherwise, it
         * might be necessary to check the major and/or minor
         * device numbers under Linux, and add other Linux-specfic
         * hacks.
	 *
	 *
	 * [mbd]
	 * Hack is too strong a word, but this entire is Linux
	 * specific since the Linux OS interface to USB is completely
	 * different from the FreeBSD one.  Trying to merge them
	 * both into a single file would just be a mess of #ifdefs.
	 *
	 * With that in mind.  Is the O_BINARY really needed under
	 * Linux?  I strongly suspect not.
         */
	while (1) {
		fd = open(device, O_RDWR | O_BINARY);
		if (fd != -1)
			break;

		switch (errno) {
		case ENODEV:
			fprintf(stderr, _("Warning: no device on %s.  "
			    "Sleeping\n"), device);
			break;
		default:
			fprintf(stderr, _("Error: Can't open \"%s\".\n"),
			    device);
			perror("open");
			return (-1);
		}
		sleep(5);
	}

	/* Set up terminal characteristics */
	tcgetattr(fd, &term);

	/* Set initial rate.  9600 bps required for handshaking */
	cfsetispeed(&term, B9600);
	cfsetospeed(&term, B9600);
	cfmakeraw(&term);

	/* XXX - Error-checking */
	tcsetattr(fd, TCSANOW, &term);
	/* XXX - Error-checking */


	if (prompt)
		printf(_("Please press the HotSync button.\n"));

	return (fd);
}

int
linux_usb_read(int fd, unsigned char *buf, int len, void *private)
{

	return (read(fd, buf, len));
}

int
linux_usb_write(int fd, unsigned const char *buf, const int len,
    void *private)
{

	return (write(fd, buf, len));
}

int
linux_usb_select(int fd, int for_reading, struct timeval *tvp, void *private)
{
	fd_set fds;
	int sel;

	FD_ZERO(&fds);
	FD_SET(fd, &fds);

	if (for_reading)
		sel = select(fd + 1, &fds, NULL, NULL, tvp);
	else
		sel = select(fd + 1, NULL, &fds, NULL, tvp);
	return (sel);
}

int
linux_usb_close(int fd, void *private)
{

	return (fd >= 0 ? close(fd) : 0);
}
#else
/* shut up a warning about empty  source */
static const int x = 0;
#endif /* defined(__linux__) */
