/* PConnection_net.c
 *
 *	Copyright (C) 2000-2001, Andrew Arensburger.
 *	You may distribute this file under the terms of the Artistic
 *	License, as specified in the README file.
 *
 * $Id: PConnection_net.c,v 1.25 2002-04-27 17:17:35 azummo Exp $
 */
#include "config.h"
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>		/* For socket() */
#include <netinet/in.h>		/* For sockaddr_in, htonl() etc. */
#include <arpa/inet.h>		/* For inet_pton() */
#include <netdb.h>		/* For getservbyname() */
#include <string.h>		/* For memset() */
#if HAVE_INET_NTOP
#  include <arpa/nameser.h>	/* Solaris's <resolv.h> requires this */
#  include <resolv.h>		/* For inet_ntop() under Solaris */
#endif	/* HAVE_INET_NTOP */

#if HAVE_STRINGS_H
#  include <strings.h>		/* For bzero() */
#endif	/* HAVE_STRINGS_H */

#if HAVE_LIBINTL_H
#  include <libintl.h>		/* For i18n */
#endif	/* HAVE_LIBINTL_H */

#include "pconn/PConnection.h"
#include "pconn/netsync.h"
#include "pconn/util.h"

/*
 * Moved from netsync.h to here, since it only applies to real netsyncing,
 * not to the Palm m50x USB cradle protocol.
 *
 * The machine with the cradle is the client. It talks to the server host,
 * which will do the actual work of syncing.
 *
 * The client starts out by sending one or more wakeup packets to the
 * server, on UDP port 14237. These packets are of the form
 *	+------+------+------+------+
 *	|    magic    | type |   ?  |
 *	+------+------+------+------+
 *	| hostid                    |
 *	+------+------+------+------+
 *	| netmask                   |
 *	+------+------+------+------+
 *	| NUL-terminated hostname...
 *	+------+------+------+------+
 *
 * Where <magic> is the constant 0xfade, <type> appears to be the type of
 * request (1 == wakeup packet, 2 == ACK). <hostid> and <netmask> are the
 * IPv4 address and netmask of the host to sync with. <hostname> is the
 * name of the host. HotSync appears to use the address as authoritative,
 * and presumably sends the name along mainly for the server's benefit.
 *
 * The server then sends back a UDP datagram with the same information,
 * except that <type> is set to 2. The client will send up to 3 wakeup
 * requests before giving up. Once the server has acknowledged the wakeup
 * packet (thereby accepting  * the connection), it listens on TCP port 14238. 
 */

/* XXX - This is just for debugging, I think */
/* INET_NTOP
 * This is a hack, intended to support both those systems that have
 * inet_ntop() and those that don't. Note that the second argument is not a
 * pointer. In one case, the macro uses &(addr), in the other it doesn't.
 */
#if HAVE_INET_NTOP
/* XXX - Solaris 2.6 or 7 appears to have this, but doesn't have a prototype.
 * Add one if necessary:
 *     const char *
 *     inet_ntop(int af, const void *src, char *dst, size_t size)
 */
#  define INET_NTOP(af, addr, buf, buflen) \
	inet_ntop((af), &(addr), (buf), (buflen))
#else
#  define INET_NTOP(af, addr, buf, buflen) \
	inet_ntoa(addr)
#endif	/* HAVE_INET_NTOP */

static int net_udp_listen(
	PConnection *pconn,
	struct netsync_wakeup *wakeup_pkt,
	struct sockaddr_in *cliaddr,
	socklen_t *cliaddr_len);
static int net_acknowledge_wakeup(
	PConnection *pconn,
	struct netsync_wakeup *wakeup_pkt,
	struct sockaddr_in *cliaddr,
	socklen_t *cliaddr_len);
static int net_tcp_listen(PConnection *pconn);

