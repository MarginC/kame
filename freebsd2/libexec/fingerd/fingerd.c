/*
 * Copyright (c) 1983, 1993
 *	The Regents of the University of California.  All rights reserved.
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
 */

#ifndef lint
static const char copyright[] =
"@(#) Copyright (c) 1983, 1993\n\
	The Regents of the University of California.  All rights reserved.\n";
#endif /* not lint */

#ifndef lint
#if 0
static char sccsid[] = "@(#)fingerd.c	8.1 (Berkeley) 6/4/93";
#endif
static const char rcsid[] =
	"$Id: fingerd.c,v 1.5.2.4 1998/02/18 12:30:56 jkh Exp $";
#endif /* not lint */

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/param.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <errno.h>

#include <unistd.h>
#include <syslog.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include "pathnames.h"

void logerr __P((const char *, ...));

int
main(argc, argv)
	int argc;
	char *argv[];
{
	register FILE *fp;
	register int ch;
	register char *lp;
	char hbuf[MAXHOSTNAMELEN];
	struct sockaddr_storage ss;
	int p[2], logging, secure, sval;
#define	ENTRIES	50
	char **ap, *av[ENTRIES + 1], **comp, line[1024], *prog;

	prog = _PATH_FINGER;
	logging = secure = 0;
	openlog("fingerd", LOG_PID | LOG_CONS, LOG_DAEMON);
	opterr = 0;
	while ((ch = getopt(argc, argv, "slp:")) != -1)
		switch (ch) {
		case 'l':
			logging = 1;
			break;
		case 'p':
			prog = optarg;
			break;
		case 's':
			secure = 1;
			break;
		case '?':
		default:
			logerr("illegal option -- %c", ch);
		}

	/*
	 * Enable server-side Transaction TCP.
	 */
	{
		int one = 1;
		int proto = 0;

		sval = sizeof(ss);
		if (getsockname(0, (struct sockaddr *)&ss, &sval) < 0)
			logerr("getpeername: %s", strerror(errno));
		if (((struct sockaddr *)&ss)->sa_family == AF_INET	/*XXX*/
		 && setsockopt(STDOUT_FILENO, IPPROTO_TCP, TCP_NOPUSH, &one, 
			       sizeof one) < 0) {
			logerr("setsockopt(TCP_NOPUSH) failed: %m");
		}
	}

	if (!fgets(line, sizeof(line), stdin))
		exit(1);

	if (logging) {
		char *t;
		char *end;

		end = memchr(line, 0, sizeof(line));
		if (end == NULL) {
			t = malloc(sizeof(line) + 1);
			memcpy(t, line, sizeof(line));
			t[sizeof(line)] = 0;
		} else {
			t = strdup(line);
		}
		for (end = t; *end; end++)
			if (*end == '\n' || *end == '\r')
				*end = ' ';
		sval = sizeof(ss);
		if (getpeername(0, (struct sockaddr *)&ss, &sval) < 0)
			logerr("getpeername: %s", strerror(errno));
		if (getnameinfo((struct sockaddr *)&ss, sval,
				hbuf, sizeof(hbuf), NULL, 0, 0) == 0) {
			(void)getnameinfo((struct sockaddr *)&ss, sval,
				hbuf, sizeof(hbuf), NULL, 0, NI_NUMERICHOST);
		}
		syslog(LOG_NOTICE, "query from %s: `%s'", hbuf, t);
	}

	comp = &av[1];
	av[2] = "--";
	for (lp = line, ap = &av[3];;) {
		*ap = strtok(lp, " \t\r\n");
		if (!*ap) {
			if (secure && ap == &av[3]) {
				puts("must provide username\r\n");
				exit(1);
			}
			break;
		}
		if (secure && strchr(*ap, '@')) {
			puts("forwarding service denied\r\n");
			exit(1);
		}

		/* RFC742: "/[Ww]" == "-l" */
		if ((*ap)[0] == '/' && ((*ap)[1] == 'W' || (*ap)[1] == 'w')) {
			av[1] = "-l";
			comp = &av[0];
		}
		else if (++ap == av + ENTRIES)
			break;
		lp = NULL;
	}

	if (lp = strrchr(prog, '/'))
		*comp = ++lp;
	else
		*comp = prog;
	if (pipe(p) < 0)
		logerr("pipe: %s", strerror(errno));

	switch(vfork()) {
	case 0:
		(void)close(p[0]);
		if (p[1] != 1) {
			(void)dup2(p[1], 1);
			(void)close(p[1]);
		}
		execv(prog, comp);
		logerr("execv: %s: %s", prog, strerror(errno));
		_exit(1);
	case -1:
		logerr("fork: %s", strerror(errno));
	}
	(void)close(p[1]);
	if (!(fp = fdopen(p[0], "r")))
		logerr("fdopen: %s", strerror(errno));
	while ((ch = getc(fp)) != EOF) {
		if (ch == '\n')
			putchar('\r');
		putchar(ch);
	}
	exit(0);
}

#if __STDC__
#include <stdarg.h>
#else
#include <varargs.h>
#endif

void
#if __STDC__
logerr(const char *fmt, ...)
#else
logerr(fmt, va_alist)
	char *fmt;
        va_dcl
#endif
{
	va_list ap;
#if __STDC__
	va_start(ap, fmt);
#else
	va_start(ap);
#endif
	(void)vsyslog(LOG_ERR, fmt, ap);
	va_end(ap);
	exit(1);
	/* NOTREACHED */
}
