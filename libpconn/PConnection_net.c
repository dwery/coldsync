/* PConnection_net.c
 */
#include "config.h"
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>		/* For socket() */
#include <netinet/in.h>		/* For sockaddr_in, htonl() etc. */
#include <string.h>		/* For memset() */

#if HAVE_LIBINTL_H
#  include <libintl.h>		/* For i18n */
#endif	/* HAVE_LIBINTL_H */

#include "pconn/PConnection.h"
#include "pconn/netsync.h"
#include "pconn/util.h"

static int net_udp_listen(struct PConnection *pconn,
			  struct netsync_wakeup *wakeup_pkt);
static int net_acknowledge_wakeup(struct PConnection *pconn,
				  struct netsync_wakeup *wakeup_pkt);
static int net_tcp_listen(struct PConnection *pconn);

/* XXX - These variables are global *ONLY* for testing */
/*  int sockfd; */
static struct sockaddr_in cliaddr;
static socklen_t cliaddr_len;
static struct sockaddr_in cliaddr_tcp;
static socklen_t cliaddr_tcp_len;
int data_sock;		/* Client on which to send/receive data */
/*  static ubyte seqno; */		/* Sequence number */

/* Ritual statements
 * These packets are sent back and forth during the initial handshaking
 * phase. I don't know what they mean. The sequence is:
 * client sends UDP wakeup packet
 * server sends UDP wakeup ACK
 * client sends ritual response 1
 * server sends ritual statement 2
 * client sends ritual response 2
 * server sends ritual statement 3
 * client sends ritual response 3
 *
 * The comments are mostly conjecture and speculation.
 */
/* XXX - The ritual responses are commented out for now to shut the
 * compiler up. They'll be restored when we're able to initiate a
 * connection to a remote host.
 */
#if 0
static ubyte ritual_resp1[] = {
	0x90,				/* Command */
	0x01,				/* argc */
	0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x20,		/* Arg ID */
	0x00, 0x00, 0x00, 0x08,		/* Arg length */
	/* Arg data */
	0x00, 0x00, 0x00, 0x01,
	0x80, 0x00, 0x00, 0x00,
};
#endif	/* 0 */

static ubyte ritual_stmt2[] = {
	0x12,				/* Command */
	0x01,				/* argc */
	0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x20,		/* Arg ID */
	0x00, 0x00, 0x00, 0x24,		/* Arg length */
	/* Arg data */
	0xff, 0xff, 0xff, 0xff,
	0x3c, 0x00,			/* These are reversed in the
					 * response */
	0x3c, 0x00,
	0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00,
	0xc0, 0xa8, 0xa5, 0x1f,		/* 192.168.165.31 */
	0x04, 0x27, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00,
};

#if 0
static ubyte ritual_resp2[] = {
	0x92,				/* Command */
	0x01,				/* argc */
	0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x20,		/* Arg ID */
	0x00, 0x00, 0x00, 0x24,		/* Arg length */
	/* Arg data */
	0xff, 0xff, 0xff, 0xff,
	0x00, 0x3c,
	0x00, 0x3c,
	0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x01,
	0xc0, 0xa8, 0x84, 0x3c,		/* 192.168.132.60
					 * Presumably, this is the IP
					 * address (or hostid) of the
					 * sender.
					 */
	0x04, 0x1c, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00,
};
#endif	/* 0 */

static ubyte ritual_stmt3[] = {
	0x13,				/* Command */
	0x01,				/* argc */
	0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x20,		/* Arg ID */
	0x00, 0x00, 0x00, 0x20,		/* Arg length */
	/* Arg data
	 * This is very similar to ritual statement/response 2.
	 */
	0xff, 0xff, 0xff, 0xff,
	0x00, 0x3c,
	0x00, 0x3c,
	0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x01,
	0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00,
};

#if 0
static ubyte ritual_resp3[] = {
	0x93,				/* Command */
	0x00,				/* argc? */
	0x00, 0x00, 0x00, 0x00,
	0x00, 0x00,
};
#endif	/* 0 */

static int
net_read(struct PConnection *p, unsigned char *buf, int len)
{
	return read(p->fd, buf, len);
}

static int
net_write(struct PConnection *p, unsigned char *buf, int len)
{
	return read(p->fd, buf, len);
}

static int
net_accept(struct PConnection *p)
{
	struct netsync_wakeup wakeup_pkt;

	/* XXX - This function should accept the wakeup packet from the
	 * client, respond to it, replace the current pconn->fd with a TCP
	 * socket, exchange ritual packets with the client, and finally
	 * return.
	 */
	net_udp_listen(p, &wakeup_pkt);
	/* XXX - Error-checking */

	net_acknowledge_wakeup(p, &wakeup_pkt);
	/* XXX - Error-checking */

	net_tcp_listen(p);
	/* XXX - Error-checking */

	return 0;
}