static int
net_bind(PConnection *pconn,
	 const void *addr,
	 const int addrlen)
{
	struct sockaddr_in myaddr;
	int err;
	struct servent *service;	/* NetSync wakeup service entry */

	service = getservbyname("netsync-wakeup", "udp");
				/* Try to get the entry for
				 * "netsync-wakeup" from /etc/services
				 */
	IO_TRACE(2)
	{
		if (service != NULL)
		{
			int i;

			fprintf(stderr, "Got entry for netsync-wakeup/udp:\n");
			fprintf(stderr, "\tname: \"%s\"\n", service->s_name);
			fprintf(stderr, "\taliases:\n");
			for (i = 0; service->s_aliases[i] != NULL; i++)
				fprintf(stderr, "\t\t\"%s\"\n",
					service->s_aliases[i]);
			fprintf(stderr, "\tport: %d\n",
				ntohs(service->s_port));
			fprintf(stderr, "\tprotocol: \"%s\"\n",
				service->s_proto);
		} else {
			fprintf(stderr, "No entry for netsync-wakeup/udp\n");
		}
	}

	/* Bind the UDP socket to the NetSync wakeup port */
	/* XXX - This assumes IPv4. Make it work with IPv6 */
	memcpy(&myaddr, addr, sizeof(struct sockaddr_in));
	myaddr.sin_family = AF_INET;
	myaddr.sin_addr.s_addr = htonl(INADDR_ANY);
				/* Listen on all interfaces */
				/* XXX - Perhaps this ought to be
				 * configurable. */
	if (service == NULL)
		myaddr.sin_port = htons(NETSYNC_WAKEUP_PORT);
	else
		myaddr.sin_port = service->s_port;
				/* Port is already in network byte order */

	IO_TRACE(4)
		fprintf(stderr, "bind()ing to %d\n",
			ntohs(myaddr.sin_port));
	err = bind(pconn->fd,
		   (struct sockaddr *) &myaddr,
		   sizeof(struct sockaddr_in));
	if (err < 0)
	{
		perror("bind");
		if (pconn->fd >= 0)
			close(pconn->fd);
		return -1;
	}

	return 0;
}

static int
net_read(PConnection *p, unsigned char *buf, int len)
{
	return read(p->fd, buf, len);
}

static int
net_write(PConnection *p, unsigned const char *buf, const int len)
{
	return write(p->fd, buf, len);
}

