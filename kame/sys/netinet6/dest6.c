/*	$KAME: dest6.c,v 1.21 2001/01/23 09:20:40 itojun Exp $	*/

/*
 * Copyright (C) 1995, 1996, 1997, and 1998 WIDE Project.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the project nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE PROJECT AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE PROJECT OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#if defined(__FreeBSD__) && __FreeBSD__ >= 3
#include "opt_inet.h"
#include "opt_inet6.h"
#endif
#ifdef __NetBSD__
#include "opt_inet.h"
#endif

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/domain.h>
#include <sys/protosw.h>
#include <sys/socket.h>
#include <sys/errno.h>
#include <sys/time.h>
#include <sys/kernel.h>

#include <net/if.h>
#include <net/route.h>

#include <netinet/in.h>
#include <netinet/in_var.h>
#include <netinet/ip6.h>
#include <netinet6/ip6_var.h>
#if !(defined(__FreeBSD__) && __FreeBSD__ >= 3) && !defined(__OpenBSD__) && !(defined(__bsdi__) && _BSDI_VERSION >= 199802)
#include <netinet6/in6_pcb.h>
#endif
#include <netinet/icmp6.h>

#ifdef MIP6
int (*mip6_store_dstopt_pre_hook)(struct mbuf *m, u_int8_t *opt,
				  u_int8_t off, u_int8_t dstlen) = NULL;
int (*mip6_rec_ctrl_sig_hook)(struct mbuf *m, int off) = NULL;
#endif /* MIP6 */

/*
 * Destination options header processing.
 */
int
dest6_input(mp, offp, proto)
	struct mbuf **mp;
	int *offp, proto;
{
	struct mbuf *m = *mp;
	int off = *offp, dstoptlen, optlen;
	struct ip6_dest *dstopts;
	struct mbuf *n;
	struct ip6_opt_home_address *haopt = NULL;
	struct ip6aux *ip6a = NULL;
	u_int8_t *opt;
	struct ip6_hdr *ip6;

	ip6 = mtod(m, struct ip6_hdr *);

	/* validation of the length of the header */
#ifndef PULLDOWN_TEST
	IP6_EXTHDR_CHECK(m, off, sizeof(*dstopts), IPPROTO_DONE);
	dstopts = (struct ip6_dest *)(mtod(m, caddr_t) + off);
#else
	IP6_EXTHDR_GET(dstopts, struct ip6_dest *, m, off, sizeof(*dstopts));
	if (dstopts == NULL)
		return IPPROTO_DONE;
#endif
	dstoptlen = (dstopts->ip6d_len + 1) << 3;

#ifndef PULLDOWN_TEST
	IP6_EXTHDR_CHECK(m, off, dstoptlen, IPPROTO_DONE);
	dstopts = (struct ip6_dest *)(mtod(m, caddr_t) + off);
#else
	IP6_EXTHDR_GET(dstopts, struct ip6_dest *, m, off, dstoptlen);
	if (dstopts == NULL)
		return IPPROTO_DONE;
#endif
	off += dstoptlen;
	dstoptlen -= sizeof(struct ip6_dest);
	opt = (u_int8_t *)dstopts + sizeof(struct ip6_dest);

	/* search header for all options. */
	for (optlen = 0; dstoptlen > 0; dstoptlen -= optlen, opt += optlen) {
		switch (*opt) {
		case IP6OPT_PAD1:
			optlen = 1;
			break;
		case IP6OPT_PADN:
			if (dstoptlen < IP6OPT_MINLEN) {
				ip6stat.ip6s_toosmall++;
				goto bad;
			}
			optlen = *(opt + 1) + 2;
			break;
		case IP6OPT_HOME_ADDRESS:
			if (dstoptlen < sizeof(*haopt)) {
				ip6stat.ip6s_toosmall++;
				goto bad;
			}

#if 0
			/* be picky about alignment: 8n+6 */
			if ((opt - (u_int8_t *)dstopts) % 8 != 6)
				goto bad;
#endif

			/* HA option must appear only once */
			n = ip6_addaux(m);
			if (!n) {
				/* not enough core */
				goto bad;
			}
			ip6a = mtod(n, struct ip6aux *);
			if ((ip6a->ip6a_flags & IP6A_HASEEN) != 0) {
				/* XXX icmp6 paramprob? */
				goto bad;
			}

			haopt = (struct ip6_opt_home_address *)opt;
			optlen = haopt->ip6oh_len + 2;

			/*
			 * don't complain even if it is larger,
			 * we don't support suboptions at this moment.
			 */
			if (optlen < sizeof(*haopt)) {
				ip6stat.ip6s_toosmall++;
				goto bad;
			}

			/* XXX check header ordering */

			bcopy(haopt->ip6oh_addr, &ip6a->ip6a_home, 
			    sizeof(ip6a->ip6a_home));
			bcopy(&ip6->ip6_src, &ip6a->ip6a_careof, 
			    sizeof(ip6a->ip6a_careof));
			ip6a->ip6a_flags |= IP6A_HASEEN;

#ifdef MIP6
			if (mip6_store_dstopt_pre_hook) {
				if ((*mip6_store_dstopt_pre_hook)(m, opt,
				    off, dstoptlen) != 0)
					goto bad;
			}
#endif
			break;
#ifdef MIP6
		case IP6OPT_BINDING_UPDATE:
		case IP6OPT_BINDING_ACK:
		case IP6OPT_BINDING_REQ:
			if (mip6_store_dstopt_pre_hook) {
				if ((*mip6_store_dstopt_pre_hook)(m, opt,
				    off, dstoptlen) != 0)
					goto bad;
			}
			optlen = *(opt + 1) + 2;
			break;
#endif /* MIP6 */

		default:		/* unknown option */
			if (dstoptlen < IP6OPT_MINLEN) {
				ip6stat.ip6s_toosmall++;
				goto bad;
			}
			optlen = ip6_unknown_opt(opt, m,
			    opt - mtod(m, u_int8_t *));
			if (optlen == -1)
				return (IPPROTO_DONE);
			optlen += 2;
			break;
		}
	}

	/* if haopt is non-NULL, we are sure we have seen fresh HA option */
	if (haopt && ip6a &&
	    (ip6a->ip6a_flags & (IP6A_HASEEN | IP6A_SWAP)) == IP6A_HASEEN) {
		/* XXX should we do this now or later? */
		bcopy(&ip6a->ip6a_careof, haopt->ip6oh_addr,
		    sizeof(haopt->ip6oh_addr));
		bcopy(&ip6a->ip6a_home, &ip6->ip6_src,
		    sizeof(ip6->ip6_src));
		ip6a->ip6a_flags |= IP6A_SWAP;
	}

#ifdef MIP6
	if (mip6_rec_ctrl_sig_hook) {
		/*
		 * All Destinations options have been processed. Call MIPv6 to
		 * process stored options.
		 */
		if ((*mip6_rec_ctrl_sig_hook)(m, *offp) != 0)
			return (IPPROTO_DONE);
	}
#endif /* MIP6 */

	*offp = off;
	return (dstopts->ip6d_nxt);

  bad:
	m_freem(m);
	return (IPPROTO_DONE);
}