static int
net_close(struct PConnection *p)
{
	/* Clean up the protocol stack elements */
	dlp_tini(p);
	netsync_tini(p);

	return close(p->fd);
}

static int
net_select(struct PConnection *p,
	      pconn_direction which,
	      struct timeval *tvp)
{
	fd_set fds;

	FD_ZERO(&fds);
	FD_SET(p->fd, &fds);

	return (which == forReading) ? select(p->fd+1, &fds, NULL, NULL, tvp)
				     : select(p->fd+1, NULL, &fds, NULL, tvp);
}

static int
net_setspeed(struct PConnection *pconn, int speed)
{
	/* This is meaningless on a network, so this always succeeds */
	return 0;
}

static int
net_drain(struct PConnection *p)
{
	/* I don't think there's a network equivalent of flushing a stream
	 * or tty connection.
	 */
	return 0;
}

int
pconn_net_open(struct PConnection *pconn, char *device, int prompt)
{
	int err;
	struct sockaddr_in myaddr;

	IO_TRACE(1)
		fprintf(stderr, "Opening net connection.\n");

	/* Initialize the various protocols that the network connection
	 * will use.
	 */
	/* Initialize the DLP part of the PConnection */
	if (dlp_init(pconn) < 0)
	{
		dlp_tini(pconn);
		return -1;
	}

	/* Initialize the NetSync part of the PConnnection */
	if (netsync_init(pconn) < 0)
	{
		dlp_tini(pconn);
		netsync_tini(pconn);
		return -1;
	}

	/* Set the methods used by the network connection */
	pconn->io_read = &net_read;
	pconn->io_write = &net_write;
	pconn->io_accept = &net_accept;
	pconn->io_close = &net_close;
	pconn->io_select = &net_select;
	pconn->io_setspeed = &net_setspeed;
	pconn->io_drain = &net_drain;
	pconn->io_private = 0;

	/* Create a socket
	 * Although we'll use pconn->fd for both the UDP and TCP sockets,
	 * for now, we'll just create the UDP socket.
	 */
	pconn->fd = socket(AF_INET, SOCK_DGRAM, 0);
	if (pconn->fd < 0)
		return pconn->fd;	/* Error */

	IO_TRACE(5)
		fprintf(stderr, "UDP socket == %d\n", pconn->fd);

	/* Bind the UDP socket to the NetSync wakeup port */
	bzero(&myaddr, sizeof(myaddr));
	myaddr.sin_family = AF_INET;
	myaddr.sin_addr.s_addr = htonl(INADDR_ANY);
				/* Listen on all interfaces */
				/* XXX - Perhaps this ought to be
				 * configurable. */
	myaddr.sin_port = htons(NETSYNC_WAKEUP_PORT);
				/* XXX - Ought to look this up in
				 * /etc/services, and default to
				 * NETSYNC_WAKEUP_PORT if not found.
				 */

	IO_TRACE(4)
		fprintf(stderr, "bind()ing to %d\n",
			ntohs(myaddr.sin_port));
	err = bind(pconn->fd, (struct sockaddr *) &myaddr, sizeof(myaddr));
	if (err < 0)
	{
		perror("bind");
		close(pconn->fd);
		return -1;
	}

	IO_TRACE(5)
		fprintf(stderr, "Returning socket %d\n", pconn->fd);

	return pconn->fd;
}

/* XXX - Highly experimental function. */
static int
net_udp_listen(struct PConnection *pconn,
	       struct netsync_wakeup *wakeup_pkt)
{
/*  	int err; */
	int len;
/*  	int sockfd; */
	ubyte buf[1024];
	const ubyte *rptr;		/* Pointer into buffer, for reading */
/*  	struct sockaddr_in servaddr; */
/*  	struct sockaddr_in cliaddr; */
/*  	socklen_t cliaddr_len; */

#if 0
	fprintf(stderr, "Calling socket().\n");
	sockfd = socket(AF_INET, SOCK_DGRAM, 0);
	if (sockfd < 0)
	{
		perror("socket");
		return -1;
	}

	bzero(&servaddr, sizeof(servaddr));
	servaddr.sin_family = AF_INET;
	servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
				/* XXX - Does this mean it accepts from any
				 * host?
				 */
	servaddr.sin_port = htons(NETSYNC_WAKEUP_PORT);
				/* XXX - Ought to look this up in
				 * /etc/services, and default to
				 * NETSYNC_WAKEUP_PORT if not found.
				 */

	fprintf(stderr, "bind()ing to %d\n", ntohs(servaddr.sin_port));
	err = bind(sockfd, (struct sockaddr *) &servaddr, sizeof(servaddr));
	if (err < 0)
	{
		perror("bind");
		return -1;
	}
#endif	/* 0 */