static int
net_connect_udp(PConnection *pconn, const void *addr, const int addrlen)
{
	int err;
	ubyte outbuf[1024];		/* XXX - Fixed size bad */
	ubyte *wptr;			/* Pointer into outbuf, for writing */
	ubyte inbuf[1024];		/* XXX - Fixed size bad */
	const ubyte *rptr;		/* Pointer into inbuf, for reading */
	size_t pkt_len;
	int len;
	struct sockaddr_in servaddr;
	socklen_t servaddr_len;
	struct netsync_wakeup wakeup_pkt;

	/* Copy the given address */
	/* XXX - This assumes IPv4 */
	memcpy(&servaddr, addr, sizeof(struct sockaddr_in));

	IO_TRACE(3)
	{
#if HAVE_INET_NTOP
		char namebuf[128];
#endif	/* HAVE_INET_NTOP */

		fprintf(stderr, "Inside net_connect(%s), port %d\n",
			INET_NTOP(servaddr.sin_family,
				  servaddr.sin_addr,
				  namebuf, 128),
			ntohs(servaddr.sin_port));
	}

	/* Send UDP wakeup */
	wptr = outbuf;
	put_uword(&wptr, NETSYNC_WAKEUP_MAGIC);
	put_ubyte(&wptr, 1);		/* XXX - 1 == wakeup */
	put_ubyte(&wptr, 0);
	put_udword(&wptr, 0xc0a8843c);	/* XXX - Should be hostid */
	put_udword(&wptr, 0xffffff00);	/* XXX - Should be netmask */
	memcpy(wptr, "baa", 4);		/* XXX */
	wptr += 4;			/* XXX - Should be length of
					 * hostname+1 */
	pkt_len = wptr - outbuf;

	/* XXX - Doesn't work with broadcast addresses: need to
	 * setsockopt(SO_BROADCAST) first.
	 */
	IO_TRACE(3)
		fprintf(stderr, "Sending wakeup.\n");
	err = sendto(pconn->fd, (const char *) outbuf, pkt_len, 0,
		     (struct sockaddr *) &servaddr,
		     addrlen);
	if (err < 0)
	{
		perror("sendto");
		return -1;
	}

	/* Receive UDP wakeup ACK */
	/* XXX - This hangs forever if the remote host doesn't send a
	 * wakeup ACK. Add a timeout somehow.
	 */
  retry:
	len = recvfrom(pconn->fd, (char *) inbuf, sizeof(inbuf), 0,
		       (struct sockaddr *) &servaddr,
		       &servaddr_len);

	fprintf(stderr, "recvfrom() returned %d\n", len);
	if (len < 0)
	{
		perror("recvfrom");
		goto retry;
	} else {
#if HAVE_INET_NTOP
		char namebuf[128];
#endif	/* HAVE_INET_NTOP */

		fprintf(stderr,
			"Got datagram from host 0x%08lx (%d.%d.%d.%d), "
			"port %d, length %ld\n",
			(unsigned long) servaddr.sin_addr.s_addr,
			(int)  (servaddr.sin_addr.s_addr	& 0xff),
			(int) ((servaddr.sin_addr.s_addr >>  8) & 0xff),
			(int) ((servaddr.sin_addr.s_addr >> 16) & 0xff),
			(int) ((servaddr.sin_addr.s_addr >> 24) & 0xff),
			servaddr.sin_port,
			(long) servaddr_len);
		debug_dump(stderr, "UDP", inbuf, len);
		fprintf(stderr, "servaddr says host [%s]\n",
			INET_NTOP(servaddr.sin_family,
				  servaddr.sin_addr,
				  namebuf, 128));
	}

	/* Parse the alleged wakeup packet */
	rptr = inbuf;
	wakeup_pkt.magic = get_uword(&rptr);
	wakeup_pkt.type = get_ubyte(&rptr);
	wakeup_pkt.unknown = get_ubyte(&rptr);
	wakeup_pkt.hostid = get_udword(&rptr);
	wakeup_pkt.netmask = get_udword(&rptr);
	memcpy(wakeup_pkt.hostname, rptr, len - (rptr-inbuf));

	/* XXX - Wrap in IO_TRACE */
	fprintf(stderr, "Got wakeup ACK packet:\n");
	fprintf(stderr, "\tmagic == 0x%04x\n", wakeup_pkt.magic);
	fprintf(stderr, "\ttype == 0x%02x\n", wakeup_pkt.type);
	fprintf(stderr, "\tunknown == 0x%02x\n", wakeup_pkt.unknown);
	fprintf(stderr, "\thostid == 0x%08lx (%d.%d.%d.%d)\n",
		wakeup_pkt.hostid,
		(int) ((wakeup_pkt.hostid >> 24) & 0xff),
		(int) ((wakeup_pkt.hostid >> 16) & 0xff),
		(int) ((wakeup_pkt.hostid >>  8) & 0xff),
		(int)  (wakeup_pkt.hostid        & 0xff));
	fprintf(stderr, "\tnetmask == 0x%08lx (%d.%d.%d.%d)\n",
		wakeup_pkt.netmask,
		(int) ((wakeup_pkt.netmask >> 24) & 0xff),
		(int) ((wakeup_pkt.netmask >> 16) & 0xff),
		(int) ((wakeup_pkt.netmask >>  8) & 0xff),
		(int)  (wakeup_pkt.netmask        & 0xff));
	fprintf(stderr, "\tHostname: \"%s\"\n", wakeup_pkt.hostname);

	if (wakeup_pkt.magic != NETSYNC_WAKEUP_MAGIC)
	{
		fprintf(stderr, "This is not a wakeup packet.\n");
		goto retry;
	}

	close(pconn->fd);	/* We're done with the UDP socket */

	return 0;
}




