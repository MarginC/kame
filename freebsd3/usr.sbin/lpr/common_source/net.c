/*
 * Copyright (c) 1983, 1993
 *	The Regents of the University of California.  All rights reserved.
 * (c) UNIX System Laboratories, Inc.
 * All or some portions of this file are derived from material licensed
 * to the University of California by American Telephone and Telegraph
 * Co. or Unix System Laboratories, Inc. and are reproduced herein with
 * the permission of UNIX System Laboratories, Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	From: @(#)common.c	8.5 (Berkeley) 4/28/95
 */

#ifndef lint
static const char rcsid[] =
	"$Id: net.c,v 1.1.2.1 1999/04/27 07:10:37 jkh Exp $";
#endif /* not lint */

#include <sys/param.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/uio.h>

#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#include <dirent.h>		/* required for lp.h, not used here */
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "lp.h"
#include "lp.local.h"
#include "pathnames.h"

			/* host machine name */
char	host[MAXHOSTNAMELEN];
char	*from = host;	/* client's machine name */

extern uid_t	uid, euid;

/*
 * Create a TCP connection to host "rhost" at port "rport".
 * If rport == 0, then use the printer service port.
 * Most of this code comes from rcmd.c.
 */
int
getport(const struct printer *pp, const char *rhost, int rport)
{
	struct addrinfo hints, *res, *ai;
	int s, timo = 1, lport = IPPORT_RESERVED - 1;
	int err;

	/*
	 * Get the host address and port number to connect to.
	 */
	if (rhost == NULL)
		fatal(pp, "no remote host to connect to");
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = 0;
	err = getaddrinfo(rhost, (rport == 0 ? "printer" : NULL), &hints, &res);
	if (err)
		fatal(pp, "%s\n", gai_strerror(err));
	if (rport != 0)
		((struct sockaddr_in *) res->ai_addr)->sin_port = htons(rport);

	/*
	 * Try connecting to the server.
	 */
	ai = res;
retry:
	seteuid(euid);
	s = rresvport_af(&lport, ai->ai_family);
	seteuid(uid);
	if (s < 0)
		return(-1);
	if (connect(s, ai->ai_addr, ai->ai_addrlen) < 0) {
		err = errno;
		(void) close(s);
		errno = err;
		/*
		 * This used to decrement lport, but the current semantics
		 * of rresvport do not provide such a function (in fact,
		 * rresvport should guarantee that the chosen port will
		 * never result in an EADDRINUSE).
		 */
		if (errno == EADDRINUSE)
			goto retry;

		if (errno == ECONNREFUSED && timo <= 16) {
			sleep(timo);
			timo *= 2;
			goto retry;
		}
		if (ai->ai_next != NULL) {
			ai = ai->ai_next;
			goto retry;
		}
		freeaddrinfo(res);
		return(-1);
	}
	freeaddrinfo(res);
	return(s);
}

/*
 * Figure out whether the local machine is the same
 * as the remote machine (RM) entry (if it exists).
 * We do this by counting the intersection of our
 * address list and theirs.  This is better than the
 * old method (comparing the canonical names), as it
 * allows load-sharing between multiple print servers.
 * The return value is an error message which must be
 * free()d.
 */
char *
checkremote(struct printer *pp)
{
	char name[MAXHOSTNAMELEN];
	register struct hostent *hp;
	char *err;
	struct in_addr *localaddrs;
	int i, j, nlocaladdrs, ncommonaddrs;

	if (!pp->rp_matches_local) { /* Remote printer doesn't match local */
		pp->remote = 1;
		return NULL;
	}

	pp->remote = 0;	/* assume printer is local */
	if (pp->remote_host != NULL) {
		/* get the addresses of the local host */
		gethostname(name, sizeof(name));
		name[sizeof(name) - 1] = '\0';
		hp = gethostbyname2(name, AF_INET6);
		if (hp == NULL)
		    hp = gethostbyname2(name, AF_INET);
		if (hp == (struct hostent *) NULL) {
			asprintf(&err, "unable to get official name "
				 "for local machine %s: %s",
				 name, hstrerror(h_errno));
			return err;
		}
		for (i = 0; hp->h_addr_list[i]; i++)
			;
		nlocaladdrs = i;
		localaddrs = malloc(i * sizeof(struct in_addr));
		if (localaddrs == 0) {
			asprintf(&err, "malloc %lu bytes failed",
				 (u_long)i * sizeof(struct in_addr));
			return err;
		}
		for (i = 0; hp->h_addr_list[i]; i++)
			localaddrs[i] = *(struct in_addr *)hp->h_addr_list[i];

		/* get the official name of RM */
		hp = gethostbyname2(pp->remote_host, AF_INET6);
		if (hp == NULL)
		    hp = gethostbyname2(pp->remote_host, AF_INET);
		hp = gethostbyname2(pp->remote_host, AF_INET);
		if (hp == (struct hostent *) NULL) {
			asprintf(&err, "unable to get address list for "
				 "remote machine %s: %s",
				 pp->remote_host, hstrerror(h_errno));
			free(localaddrs);
			return err;
		}

		ncommonaddrs = 0;
		for (i = 0; i < nlocaladdrs; i++) {
			for (j = 0; hp->h_addr_list[j]; j++) {
				char *them = hp->h_addr_list[j];
				if (localaddrs[i].s_addr ==
				    (*(struct in_addr *)them).s_addr)
					ncommonaddrs++;
			}
		}
			
		/*
		 * if the two hosts do not share at least one IP address
		 * then the printer must be remote.
		 */
		if (ncommonaddrs == 0)
			pp->remote = 1;
		free(localaddrs);
	}
	return NULL;
}

/*
 * This isn't really network-related, but it's used here to write
 * multi-part strings onto sockets without using stdio.  Return
 * values are as for writev(2).
 */
ssize_t
writel(int s, ...)
{
	va_list ap;
	int i, n;
	const char *cp;
#define NIOV 12
	struct iovec iov[NIOV], *iovp = iov;
	ssize_t retval;

	/* first count them */
	va_start(ap, s);
	n = 0;
	do {
		cp = va_arg(ap, char *);
		n++;
	} while (cp);
	va_end(ap);
	n--;			/* correct for count of trailing null */

	if (n > NIOV) {
		iovp = malloc(n * sizeof *iovp);
		if (iovp == 0)
			return -1;
	}

	/* now make up iovec and send */
	va_start(ap, s);
	for (i = 0; i < n; i++) {
		iovp[i].iov_base = va_arg(ap, char *);
		iovp[i].iov_len = strlen(iovp[i].iov_base);
	}
	va_end(ap);
	retval = writev(s, iovp, n);
	if (iovp != iov)
		free(iovp);
	return retval;
}