	/* Receive a datagram from a client */
  retry:
	cliaddr_len = sizeof(cliaddr);
	len = recvfrom(pconn->fd, buf, sizeof(buf), 0,
		       (struct sockaddr *) &cliaddr, &cliaddr_len);

	fprintf(stderr, "recvfrom() returned %d\n", len);
	if (len < 0)
	{
		perror("recvfrom");
		goto retry;
	} else {
		fprintf(stderr,
			"Got datagram from host 0x%08lx (%d.%d.%d.%d), port %d, length %d\n",
			(unsigned long) cliaddr.sin_addr.s_addr,
			(int)  (cliaddr.sin_addr.s_addr        & 0xff),
			(int) ((cliaddr.sin_addr.s_addr >>  8) & 0xff),
			(int) ((cliaddr.sin_addr.s_addr >> 16) & 0xff),
			(int) ((cliaddr.sin_addr.s_addr >> 24) & 0xff),
			cliaddr.sin_port,
			cliaddr_len);
		debug_dump(stderr, "UDP", buf, len);
	}

	/* Parse the alleged wakeup packet */
	rptr = buf;
	wakeup_pkt->magic = get_uword(&rptr);
	wakeup_pkt->type = get_ubyte(&rptr);
	wakeup_pkt->unknown = get_ubyte(&rptr);
	wakeup_pkt->hostid = get_udword(&rptr);
	wakeup_pkt->netmask = get_udword(&rptr);
	memcpy(wakeup_pkt->hostname, rptr, len - (rptr-buf));

	fprintf(stderr, "Got wakeup packet:\n");
	fprintf(stderr, "\tmagic == 0x%04x\n", wakeup_pkt->magic);
	fprintf(stderr, "\ttype == 0x%02x\n", wakeup_pkt->type);
	fprintf(stderr, "\tunknown == 0x%02x\n", wakeup_pkt->unknown);
	fprintf(stderr, "\thostid == 0x%08lx (%d.%d.%d.%d)\n",
		wakeup_pkt->hostid,
		(int) ((wakeup_pkt->hostid >> 24) & 0xff),
		(int) ((wakeup_pkt->hostid >> 16) & 0xff),
		(int) ((wakeup_pkt->hostid >>  8) & 0xff),
		(int)  (wakeup_pkt->hostid        & 0xff));
	fprintf(stderr, "\tnetmask == 0x%08lx (%d.%d.%d.%d)\n",
		wakeup_pkt->netmask,
		(int) ((wakeup_pkt->netmask >> 24) & 0xff),
		(int) ((wakeup_pkt->netmask >> 16) & 0xff),
		(int) ((wakeup_pkt->netmask >>  8) & 0xff),
		(int)  (wakeup_pkt->netmask        & 0xff));
	fprintf(stderr, "\tHostname: \"%s\"\n", wakeup_pkt->hostname);

	if (wakeup_pkt->magic != NETSYNC_WAKEUP_MAGIC)
	{
		fprintf(stderr, "This is not a wakeup packet.\n");
		goto retry;
	}

	return 0;
}

static int
net_acknowledge_wakeup(struct PConnection *pconn,
		       struct netsync_wakeup *wakeup_pkt)
{
	int err;
	ubyte outbuf[1024];
	ubyte *wptr;			/* Pointer into buffer, for writing */
	size_t pkt_len;

	/* Write the packet to outbuf */
	/* XXX - How much of this is necessary? Does it need the address,
	 * netmask, hostname?
	 */
	wptr = outbuf;
	put_uword(&wptr, NETSYNC_WAKEUP_MAGIC);
	put_ubyte(&wptr, 2);		/* XXX - 2 == response to wakeup */
	put_ubyte(&wptr, wakeup_pkt->unknown);
	put_udword(&wptr, wakeup_pkt->hostid);
	put_udword(&wptr, wakeup_pkt->netmask);
	memcpy(wptr, wakeup_pkt->hostname, strlen(wakeup_pkt->hostname)+1);
	wptr += strlen(wakeup_pkt->hostname)+1;
	pkt_len = wptr - outbuf;

	fprintf(stderr, "Sending acknowledgment.\n");
	err = sendto(pconn->fd, outbuf, pkt_len, 0,
		     (struct sockaddr *) &cliaddr, cliaddr_len);
	if (err < 0)
	{
		perror("sendto");
		return -1;
	}

	fprintf(stderr, "Closing UDP socket.\n");
	err = close(pconn->fd);
	fprintf(stderr, "close() returned %d\n", err);
	if (err < 0)
		perror("close");

	return 0;
}