/* net_connect
 * Establish a connection to the host whose address is 'addr':
 *	- Send a UDP wakeup packet to the given address.
 *	- Listen for a wakeup ACK packet.
 *	- Establish a TCP socket to the host that responded.
 *	- Exchange ritual packets.
 */
static int
net_connect(PConnection *pconn, const void *addr, const int addrlen)
{
	int err;
	int i;
	struct sockaddr_in servaddr;
	struct servent *service;

	/* XXX - Break this monster function up into parts */

	/* Copy the given address */
	/* XXX - This assumes IPv4 */
	memcpy(&servaddr, addr, sizeof(struct sockaddr_in));

	IO_TRACE(3)
	{
#if HAVE_INET_NTOP
		char namebuf[128];
#endif	/* HAVE_INET_NTOP */

		fprintf(stderr, "Inside net_connect(%s), port %d\n",
			INET_NTOP(servaddr.sin_family,
				  servaddr.sin_addr,
				  namebuf, 128),
			ntohs(servaddr.sin_port));
	}

	/* XXX - Open TCP connection to host that responded */
	/* XXX - I suspect this is "wrong": presumably, the hostid in the
	 * wakeup ACK is the hostid (IPv4 address) of the host with which
	 * to sync (which may be different from the host that responded to
	 * the wakeup packet).
	 * Thus, one might have the following situation: two networks,
	 * 10.0.0.0 and 192.168.0.0. The dual-homed host 'twohome'
	 * (10.0.0.1 and 192.168.1.1) routes between the two. The host
	 * 192.168.1.100 sends out a UDP wakeup packet on the broadcast
	 * address 192.168.1.255. twohome sends it an ACK with the address
	 * (hostid) 10.0.0.100 . Then 192.168.1.100 establishes a TCP
	 * connection with 10.0.0.100.
	 * This doesn't work with IPv6, since the hostid field is too small
	 * to hold an IPv6 address (plus, since there's no broadcasting, I
	 * suspect that the netmask field is irrelevant). One solution
	 * would be to take the hostid field as just that: a host ID. Look
	 * that up in a table (/usr/local/etc/coldsync.hosts), and get the
	 * address there.
	 */
	IO_TRACE(5)
		fprintf(stderr, "Opening TCP socket to server.\n");
	
	/* We initiated the connection */
	pconn->whosonfirst = 1;
	
	
	pconn->fd = socket(AF_INET, SOCK_STREAM, 0);
	if (pconn->fd < 0)
	{
		perror("socket");
		return -1;
	}
	IO_TRACE(5)
		fprintf(stderr, "TCP socket == %d\n", pconn->fd);

	service = getservbyname("netsync", "tcp");
				/* Try to get the entry for "netsync" from
				 * /etc/services
				 */
	IO_TRACE(2)
	{
		if (service != NULL)
		{
			int i;

			fprintf(stderr, "Got entry for netsync/tcp:\n");
			fprintf(stderr, "\tname: \"%s\"\n", service->s_name);
			fprintf(stderr, "\taliases:\n");
			for (i = 0; service->s_aliases[i] != NULL; i++)
				fprintf(stderr, "\t\t\"%s\"\n",
					service->s_aliases[i]);
			fprintf(stderr, "\tport: %d\n",
				ntohs(service->s_port));
			fprintf(stderr, "\tprotocol: \"%s\"\n",
				service->s_proto);
		} else {
			fprintf(stderr, "No entry for netsync/tcp\n");
		}
	}

	servaddr.sin_family = AF_INET;
	if (service == NULL)
		servaddr.sin_port = htons(NETSYNC_DATA_PORT);
	else
		servaddr.sin_port = service->s_port;

	/* XXX - connect() should time out after a while. 10 seconds? */
	for (i = 0; i < PCONN_NET_CONNECT_RETRIES; i++)
	{
		IO_TRACE(3)
		{
#if HAVE_INET_NTOP
			char namebuf[128];
#endif	/* HAVE_INET_NTOP */

			fprintf(stderr, "connecting to [%s], port %d\n",
				INET_NTOP(servaddr.sin_family,
					  servaddr.sin_addr,
					  namebuf, 128),
				ntohs(servaddr.sin_port));
		}

		err = connect(pconn->fd,
			      (struct sockaddr *) &servaddr,
			      sizeof(struct sockaddr));
			/* Normally, the second argument ought to be cast
			 * to (const struct sockaddr *), but Solaris
			 * expects a (struct sockaddr *), so it complains.
			 * You can #define _XPG4_2 to get a 'const'
			 * prototype, but that introduces all sorts of
			 * other problems.
			 */
		if (err < 0)
		{
			perror("connect");
			sleep(PCONN_NET_CONNECT_DELAY);	/* Give the server time to appear */
			continue;
		}

		IO_TRACE(3)
			fprintf(stderr, "connected\n");
		break;
	}

	if (err < 0)
	{
		fprintf(stderr, _("Can't connect to server.\n"));
		return -1;
	}


	/* Exchange ritual packets with server */
	err = ritual_exch_client(pconn);
	if (err < 0)
		return -1;

	return 0;
}

static int
net_accept(PConnection *p)
{
	/* XXX - Redo this to allow IPv6 */
	struct netsync_wakeup wakeup_pkt;
	struct sockaddr_in cliaddr;	/* Client's address */
	socklen_t cliaddr_len;		/* Length of client's address */

	if (p->protocol != PCONN_STACK_NET)
		/* No other protocol stack is supported (for now, at least) */
		return -1;

	cliaddr_len = sizeof(cliaddr);
	net_udp_listen(p, &wakeup_pkt,
		       &cliaddr, &cliaddr_len);
	/* XXX - Error-checking */

	net_acknowledge_wakeup(p, &wakeup_pkt,
			       &cliaddr, &cliaddr_len);
	/* XXX - Error-checking */

	net_tcp_listen(p);
	/* XXX - Error-checking */

	return 0;
}

static int
net_close(PConnection *p)
{
	/* Clean up the protocol stack elements */

	switch (p->protocol)
	{
	    case PCONN_STACK_DEFAULT:	/* Fall through */
	    case PCONN_STACK_NET:
		dlp_tini(p);
		netsync_tini(p);
		break;

	    case PCONN_STACK_FULL:	/* Fall through */
	    case PCONN_STACK_SIMPLE:	/* Fall through */
	    default:
		/* Fail silently */
		break;
	}

	return (p->fd >= 0 ? close(p->fd) : 0);
}