/* XXX - This should be net_accept() */
static int
net_tcp_listen(struct PConnection *pconn)
{
	int err;
	struct sockaddr_in servaddr;
#if 0
	ubyte outbuf_userinfo[] = {
		DLPCMD_ReadUserInfo,	/* Command */
		0x00,			/* argc */
	};
	ubyte outbuf_end[] = {
		DLPCMD_EndOfSync,	/* Command */
		0x01,			/* argc */
		/* arg 0 */
		0x00, 0x00, 0x00, 0x00,	/* ? */
		0x00, 0x00, 0x00, 0x20,	/* arg ID */
		0x00, 0x00, 0x00, 0x01,	/* arg 0 length */
		0x00,			/* arg 0 data: 0 = OK */
		0x00,			/* Padding? */
	};
#endif	/* 0 */
	const ubyte *inbuf;
	uword inlen;

	fprintf(stderr, "Inside net_tcp_listen()\n");

	fprintf(stderr, "Creating TCP socket.\n");
	pconn->fd = socket(AF_INET, SOCK_STREAM, 0);
	if (pconn->fd < 0)
	{
		perror("socket");
		return -1;
	}

	bzero(&servaddr, sizeof(servaddr));
	servaddr.sin_family = AF_INET;
	servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
	servaddr.sin_port = htons(NETSYNC_DATA_PORT);
				/* XXX - Ought to look this up in
				 * /etc/services and default to
				 * NETSYNC_DATA_PORT if not found.
				 */

	fprintf(stderr, "binding\n");
	err = bind(pconn->fd, (struct sockaddr *) &servaddr, sizeof(servaddr));
	if (err < 0)
	{
		perror("bind");
		return -1;
	}

	fprintf(stderr, "listening\n");
	err = listen(pconn->fd, 5);	/* XXX - What's a good value for
					 * the backlog?
					 */
	if (err < 0)
	{
		perror("listen");
		return -1;
	}

	fprintf(stderr, "accepting\n");
	cliaddr_tcp_len = sizeof(cliaddr_tcp);
	data_sock = accept(pconn->fd, (struct sockaddr *) &cliaddr_tcp,
			   &cliaddr_tcp_len);
	if (data_sock < 0)
	{
		perror("accept");
		return -1;
	}
	fprintf(stderr,
		"Accepted TCP connection from 0x%08lx (%d.%d.%d.%d), port %d\n",
		(unsigned long) cliaddr_tcp.sin_addr.s_addr,
		(int)  (cliaddr_tcp.sin_addr.s_addr        & 0xff),
		(int) ((cliaddr_tcp.sin_addr.s_addr >>  8) & 0xff),
		(int) ((cliaddr_tcp.sin_addr.s_addr >> 16) & 0xff),
		(int) ((cliaddr_tcp.sin_addr.s_addr >> 24) & 0xff),
		cliaddr_tcp.sin_port);

	/* Receive ritual response 1 */
	err = netsync_read(NULL, &inbuf, &inlen);
	fprintf(stderr, "netsync_read returned %d\n", err);
	debug_dump(stderr, "<<<", inbuf, inlen);

	/* Send ritual statement 2 */
	err = netsync_write(NULL, ritual_stmt2, sizeof(ritual_stmt2));
	fprintf(stderr, "netsync_write(ritual stmt 2) returned %d\n",
		err);

	/* Receive ritual response 2 */
	err = netsync_read(NULL, &inbuf, &inlen);
	fprintf(stderr, "netsync_read returned %d\n", err);
	debug_dump(stderr, "<<<", inbuf, inlen);

	/* Send ritual statement 3 */
	err = netsync_write(NULL, ritual_stmt3, sizeof(ritual_stmt3));
	fprintf(stderr, "netsync_write(ritual stmt 3) returned %d\n",
		err);

	/* Receive ritual response 3 */
	err = netsync_read(NULL, &inbuf, &inlen);
	fprintf(stderr, "netsync_read returned %d\n", err);
	debug_dump(stderr, "<<<", inbuf, inlen);

#if 0
	/* Send a DLP request */
	err = netsync_write(NULL, outbuf_userinfo,
			    sizeof(outbuf_userinfo));

	err = netsync_read(NULL, &inbuf, &inlen);
	fprintf(stderr, "netsync_read returned %d\n", err);
	debug_dump(stderr, "<<<", inbuf, inlen);

	/* Send a DLP SyncEnd request */
	err = netsync_write(NULL, outbuf_end,
			    sizeof(outbuf_end)/sizeof(outbuf_end[0]));

	err = netsync_read(NULL, &inbuf, &inlen);
	fprintf(stderr, "netsync_read returned %d\n", err);
	debug_dump(stderr, "<<<", inbuf, inlen);
#endif	/* 0 */

	return 0;
}

/* This is for Emacs's benefit:
 * Local Variables: ***
 * fill-column:	75 ***
 * End: ***
 */