static int
net_select(PConnection *p,
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
net_drain(PConnection *p)
{
	/* I don't think there's a network equivalent of flushing a stream
	 * or tty connection.
	 */
	/* We ignore the p->protocol field because they don't matter */
	return 0;
}

int
pconn_net_open(PConnection *pconn,
	       const char *device,
	       const pconn_proto_t protocol,
	       const unsigned short flags)
{
	IO_TRACE(1)
		fprintf(stderr, "Opening net connection.\n");

	if (protocol == PCONN_STACK_DEFAULT)
		pconn->protocol = PCONN_STACK_NET;
	else
		pconn->protocol = protocol;

	/* Initialize the various protocols that the network connection
	 * will use.
	 */
	switch (pconn->protocol)
	{
	    case PCONN_STACK_FULL:
	    case PCONN_STACK_SIMPLE:
		/* XXX - Should these even be supported? Not for now */
		return -1;

	    case PCONN_STACK_NET:
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
		break;

	    case PCONN_STACK_NONE:
	    case PCONN_STACK_DEFAULT:
		/* XXX - Error */
	    default:
		/* XXX - Indicate error: unsupported protocol stack */
		return -1;
	}

	/* Set the methods used by the network connection */
	pconn->io_bind		= &net_bind;
	pconn->io_read		= &net_read;
	pconn->io_write		= &net_write;
	pconn->io_connect	= &net_connect;
	pconn->io_accept	= &net_accept;
	pconn->io_close		= &net_close;
	pconn->io_select	= &net_select;
	pconn->io_drain		= &net_drain;
	pconn->io_private	= 0;

	pconn->whosonfirst	= 0;
	pconn->net.xid		= 0xFE;

	/* Create a socket
	 * Although we'll use pconn->fd for both the UDP and TCP sockets,
	 * for now, we'll just create the UDP socket.
	 */
	/* XXX - Make this work for IPv6 as well */
	pconn->fd = socket(AF_INET, SOCK_DGRAM, 0);
	if (pconn->fd < 0)
		return pconn->fd;	/* Error */

	IO_TRACE(5)
		fprintf(stderr, "UDP socket == %d\n", pconn->fd);

	return pconn->fd;
}

static int
net_udp_listen(PConnection *pconn,
	       struct netsync_wakeup *wakeup_pkt,
	       struct sockaddr_in *cliaddr,
	       socklen_t *cliaddr_len)
{
	int len;
	ubyte buf[1024];		/* XXX - Fixed size bad */
	const ubyte *rptr;		/* Pointer into buffer, for reading */

	/* Receive a datagram from a client */
  retry:
	len = recvfrom(pconn->fd, (char *) buf, sizeof(buf), 0,
		       (struct sockaddr *) cliaddr,
		       cliaddr_len);

	fprintf(stderr, "recvfrom() returned %d\n", len);
	if (len < 0)
	{
		perror("recvfrom");
		goto retry;
	} else {
		fprintf(stderr,
			"Got datagram from host 0x%08lx (%d.%d.%d.%d), "
			"port %d, length %ld\n",
			(unsigned long) cliaddr->sin_addr.s_addr,
			(int)  (cliaddr->sin_addr.s_addr	& 0xff),
			(int) ((cliaddr->sin_addr.s_addr >>  8) & 0xff),
			(int) ((cliaddr->sin_addr.s_addr >> 16) & 0xff),
			(int) ((cliaddr->sin_addr.s_addr >> 24) & 0xff),
			cliaddr->sin_port,
			(long) *cliaddr_len);
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

	/* XXX - Wrap in IO_TRACE */
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
net_acknowledge_wakeup(PConnection *pconn,
		       struct netsync_wakeup *wakeup_pkt,
		       struct sockaddr_in *cliaddr,
		       socklen_t *cliaddr_len)
{
	int err;
	ubyte outbuf[1024];		/* XXX - Fixed size bad, m'kay? */
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

	IO_TRACE(3)
		fprintf(stderr, "Sending acknowledgment.\n");
	err = sendto(pconn->fd, (const char *) outbuf, pkt_len, 0,
		     (struct sockaddr *) cliaddr,
		     *cliaddr_len);
	if (err < 0)
	{
		perror("sendto");
		return -1;
	}

	fprintf(stderr, "Closing UDP socket.\n");
	if (pconn->fd >= 0)
	{
		err = close(pconn->fd);
		fprintf(stderr, "close() returned %d\n", err);
		if (err < 0)
			perror("close");
	}

	return 0;
}

static int
net_tcp_listen(PConnection *pconn)
{
	int err;
	struct sockaddr_in servaddr;	/* Local host's (server's) address */
	struct sockaddr_in cliaddr;	/* Client's address */
	socklen_t cliaddr_len;		/* Length of client's address */
	struct servent *service;	/* "netsync" entry in /etc/services */
	int data_sock;			/* Data socket (TCP). Will replace
					 * the UDP socket pconn->fd.
					 */

	IO_TRACE(4)
		fprintf(stderr, "Inside net_tcp_listen()\n");

	IO_TRACE(5)
		fprintf(stderr, "Creating TCP socket.\n");
	pconn->fd = socket(AF_INET, SOCK_STREAM, 0);
	if (pconn->fd < 0)
	{
		perror("socket");
		return -1;
	}
	IO_TRACE(5)
		fprintf(stderr, "TCP socket == %d\n", pconn->fd);

	service = getservbyname("netsync", "tcp");
				/* Try to get the entry for "netsync" from
				 * /etc/services
				 */
	IO_TRACE(2)
	{
		if (service != NULL)
		{
			int i;

			fprintf(stderr, "Got entry for netsync/tcp:\n");
			fprintf(stderr, "\tname: \"%s\"\n", service->s_name);
			fprintf(stderr, "\taliases:\n");
			for (i = 0; service->s_aliases[i] != NULL; i++)
				fprintf(stderr, "\t\t\"%s\"\n",
					service->s_aliases[i]);
			fprintf(stderr, "\tport: %d\n",
				ntohs(service->s_port));
			fprintf(stderr, "\tprotocol: \"%s\"\n",
				service->s_proto);
		} else {
			fprintf(stderr, "No entry for netsync/tcp\n");
		}
	}

	bzero((void *) &servaddr, sizeof(servaddr));
	servaddr.sin_family = AF_INET;
	servaddr.sin_addr.s_addr = htonl(INADDR_ANY);

	if (service == NULL)
		servaddr.sin_port = htons(NETSYNC_DATA_PORT);
	else
		servaddr.sin_port = service->s_port;
				/* Port is already in network byte order */

	IO_TRACE(5)
		fprintf(stderr, "binding\n");
	err = bind(pconn->fd, (struct sockaddr *) &servaddr, sizeof(servaddr));
	if (err < 0)
	{
		perror("bind");
		return -1;
	}

	IO_TRACE(5)
		fprintf(stderr, "listening\n");
	err = listen(pconn->fd, 1);
				/* NB: the backlog is set to 1 because we
				 * know for sure that there's one incoming
				 * connection, and if there's a second one,
				 * this process isn't going to handle it.
				 * In other circumstances, a different
				 * value would be required.
				 */
	if (err < 0)
	{
		perror("listen");
		return -1;
	}

	/* XXX - accept() should time out after a while. 10 seconds? */
	IO_TRACE(5)
		fprintf(stderr, "accepting\n");
	cliaddr_len = sizeof(cliaddr);
	data_sock = accept(pconn->fd, (struct sockaddr *) &cliaddr,
			   &cliaddr_len);
	if (data_sock < 0)
	{
		perror("accept");
		return -1;
	}
	IO_TRACE(1)
		fprintf(stderr,
			"Accepted TCP connection from 0x%08lx (%d.%d.%d.%d), "
			"port %d\n",
			(unsigned long) cliaddr.sin_addr.s_addr,
			(int)  (cliaddr.sin_addr.s_addr        & 0xff),
			(int) ((cliaddr.sin_addr.s_addr >>  8) & 0xff),
			(int) ((cliaddr.sin_addr.s_addr >> 16) & 0xff),
			(int) ((cliaddr.sin_addr.s_addr >> 24) & 0xff),
			cliaddr.sin_port);

	/* We've accepted a TCP connection, so we don't need the UDP socket
	 * anymore. Replace the UDP socket with the TCP one.
	 */
	close(pconn->fd);		/* XXX - Wasn't this done already
					 * in net_acknowledge_wakeup()?
					 */
	pconn->fd = data_sock;

	/* Exchange ritual packets with the client */
	err = ritual_exch_server(pconn);
	if (err < 0)
		return -1;

	return 0;
}

/* This is for Emacs's benefit:
 * Local Variables: ***
 * fill-column:	75 ***
 * End: ***
 */
