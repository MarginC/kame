/*	$KAME: ip6_output.c,v 1.168 2001/03/12 02:54:43 itojun Exp $	*/

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

/*
 * Copyright (c) 1982, 1986, 1988, 1990, 1993
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
 *
 *	@(#)ip_output.c	8.3 (Berkeley) 1/21/94
 */

#ifdef __FreeBSD__
#include "opt_ip6fw.h"
#if __FreeBSD__ >= 3
#include "opt_inet.h"
#include "opt_inet6.h"
#include "opt_ipsec.h"
#endif
#endif
#ifdef __NetBSD__
#include "opt_inet.h"
#include "opt_ipsec.h"
#endif

#include <sys/param.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/errno.h>
#include <sys/protosw.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/systm.h>
#if (defined(__FreeBSD__) && __FreeBSD__ >= 3)
#include <sys/kernel.h>
#endif
#if defined(__bsdi__) && _BSDI_VERSION >= 199802
#include <machine/pcpu.h>
#endif
#include <sys/proc.h>

#include <net/if.h>
#include <net/route.h>
#if defined(__NetBSD__) && defined(PFIL_HOOKS)
#include <net/pfil.h>
#endif

#include <netinet/in.h>
#include <netinet/in_var.h>
#if defined(__OpenBSD__) || (defined(__bsdi__) && _BSDI_VERSION >= 199802)
#include <netinet/in_systm.h>
#include <netinet/ip.h>
#endif
#include <netinet6/in6_var.h>
#include <netinet/ip6.h>
#include <netinet/icmp6.h>
#include <netinet6/ip6_var.h>
#if (defined(__FreeBSD__) && __FreeBSD__ >= 3) || defined(__OpenBSD__) || (defined(__bsdi__) && _BSDI_VERSION >= 199802)
#include <netinet/in_pcb.h>
#else
#include <netinet6/in6_pcb.h>
#endif
#include <netinet6/nd6.h>
#include <netinet6/ip6protosw.h>

#ifdef IPSEC
#ifdef __OpenBSD__
#include <netinet/ip_ah.h>
#include <netinet/ip_esp.h>
#include <netinet/udp.h>
#include <netinet/tcp.h>
#include <net/pfkeyv2.h>

extern u_int8_t get_sa_require  __P((struct inpcb *));

extern int ipsec_auth_default_level;
extern int ipsec_esp_trans_default_level;
extern int ipsec_esp_network_default_level;
#else
#include <netinet6/ipsec.h>
#include <netkey/key.h>
#endif
#endif /* IPSEC */

#ifndef __bsdi__
#include "loop.h"
#endif

#if defined(IPV6FIREWALL) || (defined(__FreeBSD__) && __FreeBSD__ >= 4)
#include <netinet6/ip6_fw.h>
#endif

#include <net/net_osdep.h>

#if defined(__FreeBSD__) && __FreeBSD__ >= 3
static MALLOC_DEFINE(M_IPMOPTS, "ip6_moptions", "internet multicast options");
#endif

struct ip6_exthdrs {
	struct mbuf *ip6e_ip6;
	struct mbuf *ip6e_hbh;
	struct mbuf *ip6e_dest1;
	struct mbuf *ip6e_rthdr;
	struct mbuf *ip6e_dest2;
};

static int ip6_pcbopt __P((int, u_char *, int, struct ip6_pktopts **, int));
static int ip6_getpcbopt __P((struct ip6_pktopts *, int, void **, int *));
#if defined(__FreeBSD__) && __FreeBSD__ >= 3
static int ip6_pcbopts __P((struct ip6_pktopts **, struct mbuf *,
			    struct socket *, struct sockopt *sopt));
#else
static int ip6_pcbopts __P((struct ip6_pktopts **, struct mbuf *,
			    struct socket *));
#endif
static int ip6_setmoptions __P((int, struct ip6_moptions **, struct mbuf *));
static int ip6_getmoptions __P((int, struct ip6_moptions *, struct mbuf **));
static int ip6_copyexthdr __P((struct mbuf **, caddr_t, int));
static int ip6_insertfraghdr __P((struct mbuf *, struct mbuf *, int,
				  struct ip6_frag **));
static int ip6_insert_jumboopt __P((struct ip6_exthdrs *, u_int32_t));
static int ip6_splithdr __P((struct mbuf *, struct ip6_exthdrs *));
#ifdef __bsdi__
#if _BSDI_VERSION < 199802
extern struct ifnet loif;
#else
extern struct ifnet *loifp;
#endif
#endif
#if defined(__NetBSD__) || defined(__OpenBSD__)
extern struct ifnet loif[NLOOP];
#endif

#ifdef MIP6
int (*mip6_output_hook)(struct mbuf *m, struct ip6_pktopts **opt);
#endif /* MIP6 */

/*
 * IP6 output. The packet in mbuf chain m contains a skeletal IP6
 * header (with pri, len, nxt, hlim, src, dst).
 * This function may modify ver and hlim only.
 * The mbuf chain containing the packet will be freed.
 * The mbuf opt, if present, will not be freed.
 *
 * type of "mtu": rt_rmx.rmx_mtu is u_long, ifnet.ifr_mtu is int, and
 * nd_ifinfo.linkmtu is u_int32_t.  so we use u_long to hold largest one,
 * which is rt_rmx.rmx_mtu.
 */
int
ip6_output(m0, opt, ro, flags, im6o, ifpp)
	struct mbuf *m0;
	struct ip6_pktopts *opt;
#ifdef NEW_STRUCT_ROUTE
	struct route *ro;
#else
	struct route_in6 *ro;
#endif
	int flags;
	struct ip6_moptions *im6o;
	struct ifnet **ifpp;		/* XXX: just for statistics */
{
	struct ip6_hdr *ip6, *mhip6;
	struct ifnet *ifp, *origifp;
	struct mbuf *m = m0;
	int hlen, tlen, len, off;
#ifdef NEW_STRUCT_ROUTE
	struct route ip6route;
#else
	struct route_in6 ip6route;
#endif
	struct sockaddr_in6 *dst;
	int error = 0;
	struct in6_ifaddr *ia;
	u_long mtu;
	u_int32_t optlen = 0, plen = 0, unfragpartlen = 0;
	struct ip6_exthdrs exthdrs;
	struct in6_addr finaldst;
#ifdef NEW_STRUCT_ROUTE
	struct route *ro_pmtu = NULL;
#else
	struct route_in6 *ro_pmtu = NULL;
#endif
	int hdrsplit = 0;
#ifdef __OpenBSD__
	u_int8_t sproto = 0;
#else
	int needipsec = 0;
#endif
#if defined(__NetBSD__) && defined(PFIL_HOOKS)
	struct packet_filter_hook *pfh;
	struct mbuf *m1;
	int rv;
#endif /* PFIL_HOOKS */
#if defined(__bsdi__) && _BSDI_VERSION < 199802
	struct ifnet *loifp = &loif;
#endif
#ifdef IPSEC
#ifdef __OpenBSD__
	union sockaddr_union sdst;
	u_int32_t sspi;
	struct inpcb *inp;
	struct tdb *tdb;
	int s;

	inp = NULL;     /*XXX*/
	if (inp && (inp->inp_flags & INP_IPV6) == 0)
		panic("ip6_output: IPv4 pcb is passed");
#else
	int needipsectun = 0;
	struct socket *so;
	struct secpolicy *sp = NULL;

	/* for AH processing. stupid to have "socket" variable in IP layer... */
	so = ipsec_getsocket(m);
	(void)ipsec_setsocket(m, NULL);
	ip6 = mtod(m, struct ip6_hdr *);
#endif
#endif /* IPSEC */

#define MAKE_EXTHDR(hp, mp)						\
    do {								\
	if (hp) {							\
		struct ip6_ext *eh = (struct ip6_ext *)(hp);		\
		error = ip6_copyexthdr((mp), (caddr_t)(hp), 		\
				       ((eh)->ip6e_len + 1) << 3);	\
		if (error)						\
			goto freehdrs;					\
	}								\
    } while (0)
	
	bzero(&exthdrs, sizeof(exthdrs));
	
#ifdef MIP6
	/*
	 * Mobile IPv6
	 *
	 * Call Mobile IPv6 to check if there are any Destination Header
	 * options to add.
	 */
	if (mip6_output_hook) {
		error = (*mip6_output_hook)(m, &opt);
		if (error)
			goto freehdrs;
	}
#endif /* MIP6 */

	if (opt) {
		/* Hop-by-Hop options header */
		MAKE_EXTHDR(opt->ip6po_hbh, &exthdrs.ip6e_hbh);
		if (opt->ip6po_rthdr) {
			/*
			 * Destination options header(1st part)
			 * This only makes sence with a routing header.
			 */
			MAKE_EXTHDR(opt->ip6po_dest1, &exthdrs.ip6e_dest1);
		}
		/* Routing header */
		MAKE_EXTHDR(opt->ip6po_rthdr, &exthdrs.ip6e_rthdr);
		/* Destination options header(2nd part) */
		MAKE_EXTHDR(opt->ip6po_dest2, &exthdrs.ip6e_dest2);
	}

#ifdef IPSEC
#ifdef __OpenBSD__
	/* Disallow nested IPsec for now */
	if (flags & IPV6_ENCAPSULATED)
		goto done_spd;

	/*
	 * splnet is chosen over spltdb because we are not allowed to
	 * lower the level, and udp6_output calls us in splnet(). XXX check
	 */
	s = splnet();

	/*
	 * Check if there was an outgoing SA bound to the flow
	 * from a transport protocol.
	 */
	ip6 = mtod(m, struct ip6_hdr *);
	if (inp && inp->inp_tdb &&
	    inp->inp_tdb->tdb_dst.sa.sa_family == AF_INET6 &&
	    IN6_ARE_ADDR_EQUAL(&inp->inp_tdb->tdb_dst.sin6.sin6_addr,
		  &ip6->ip6_dst)) {
	        tdb = inp->inp_tdb;
	} else {
	        tdb = ipsp_spd_lookup(m, AF_INET6, sizeof(struct ip6_hdr),
	            &error, IPSP_DIRECTION_OUT, NULL, NULL);
	}

	if (tdb == NULL) {
	        splx(s);

		if (error == 0) {
		        /*
			 * No IPsec processing required, we'll just send the
			 * packet out.
			 */
		        sproto = 0;

			/* Fall through to routing/multicast handling */
		} else {
		        /*
			 * -EINVAL is used to indicate that the packet should
			 * be silently dropped, typically because we've asked
			 * key management for an SA.
			 */
		        if (error == -EINVAL) /* Should silently drop packet */
				error = 0;

			goto freehdrs;
		}
	} else {
	        /* We need to do IPsec */
	        bcopy(&tdb->tdb_dst, &sdst, sizeof(sdst));
		sspi = tdb->tdb_spi;
		sproto = tdb->tdb_sproto;

		/*
		 * If the socket has set the bypass flags and SA destination
		 * matches the IP destination, skip IPsec. This allows
		 * IKE packets to travel through IPsec tunnels.
		 */
		if (inp != NULL && 
		    inp->inp_seclevel[SL_AUTH] == IPSEC_LEVEL_BYPASS &&
		    inp->inp_seclevel[SL_ESP_TRANS] == IPSEC_LEVEL_BYPASS &&
		    inp->inp_seclevel[SL_ESP_NETWORK] == IPSEC_LEVEL_BYPASS &&
		    sdst.sa.sa_family == AF_INET6 &&
		    IN6_ARE_ADDR_EQUAL(&sdst.sin6.sin6_addr, &ip6->ip6_dst)) {
		        splx(s);
		        sproto = 0; /* mark as no-IPsec-needed */
			goto done_spd;
		}

		/* XXX Take into consideration socket requirements ? */

#if 1 /* XXX */
		/* if we have any extension header, we cannot perform IPsec */
		if (exthdrs.ip6e_hbh || exthdrs.ip6e_dest1 ||
		    exthdrs.ip6e_rthdr || exthdrs.ip6e_dest2) {
			splx(s);
			error = EHOSTUNREACH;
			goto freehdrs;
		}
#endif
		splx(s);
	}

	/* Fall through to the routing/multicast handling code */
 done_spd:
#else
	/* get a security policy for this packet */
	if (so == NULL)
		sp = ipsec6_getpolicybyaddr(m, IPSEC_DIR_OUTBOUND, 0, &error);
	else
		sp = ipsec6_getpolicybysock(m, IPSEC_DIR_OUTBOUND, so, &error);

	if (sp == NULL) {
		ipsec6stat.out_inval++;
		goto freehdrs;
	}

	error = 0;

	/* check policy */
	switch (sp->policy) {
	case IPSEC_POLICY_DISCARD:
		/*
		 * This packet is just discarded.
		 */
		ipsec6stat.out_polvio++;
		goto freehdrs;

	case IPSEC_POLICY_BYPASS:
	case IPSEC_POLICY_NONE:
		/* no need to do IPsec. */
		needipsec = 0;
		break;
	
	case IPSEC_POLICY_IPSEC:
		if (sp->req == NULL) {
			/* acquire a policy */
			error = key_spdacquire(sp);
			goto freehdrs;
		}
		needipsec = 1;
		break;

	case IPSEC_POLICY_ENTRUST:
	default:
		printf("ip6_output: Invalid policy found. %d\n", sp->policy);
	}
#endif /* OpenBSD */
#endif /* IPSEC */

	/*
	 * Calculate the total length of the extension header chain.
	 * Keep the length of the unfragmentable part for fragmentation.
	 */
	optlen = 0;
	if (exthdrs.ip6e_hbh) optlen += exthdrs.ip6e_hbh->m_len;
	if (exthdrs.ip6e_dest1) optlen += exthdrs.ip6e_dest1->m_len;
	if (exthdrs.ip6e_rthdr) optlen += exthdrs.ip6e_rthdr->m_len;
	unfragpartlen = optlen + sizeof(struct ip6_hdr);
	/* NOTE: we don't add AH/ESP length here. do that later. */
	if (exthdrs.ip6e_dest2) optlen += exthdrs.ip6e_dest2->m_len;

	/*
	 * If we need IPsec, or there is at least one extension header,
	 * separate IP6 header from the payload.
	 */
#ifdef __OpenBSD__
	if ((sproto || optlen) && !hdrsplit)
#else
	if ((needipsec || optlen) && !hdrsplit)
#endif
	{
		if ((error = ip6_splithdr(m, &exthdrs)) != 0) {
			m = NULL;
			goto freehdrs;
		}
		m = exthdrs.ip6e_ip6;
		hdrsplit++;
	}

	/* adjust pointer */
	ip6 = mtod(m, struct ip6_hdr *);

	/* adjust mbuf packet header length */
	m->m_pkthdr.len += optlen;
	plen = m->m_pkthdr.len - sizeof(*ip6);

	/* If this is a jumbo payload, insert a jumbo payload option. */
	if (plen > IPV6_MAXPACKET) {
		if (!hdrsplit) {
			if ((error = ip6_splithdr(m, &exthdrs)) != 0) {
				m = NULL;
				goto freehdrs;
			}
			m = exthdrs.ip6e_ip6;
			hdrsplit++;
		}
		/* adjust pointer */
		ip6 = mtod(m, struct ip6_hdr *);
		if ((error = ip6_insert_jumboopt(&exthdrs, plen)) != 0)
			goto freehdrs;
		ip6->ip6_plen = 0;
	} else
		ip6->ip6_plen = htons(plen);

	/*
	 * Concatenate headers and fill in next header fields.
	 * Here we have, on "m"
	 *	IPv6 payload
	 * and we insert headers accordingly.  Finally, we should be getting:
	 *	IPv6 hbh dest1 rthdr ah* [esp* dest2 payload]
	 *
	 * during the header composing process, "m" points to IPv6 header.
	 * "mprev" points to an extension header prior to esp.
	 */
	{
		u_char *nexthdrp = &ip6->ip6_nxt;
		struct mbuf *mprev = m;

		/*
		 * we treat dest2 specially.  this makes IPsec processing
		 * much easier.
		 *
		 * result: IPv6 dest2 payload
		 * m and mprev will point to IPv6 header.
		 */
		if (exthdrs.ip6e_dest2) {
			if (!hdrsplit)
				panic("assumption failed: hdr not split");
			exthdrs.ip6e_dest2->m_next = m->m_next;
			m->m_next = exthdrs.ip6e_dest2;
			*mtod(exthdrs.ip6e_dest2, u_char *) = ip6->ip6_nxt;
			ip6->ip6_nxt = IPPROTO_DSTOPTS;
		}

#define MAKE_CHAIN(m, mp, p, i)\
    do {\
	if (m) {\
		if (!hdrsplit) \
			panic("assumption failed: hdr not split"); \
		*mtod((m), u_char *) = *(p);\
		*(p) = (i);\
		p = mtod((m), u_char *);\
		(m)->m_next = (mp)->m_next;\
		(mp)->m_next = (m);\
		(mp) = (m);\
	}\
    } while (0)
		/*
		 * result: IPv6 hbh dest1 rthdr dest2 payload
		 * m will point to IPv6 header.  mprev will point to the
		 * extension header prior to dest2 (rthdr in the above case).
		 */
		MAKE_CHAIN(exthdrs.ip6e_hbh, mprev,
			   nexthdrp, IPPROTO_HOPOPTS);
		MAKE_CHAIN(exthdrs.ip6e_dest1, mprev,
			   nexthdrp, IPPROTO_DSTOPTS);
		MAKE_CHAIN(exthdrs.ip6e_rthdr, mprev,
			   nexthdrp, IPPROTO_ROUTING);

#if defined(IPSEC) && !defined(__OpenBSD__)
		if (!needipsec)
			goto skip_ipsec2;

		/*
		 * pointers after IPsec headers are not valid any more.
		 * other pointers need a great care too.
		 * (IPsec routines should not mangle mbufs prior to AH/ESP)
		 */
		exthdrs.ip6e_dest2 = NULL;

	    {
		struct ip6_rthdr *rh = NULL;
		int segleft_org = 0;
		struct ipsec_output_state state;

		if (exthdrs.ip6e_rthdr) {
			rh = mtod(exthdrs.ip6e_rthdr, struct ip6_rthdr *);
			segleft_org = rh->ip6r_segleft;
			rh->ip6r_segleft = 0;
		}

		bzero(&state, sizeof(state));
		state.m = m;
		error = ipsec6_output_trans(&state, nexthdrp, mprev, sp, flags,
			&needipsectun);
		m = state.m;
		if (error) {
			/* mbuf is already reclaimed in ipsec6_output_trans. */
			m = NULL;
			switch (error) {
			case EHOSTUNREACH:
			case ENETUNREACH:
			case EMSGSIZE:
			case ENOBUFS:
			case ENOMEM:
				break;
			default:
				printf("ip6_output (ipsec): error code %d\n", error);
				/*fall through*/
			case ENOENT:
				/* don't show these error codes to the user */
				error = 0;
				break;
			}
			goto bad;
		}
		if (exthdrs.ip6e_rthdr) {
			/* ah6_output doesn't modify mbuf chain */
			rh->ip6r_segleft = segleft_org;
		}
	    }
skip_ipsec2:;
#endif
	}

	/*
	 * If there is a routing header, replace destination address field
	 * with the first hop of the routing header.
	 */
	if (exthdrs.ip6e_rthdr) {
		struct ip6_rthdr *rh =
			(struct ip6_rthdr *)(mtod(exthdrs.ip6e_rthdr,
						  struct ip6_rthdr *));
		struct ip6_rthdr0 *rh0;
		struct in6_addr *addr;

		finaldst = ip6->ip6_dst;
		switch (rh->ip6r_type) {
		case IPV6_RTHDR_TYPE_0:
			 rh0 = (struct ip6_rthdr0 *)rh;
			 addr = (struct in6_addr *)(rh0 + 1);

			 ip6->ip6_dst = *addr;
			 bcopy((caddr_t)(addr + 1), (caddr_t)addr,
				 sizeof(struct in6_addr) * (rh0->ip6r0_segleft - 1)
				 );
			 *(addr + rh0->ip6r0_segleft - 1) = finaldst;
			 break;
		default:	/* is it possible? */
			 error = EINVAL;
			 goto bad;
		}
	}

	/* Source address validation */
	if (IN6_IS_ADDR_UNSPECIFIED(&ip6->ip6_src) &&
	    (flags & IPV6_DADOUTPUT) == 0) {
		error = EOPNOTSUPP;
		ip6stat.ip6s_badscope++;
		goto bad;
	}
	if (IN6_IS_ADDR_MULTICAST(&ip6->ip6_src)) {
		error = EOPNOTSUPP;
		ip6stat.ip6s_badscope++;
		goto bad;
	}

	ip6stat.ip6s_localout++;

	/*
	 * Route packet.
	 */
	if (ro == 0) {
		ro = &ip6route;
		bzero((caddr_t)ro, sizeof(*ro));
	}
	ro_pmtu = ro;
	if (opt && opt->ip6po_rthdr)
		ro = &opt->ip6po_route;
	dst = (struct sockaddr_in6 *)&ro->ro_dst;
	/*
	 * If there is a cached route,
	 * check that it is to the same destination
	 * and is still up. If not, free it and try again.
	 */
	if (ro->ro_rt && ((ro->ro_rt->rt_flags & RTF_UP) == 0 ||
			 !IN6_ARE_ADDR_EQUAL(&dst->sin6_addr, &ip6->ip6_dst))) {
		RTFREE(ro->ro_rt);
		ro->ro_rt = (struct rtentry *)0;
	}
	if (ro->ro_rt == 0) {
		bzero(dst, sizeof(*dst));
		dst->sin6_family = AF_INET6;
		dst->sin6_len = sizeof(struct sockaddr_in6);
		dst->sin6_addr = ip6->ip6_dst;
#ifdef SCOPEDROUTING
		/* XXX: sin6_scope_id should already be fixed at this point */
		if (IN6_IS_SCOPE_LINKLOCAL(&dst->sin6_addr))
			dst->sin6_scope_id = ntohs(dst->sin6_addr.s6_addr16[1]);
#endif
	}
#ifdef IPSEC
#ifdef __OpenBSD__
	/*
	 * Check if the packet needs encapsulation.
	 * ipsp_process_packet will never come back to here.
	 */
	if (sproto != 0) {
	        s = splnet();

		/* fill in IPv6 header which would be filled later */
		if (!IN6_IS_ADDR_MULTICAST(&ip6->ip6_dst)) {
			if (opt && opt->ip6po_hlim != -1)
				ip6->ip6_hlim = opt->ip6po_hlim & 0xff;
		} else {
			if (im6o != NULL)
				ip6->ip6_hlim = im6o->im6o_multicast_hlim;
			else
				ip6->ip6_hlim = ip6_defmcasthlim;
			if (opt && opt->ip6po_hlim != -1)
				ip6->ip6_hlim = opt->ip6po_hlim & 0xff;

			/*
			 * XXX what should we do if ip6_hlim == 0 and the packet
			 * gets tunnelled?
			 */
		}

		tdb = gettdb(sspi, &sdst, sproto);
		if (tdb == NULL) {
			error = EHOSTUNREACH;
			m_freem(m);
			splx(s);
			goto done;
		}

		m->m_flags &= ~(M_BCAST | M_MCAST);	/* just in case */

		/* Callee frees mbuf */
		error = ipsp_process_packet(m, tdb, AF_INET6, 0);
		splx(s);
		return error;  /* Nothing more to be done */
	}
#else
	if (needipsec && needipsectun) {
		struct ipsec_output_state state;

		/*
		 * All the extension headers will become inaccessible
		 * (since they can be encrypted).
		 * Don't panic, we need no more updates to extension headers
		 * on inner IPv6 packet (since they are now encapsulated).
		 *
		 * IPv6 [ESP|AH] IPv6 [extension headers] payload
		 */
		bzero(&exthdrs, sizeof(exthdrs));
		exthdrs.ip6e_ip6 = m;

		bzero(&state, sizeof(state));
		state.m = m;
		state.ro = (struct route *)ro;
		state.dst = (struct sockaddr *)dst;

		error = ipsec6_output_tunnel(&state, sp, flags);

		m = state.m;
#ifdef NEW_STRUCT_ROUTE
		ro = state.ro;
#else
		ro = (struct route_in6 *)state.ro;
#endif
		dst = (struct sockaddr_in6 *)state.dst;
		if (error) {
			/* mbuf is already reclaimed in ipsec6_output_tunnel. */
			m0 = m = NULL;
			m = NULL;
			switch (error) {
			case EHOSTUNREACH:
			case ENETUNREACH:
			case EMSGSIZE:
			case ENOBUFS:
			case ENOMEM:
				break;
			default:
				printf("ip6_output (ipsec): error code %d\n", error);
				/*fall through*/
			case ENOENT:
				/* don't show these error codes to the user */
				error = 0;
				break;
			}
			goto bad;
		}

		exthdrs.ip6e_ip6 = m;
	}
#endif /*OpenBSD*/
#endif /*IPSEC*/

	if (!IN6_IS_ADDR_MULTICAST(&ip6->ip6_dst)) {
		/* Unicast */

#define ifatoia6(ifa)	((struct in6_ifaddr *)(ifa))
#define sin6tosa(sin6)	((struct sockaddr *)(sin6))
		/* xxx
		 * interface selection comes here
		 * if an interface is specified from an upper layer,
		 * ifp must point it.
		 */
		if (ro->ro_rt == 0) {
#ifndef __bsdi__
			/*
			 * non-bsdi always clone routes, if parent is
			 * PRF_CLONING.
			 */
			rtalloc((struct route *)ro);
#else
			if (ro == &ip6route)	/* xxx kazu */
				rtalloc((struct route *)ro);
			else
				rtcalloc((struct route *)ro);
#endif
		}
		if (ro->ro_rt == 0) {
			ip6stat.ip6s_noroute++;
			error = EHOSTUNREACH;
			/* XXX in6_ifstat_inc(ifp, ifs6_out_discard); */
			goto bad;
		}
		ia = ifatoia6(ro->ro_rt->rt_ifa);
		ifp = ro->ro_rt->rt_ifp;
		ro->ro_rt->rt_use++;
		if (ro->ro_rt->rt_flags & RTF_GATEWAY)
			dst = (struct sockaddr_in6 *)ro->ro_rt->rt_gateway;
		m->m_flags &= ~(M_BCAST | M_MCAST);	/* just in case */

		in6_ifstat_inc(ifp, ifs6_out_request);

		/*
		 * Check if the outgoing interface conflicts with
		 * the interface specified by ifi6_ifindex (if specified).
		 * Note that loopback interface is always okay.
		 * (this may happen when we are sending a packet to one of
		 *  our own addresses.)
		 */
		if (opt && opt->ip6po_pktinfo
		 && opt->ip6po_pktinfo->ipi6_ifindex) {
			if (!(ifp->if_flags & IFF_LOOPBACK)
			 && ifp->if_index != opt->ip6po_pktinfo->ipi6_ifindex) {
				ip6stat.ip6s_noroute++;
				in6_ifstat_inc(ifp, ifs6_out_discard);
				error = EHOSTUNREACH;
				goto bad;
			}
		}

		if (opt && opt->ip6po_hlim != -1)
			ip6->ip6_hlim = opt->ip6po_hlim & 0xff;
		if (opt) {
			ip6->ip6_flow &= ~htonl(0xff << 20);
			ip6->ip6_flow |= htonl(opt->ip6po_tclass << 20);
		}
	} else {
		/* Multicast */
		struct	in6_multi *in6m;

		m->m_flags = (m->m_flags & ~M_BCAST) | M_MCAST;

		/*
		 * See if the caller provided any multicast options
		 */
		ifp = NULL;
		if (im6o != NULL) {
			ip6->ip6_hlim = im6o->im6o_multicast_hlim;
			if (im6o->im6o_multicast_ifp != NULL)
				ifp = im6o->im6o_multicast_ifp;
		} else
			ip6->ip6_hlim = ip6_defmcasthlim;

		/*
		 * See if the caller provided the outgoing interface
		 * as an ancillary data.
		 * Boundary check for ifindex is assumed to be already done.
		 */
		if (opt && opt->ip6po_pktinfo && opt->ip6po_pktinfo->ipi6_ifindex)
			ifp = ifindex2ifnet[opt->ip6po_pktinfo->ipi6_ifindex];

		/*
		 * If the destination is a node-local scope multicast,
		 * the packet should be loop-backed only.
		 */
		if (IN6_IS_ADDR_MC_NODELOCAL(&ip6->ip6_dst)) {
			/*
			 * If the outgoing interface is already specified,
			 * it should be a loopback interface.
			 */
			if (ifp && (ifp->if_flags & IFF_LOOPBACK) == 0) {
				ip6stat.ip6s_badscope++;
				error = ENETUNREACH; /* XXX: better error? */
				/* XXX correct ifp? */
				in6_ifstat_inc(ifp, ifs6_out_discard);
				goto bad;
			} else {
#ifdef __bsdi__
				ifp = loifp;
#else
				ifp = &loif[0];
#endif
			}
		}

		if (opt && opt->ip6po_hlim != -1)
			ip6->ip6_hlim = opt->ip6po_hlim & 0xff;
		if (opt) {
			ip6->ip6_flow &= ~htonl(0xff << 20);
			ip6->ip6_flow |= htonl(opt->ip6po_tclass << 20);
		}

		/*
		 * If caller did not provide an interface lookup a
		 * default in the routing table.  This is either a
		 * default for the speicfied group (i.e. a host
		 * route), or a multicast default (a route for the
		 * ``net'' ff00::/8).
		 */
		if (ifp == NULL) {
			if (ro->ro_rt == 0) {
				ro->ro_rt = rtalloc1((struct sockaddr *)
						&ro->ro_dst, 0
#ifdef __FreeBSD__
						, 0UL
#endif
						);
			}
			if (ro->ro_rt == 0) {
				ip6stat.ip6s_noroute++;
				error = EHOSTUNREACH;
				/* XXX in6_ifstat_inc(ifp, ifs6_out_discard) */
				goto bad;
			}
			ia = ifatoia6(ro->ro_rt->rt_ifa);
			ifp = ro->ro_rt->rt_ifp;
			ro->ro_rt->rt_use++;
		}

		if ((flags & IPV6_FORWARDING) == 0)
			in6_ifstat_inc(ifp, ifs6_out_request);
		in6_ifstat_inc(ifp, ifs6_out_mcast);

		/*
		 * Confirm that the outgoing interface supports multicast.
		 */
		if ((ifp->if_flags & IFF_MULTICAST) == 0) {
			ip6stat.ip6s_noroute++;
			in6_ifstat_inc(ifp, ifs6_out_discard);
			error = ENETUNREACH;
			goto bad;
		}
		IN6_LOOKUP_MULTI(ip6->ip6_dst, ifp, in6m);
		if (in6m != NULL &&
		   (im6o == NULL || im6o->im6o_multicast_loop)) {
			/*
			 * If we belong to the destination multicast group
			 * on the outgoing interface, and the caller did not
			 * forbid loopback, loop back a copy.
			 */
			ip6_mloopback(ifp, m, dst);
		} else {
			/*
			 * If we are acting as a multicast router, perform
			 * multicast forwarding as if the packet had just
			 * arrived on the interface to which we are about
			 * to send.  The multicast forwarding function
			 * recursively calls this function, using the
			 * IPV6_FORWARDING flag to prevent infinite recursion.
			 *
			 * Multicasts that are looped back by ip6_mloopback(),
			 * above, will be forwarded by the ip6_input() routine,
			 * if necessary.
			 */
			if (ip6_mrouter && (flags & IPV6_FORWARDING) == 0) {
				if (ip6_mforward(ip6, ifp, m) != 0) {
					m_freem(m);
					goto done;
				}
			}
		}
		/*
		 * Multicasts with a hoplimit of zero may be looped back,
		 * above, but must not be transmitted on a network.
		 * Also, multicasts addressed to the loopback interface
		 * are not sent -- the above call to ip6_mloopback() will
		 * loop back a copy if this host actually belongs to the
		 * destination group on the loopback interface.
		 */
		if (ip6->ip6_hlim == 0 || (ifp->if_flags & IFF_LOOPBACK)) {
			m_freem(m);
			goto done;
		}
	}

	/*
	 * Fill the outgoing inteface to tell the upper layer
	 * to increment per-interface statistics.
	 */
	if (ifpp)
		*ifpp = ifp;

	/*
	 * Upper-layer reachability confirmation
	 */
	if (opt && (opt->ip6po_flags & IP6PO_REACHCONF))
		nd6_nud_hint(ro->ro_rt, NULL, 0);

	/*
	 * Determine path MTU.
	 */
	if (ro_pmtu != ro) {
		/* The first hop and the final destination may differ. */
		struct sockaddr_in6 *sin6_fin =
			(struct sockaddr_in6 *)&ro_pmtu->ro_dst;
		if (ro_pmtu->ro_rt && ((ro->ro_rt->rt_flags & RTF_UP) == 0 ||
				       !IN6_ARE_ADDR_EQUAL(&sin6_fin->sin6_addr,
							   &finaldst))) {
			RTFREE(ro_pmtu->ro_rt);
			ro_pmtu->ro_rt = (struct rtentry *)0;
		}
		if (ro_pmtu->ro_rt == 0) {
			bzero(sin6_fin, sizeof(*sin6_fin));
			sin6_fin->sin6_family = AF_INET6;
			sin6_fin->sin6_len = sizeof(struct sockaddr_in6);
			sin6_fin->sin6_addr = finaldst;

#ifdef __bsdi__			/* bsdi needs rtcalloc to clone a route. */
			rtcalloc((struct route *)ro_pmtu);
#else
			rtalloc((struct route *)ro_pmtu);
#endif
		}
	}
	if (ro_pmtu->ro_rt != NULL) {
		u_int32_t ifmtu = nd_ifinfo[ifp->if_index].linkmtu;

		mtu = ro_pmtu->ro_rt->rt_rmx.rmx_mtu;
		if (mtu > ifmtu) {
			/*
			 * The MTU on the route is larger than the MTU on
			 * the interface!  This shouldn't happen, unless the
			 * MTU of the interface has been changed after the
			 * interface was brought up.  Change the MTU in the
			 * route to match the interface MTU (as long as the
			 * field isn't locked).
			 */
			 mtu = ifmtu;
			 if ((ro_pmtu->ro_rt->rt_rmx.rmx_locks & RTV_MTU) == 0)
				 ro_pmtu->ro_rt->rt_rmx.rmx_mtu = mtu; /* XXX */
		}
	} else {
		mtu = nd_ifinfo[ifp->if_index].linkmtu;
	}

	/*
	 * advanced API (IPV6_USE_MIN_MTU or IPV6_USE_MTU - the latter is
	 * KAME-specific at this moment -) overrides mtu setting. We ignore
	 * the specified MTU if it is larger than the already-known path MTU.
	 * XXX: which should be preferred, if both IPV6_USE_MIN_MTU and
	 * IPV6_USE_MTU are specified?
	 */
	if (mtu > IPV6_MMTU) {
		if ((opt && (opt->ip6po_flags & IP6PO_MINMTU)) ||
		    (flags & IPV6_MINMTU)) {
			mtu = IPV6_MMTU;
		}
		else if (opt && (opt->ip6po_mtu != -1) &&
			 mtu > opt->ip6po_mtu) {
			mtu = opt->ip6po_mtu;
		}
	}

	/* Fake scoped addresses */
	if ((ifp->if_flags & IFF_LOOPBACK) != 0) {
		/*
		 * If source or destination address is a scoped address, and
		 * the packet is going to be sent to a loopback interface,
		 * we should keep the original interface.
		 */

		/*
		 * XXX: this is a very experimental and temporary solution.
		 * We eventually have sockaddr_in6 and use the sin6_scope_id
		 * field of the structure here.
		 * We rely on the consistency between two scope zone ids
		 * of source add destination, which should already be assured
		 * Larger scopes than link will be supported in the near
		 * future.
		 */
		origifp = NULL;
		if (IN6_IS_SCOPE_LINKLOCAL(&ip6->ip6_src))
			origifp = ifindex2ifnet[ntohs(ip6->ip6_src.s6_addr16[1])];
		else if (IN6_IS_SCOPE_LINKLOCAL(&ip6->ip6_dst))
			origifp = ifindex2ifnet[ntohs(ip6->ip6_dst.s6_addr16[1])];
		/*
		 * XXX: origifp can be NULL even in those two cases above.
		 * For example, if we remove the (only) link-local address
		 * from the loopback interface, and try to send a link-local
		 * address without link-id information.  Then the source
		 * address is ::1, and the destination address is the
		 * link-local address with its s6_addr16[1] being zero.
		 * What is worse, if the packet goes to the loopback interface
		 * by a default rejected route, the null pointer would be
		 * passed to looutput, and the kernel would hang.
		 * The following last resort would prevent such disaster.
		 */
		if (origifp == NULL)
			origifp = ifp;
	}
	else
		origifp = ifp;
#ifndef SCOPEDROUTING
	/*
	 * clear embedded scope identifiers if necessary.
	 * in6_clearscope will touch the addresses only when necessary.
	 */
	in6_clearscope(&ip6->ip6_src);
	in6_clearscope(&ip6->ip6_dst);
#endif

#if defined(IPV6FIREWALL) || (defined(__FreeBSD__) && __FreeBSD__ >= 4)
	/*
	 * Check with the firewall...
	 */
#if defined(__FreeBSD__) && __FreeBSD__ >= 4
        if (ip6_fw_enable && ip6_fw_chk_ptr) {
#else
        if (ip6_fw_chk_ptr) {
#endif
		u_short port = 0;
		m->m_pkthdr.rcvif = NULL;	/*XXX*/
		/* If ipfw says divert, we have to just drop packet */
		if ((*ip6_fw_chk_ptr)(&ip6, ifp, &port, &m)) {
			m_freem(m);
			goto done;
		}
		if (!m) {
			error = EACCES;
			goto done;
		}
	}
#endif

	/*
	 * If the outgoing packet contains a hop-by-hop options header,
	 * it must be examined and processed even by the source node.
	 * (RFC 2460, section 4.)
	 */
	if (exthdrs.ip6e_hbh) {
		struct ip6_hbh *hbh = mtod(exthdrs.ip6e_hbh, struct ip6_hbh *);
		u_int32_t dummy1; /* XXX unused */
		u_int32_t dummy2; /* XXX unused */

#ifdef DIAGNOSTIC
		if ((hbh->ip6h_len + 1) << 3 > exthdrs.ip6e_hbh->m_len)
			panic("ip6e_hbh is not continuous");
#endif
		/*
		 *  XXX: if we have to send an ICMPv6 error to the sender,
		 *       we need the M_LOOP flag since icmp6_error() expects
		 *       the IPv6 and the hop-by-hop options header are
		 *       continuous unless the flag is set.
		 */
		m->m_flags |= M_LOOP;
		m->m_pkthdr.rcvif = ifp;
		if (ip6_process_hopopts(m,
					(u_int8_t *)(hbh + 1),
					((hbh->ip6h_len + 1) << 3) -
					sizeof(struct ip6_hbh),
					&dummy1, &dummy2) < 0) {
			/* m was already freed at this point */
			error = EINVAL;/* better error? */
			goto done;
		}
		m->m_flags &= ~M_LOOP; /* XXX */
		m->m_pkthdr.rcvif = NULL;
	}

#if defined(__NetBSD__) && defined(PFIL_HOOKS)
	/*
	 * Run through list of hooks for output packets.
	 */
	m1 = m;
	pfh = pfil_hook_get(PFIL_OUT, &inetsw[ip_protox[IPPROTO_IPV6]].pr_pfh);
	for (; pfh; pfh = pfh->pfil_link.tqe_next)
		if (pfh->pfil_func) {
		    	rv = pfh->pfil_func(ip6, sizeof(*ip6), ifp, 1, &m1);
			if (rv) {
				error = EHOSTUNREACH;
				goto done;
			}
			m = m1;
			if (m == NULL)
				goto done;
			ip6 = mtod(m, struct ip6_hdr *);
		}
#endif /* PFIL_HOOKS */
	/*
	 * Send the packet to the outgoing interface.
	 * If necessary, do IPv6 fragmentation before sending.
	 */
	tlen = m->m_pkthdr.len;
	if (tlen <= mtu
#ifdef notyet
	    /*
	     * On any link that cannot convey a 1280-octet packet in one piece,
	     * link-specific fragmentation and reassembly must be provided at
	     * a layer below IPv6. [RFC 2460, sec.5]
	     * Thus if the interface has ability of link-level fragmentation,
	     * we can just send the packet even if the packet size is
	     * larger than the link's MTU.
	     * XXX: IFF_FRAGMENTABLE (or such) flag has not been defined yet...
	     */
	
	    || ifp->if_flags & IFF_FRAGMENTABLE
#endif
	    )
	{
#if defined(__NetBSD__) && defined(IFA_STATS)
		struct in6_ifaddr *ia6;
		ip6 = mtod(m, struct ip6_hdr *);
		ia6 = in6_ifawithifp(ifp, &ip6->ip6_src);
		if (ia6) {
			ia6->ia_ifa.ifa_data.ifad_outbytes +=
				m->m_pkthdr.len;
		}
#endif
#if defined(IPSEC) && !defined(__OpenBSD__)
		/* clean ipsec history once it goes out of the node */
		ipsec_delaux(m);
#endif
#ifdef OLDIP6OUTPUT
		error = (*ifp->if_output)(ifp, m, (struct sockaddr *)dst,
					  ro->ro_rt);
#else
		error = nd6_output(ifp, origifp, m, dst, ro->ro_rt);
#endif
		goto done;
	} else if (mtu < IPV6_MMTU) {
		/*
		 * note that path MTU is never less than IPV6_MMTU
		 * (see icmp6_input).
		 */
		error = EMSGSIZE;
		in6_ifstat_inc(ifp, ifs6_out_fragfail);
		goto bad;
	} else if (ip6->ip6_plen == 0) { /* jumbo payload cannot be fragmented */
		error = EMSGSIZE;
		in6_ifstat_inc(ifp, ifs6_out_fragfail);
		goto bad;
	} else {
		struct mbuf **mnext, *m_frgpart;
		struct ip6_frag *ip6f;
		u_int32_t id = htonl(ip6_id++);
		u_char nextproto;
		struct ip6ctlparam ip6cp;

		/*
		 * Too large for the destination or interface;
		 * fragment if possible.
		 * Must be able to put at least 8 bytes per fragment.
		 */
		hlen = unfragpartlen;
		if (mtu > IPV6_MAXPACKET)
			mtu = IPV6_MAXPACKET;

		/*
		 * Notify a proper path MTU to applications. We skip this
		 * process if an outgoing MTU is specified, since fragmentation
		 * can always occur when an application tries to send a larger
		 * packet than the link MTU even with specifing path MTU.
		 * Or should we always notify all applications?
		 */
		if ((opt == NULL || opt->ip6po_mtu == -1)) {
			u_int32_t mtu32;
		 	/* mtu > IPV6_MAXPACKET case is already covered */
			mtu32 = (u_int32_t)mtu;
			bzero(&ip6cp, sizeof(ip6cp));
			ip6cp.ip6c_cmdarg = (void *)&mtu32;
			pfctlinput2(PRC_MSGSIZE, &ro_pmtu->ro_dst,
				    (void *)&ip6cp);
		}

		len = (mtu - hlen - sizeof(struct ip6_frag)) & ~7;
		if (len < 8) {
			error = EMSGSIZE;
			in6_ifstat_inc(ifp, ifs6_out_fragfail);
			goto bad;
		}

		mnext = &m->m_nextpkt;

		/*
		 * Change the next header field of the last header in the
		 * unfragmentable part.
		 */
		if (exthdrs.ip6e_rthdr) {
			nextproto = *mtod(exthdrs.ip6e_rthdr, u_char *);
			*mtod(exthdrs.ip6e_rthdr, u_char *) = IPPROTO_FRAGMENT;
		} else if (exthdrs.ip6e_dest1) {
			nextproto = *mtod(exthdrs.ip6e_dest1, u_char *);
			*mtod(exthdrs.ip6e_dest1, u_char *) = IPPROTO_FRAGMENT;
		} else if (exthdrs.ip6e_hbh) {
			nextproto = *mtod(exthdrs.ip6e_hbh, u_char *);
			*mtod(exthdrs.ip6e_hbh, u_char *) = IPPROTO_FRAGMENT;
		} else {
			nextproto = ip6->ip6_nxt;
			ip6->ip6_nxt = IPPROTO_FRAGMENT;
		}

		/*
		 * Loop through length of segment after first fragment,
		 * make new header and copy data of each part and link onto chain.
		 */
		m0 = m;
		for (off = hlen; off < tlen; off += len) {
			MGETHDR(m, M_DONTWAIT, MT_HEADER);
			if (!m) {
				error = ENOBUFS;
				ip6stat.ip6s_odropped++;
				goto sendorfree;
			}
			m->m_flags = m0->m_flags & M_COPYFLAGS;
			*mnext = m;
			mnext = &m->m_nextpkt;
			m->m_data += max_linkhdr;
			mhip6 = mtod(m, struct ip6_hdr *);
			*mhip6 = *ip6;
			m->m_len = sizeof(*mhip6);
 			error = ip6_insertfraghdr(m0, m, hlen, &ip6f);
 			if (error) {
				ip6stat.ip6s_odropped++;
				goto sendorfree;
			}
			ip6f->ip6f_offlg = htons((u_short)((off - hlen) & ~7));
			if (off + len >= tlen)
				len = tlen - off;
			else
				ip6f->ip6f_offlg |= IP6F_MORE_FRAG;
			mhip6->ip6_plen = htons((u_short)(len + hlen +
							  sizeof(*ip6f) -
							  sizeof(struct ip6_hdr)));
			if ((m_frgpart = m_copy(m0, off, len)) == 0) {
				error = ENOBUFS;
				ip6stat.ip6s_odropped++;
				goto sendorfree;
			}
			m_cat(m, m_frgpart);
			m->m_pkthdr.len = len + hlen + sizeof(*ip6f);
			m->m_pkthdr.rcvif = (struct ifnet *)0;
			ip6f->ip6f_reserved = 0;
			ip6f->ip6f_ident = id;
			ip6f->ip6f_nxt = nextproto;
			ip6stat.ip6s_ofragments++;
			in6_ifstat_inc(ifp, ifs6_out_fragcreat);
		}

		in6_ifstat_inc(ifp, ifs6_out_fragok);
	}

	/*
	 * Remove leading garbages.
	 */
sendorfree:
	m = m0->m_nextpkt;
	m0->m_nextpkt = 0;
	m_freem(m0);
	for (m0 = m; m; m = m0) {
		m0 = m->m_nextpkt;
		m->m_nextpkt = 0;
		if (error == 0) {
#if defined(__NetBSD__) && defined(IFA_STATS)
			struct in6_ifaddr *ia6;
			ip6 = mtod(m, struct ip6_hdr *);
			ia6 = in6_ifawithifp(ifp, &ip6->ip6_src);
			if (ia6) {
				ia6->ia_ifa.ifa_data.ifad_outbytes +=
					m->m_pkthdr.len;
			}
#endif
#if defined(IPSEC) && !defined(__OpenBSD__)
			/* clean ipsec history once it goes out of the node */
			ipsec_delaux(m);
#endif
#ifdef OLDIP6OUTPUT
			error = (*ifp->if_output)(ifp, m,
						  (struct sockaddr *)dst,
						  ro->ro_rt);
#else
			error = nd6_output(ifp, origifp, m, dst, ro->ro_rt);
#endif
		} else
			m_freem(m);
	}

	if (error == 0)
		ip6stat.ip6s_fragmented++;

done:
	if (ro == &ip6route && ro->ro_rt) { /* brace necessary for RTFREE */
		RTFREE(ro->ro_rt);
	} else if (ro_pmtu == &ip6route && ro_pmtu->ro_rt) {
		RTFREE(ro_pmtu->ro_rt);
	}

#if defined(IPSEC) && !defined(__OpenBSD__)
	if (sp != NULL)
		key_freesp(sp);
#endif /* IPSEC */

	return(error);

freehdrs:
	m_freem(exthdrs.ip6e_hbh);	/* m_freem will check if mbuf is 0 */
	m_freem(exthdrs.ip6e_dest1);
	m_freem(exthdrs.ip6e_rthdr);
	m_freem(exthdrs.ip6e_dest2);
	/* fall through */
bad:
	m_freem(m);
	goto done;
}

static int
ip6_copyexthdr(mp, hdr, hlen)
	struct mbuf **mp;
	caddr_t hdr;
	int hlen;
{
	struct mbuf *m;

	if (hlen > MCLBYTES)
		return(ENOBUFS); /* XXX */

	MGET(m, M_DONTWAIT, MT_DATA);
	if (!m)
		return(ENOBUFS);

	if (hlen > MLEN) {
		MCLGET(m, M_DONTWAIT);
		if ((m->m_flags & M_EXT) == 0) {
			m_free(m);
			return(ENOBUFS);
		}
	}
	m->m_len = hlen;
	if (hdr)
		bcopy(hdr, mtod(m, caddr_t), hlen);

	*mp = m;
	return(0);
}

/*
 * Insert jumbo payload option.
 */
static int
ip6_insert_jumboopt(exthdrs, plen)
	struct ip6_exthdrs *exthdrs;
	u_int32_t plen;
{
	struct mbuf *mopt;
	u_char *optbuf;
	u_int32_t v;

#define JUMBOOPTLEN	8	/* length of jumbo payload option and padding */

	/*
	 * If there is no hop-by-hop options header, allocate new one.
	 * If there is one but it doesn't have enough space to store the
	 * jumbo payload option, allocate a cluster to store the whole options.
	 * Otherwise, use it to store the options.
	 */
	if (exthdrs->ip6e_hbh == 0) {
		MGET(mopt, M_DONTWAIT, MT_DATA);
		if (mopt == 0)
			return(ENOBUFS);
		mopt->m_len = JUMBOOPTLEN;
		optbuf = mtod(mopt, u_char *);
		optbuf[1] = 0;	/* = ((JUMBOOPTLEN) >> 3) - 1 */
		exthdrs->ip6e_hbh = mopt;
	} else {
		struct ip6_hbh *hbh;

		mopt = exthdrs->ip6e_hbh;
		if (M_TRAILINGSPACE(mopt) < JUMBOOPTLEN) {
			/*
			 * XXX assumption:
			 * - exthdrs->ip6e_hbh is not referenced from places
			 *   other than exthdrs.
			 * - exthdrs->ip6e_hbh is not an mbuf chain.
			 */
			int oldoptlen = mopt->m_len;
			struct mbuf *n;

			/*
			 * XXX: give up if the whole (new) hbh header does
			 * not fit even in an mbuf cluster.
			 */
			if (oldoptlen + JUMBOOPTLEN > MCLBYTES)
				return(ENOBUFS);

			/*
			 * As a consequence, we must always prepare a cluster
			 * at this point.
			 */
			MGET(n, M_DONTWAIT, MT_DATA);
			if (n) {
				MCLGET(n, M_DONTWAIT);
				if ((n->m_flags & M_EXT) == 0) {
					m_freem(n);
					n = NULL;
				}
			}
			if (!n)
				return(ENOBUFS);
			n->m_len = oldoptlen + JUMBOOPTLEN;
			bcopy(mtod(mopt, caddr_t), mtod(n, caddr_t),
			      oldoptlen);
			optbuf = mtod(n, caddr_t) + oldoptlen;
			m_freem(mopt);
			exthdrs->ip6e_hbh = n;
		} else {
			optbuf = mtod(mopt, u_char *) + mopt->m_len;
			mopt->m_len += JUMBOOPTLEN;
		}
		optbuf[0] = IP6OPT_PADN;
		optbuf[1] = 1;

		/*
		 * Adjust the header length according to the pad and
		 * the jumbo payload option.
		 */
		hbh = mtod(mopt, struct ip6_hbh *);
		hbh->ip6h_len += (JUMBOOPTLEN >> 3);
	}

	/* fill in the option. */
	optbuf[2] = IP6OPT_JUMBO;
	optbuf[3] = 4;
	v = (u_int32_t)htonl(plen + JUMBOOPTLEN);
	bcopy(&v, &optbuf[4], sizeof(u_int32_t));

	/* finally, adjust the packet header length */
	exthdrs->ip6e_ip6->m_pkthdr.len += JUMBOOPTLEN;

	return(0);
#undef JUMBOOPTLEN
}

/*
 * Insert fragment header and copy unfragmentable header portions.
 */
static int
ip6_insertfraghdr(m0, m, hlen, frghdrp)
	struct mbuf *m0, *m;
	int hlen;
	struct ip6_frag **frghdrp;
{
	struct mbuf *n, *mlast;

	if (hlen > sizeof(struct ip6_hdr)) {
		n = m_copym(m0, sizeof(struct ip6_hdr),
			    hlen - sizeof(struct ip6_hdr), M_DONTWAIT);
		if (n == 0)
			return(ENOBUFS);
		m->m_next = n;
	} else
		n = m;

	/* Search for the last mbuf of unfragmentable part. */
	for (mlast = n; mlast->m_next; mlast = mlast->m_next)
		;

	if ((mlast->m_flags & M_EXT) == 0 &&
	    M_TRAILINGSPACE(mlast) >= sizeof(struct ip6_frag)) {
		/* use the trailing space of the last mbuf for the fragment hdr */
		*frghdrp =
			(struct ip6_frag *)(mtod(mlast, caddr_t) + mlast->m_len);
		mlast->m_len += sizeof(struct ip6_frag);
		m->m_pkthdr.len += sizeof(struct ip6_frag);
	} else {
		/* allocate a new mbuf for the fragment header */
		struct mbuf *mfrg;

		MGET(mfrg, M_DONTWAIT, MT_DATA);
		if (mfrg == 0)
			return(ENOBUFS);
		mfrg->m_len = sizeof(struct ip6_frag);
		*frghdrp = mtod(mfrg, struct ip6_frag *);
		mlast->m_next = mfrg;
	}

	return(0);
}

/*
 * IP6 socket option processing.
 */
#if defined(__FreeBSD__) && __FreeBSD__ >= 3
int
ip6_ctloutput(so, sopt)
	struct socket *so;
	struct sockopt *sopt;
#else
int
ip6_ctloutput(op, so, level, optname, mp)
	int op;
	struct socket *so;
	int level, optname;
	struct mbuf **mp;
#endif
{
	int privileged, optdatalen;
	void *optdata;
	struct ip6_recvpktopts *rcvopts;
#if defined(IPSEC) && defined(__OpenBSD__)
	struct proc *p = curproc; /* XXX */
	struct tdb *tdb;
	struct tdb_ident *tdbip, tdbi;
	int s;
#endif
#if defined(__FreeBSD__) && __FreeBSD__ >= 3
	struct inpcb *in6p = sotoinpcb(so);
	int error, optval;
	int level, op, optname;
	int optlen;
	struct proc *p;

	if (sopt) {
		level = sopt->sopt_level;
		op = sopt->sopt_dir;
		optname = sopt->sopt_name;
		optlen = sopt->sopt_valsize;
		p = sopt->sopt_p;
	} else {
		panic("ip6_ctloutput: arg soopt is NULL");
	}
#else
#ifdef HAVE_NRL_INPCB
	struct inpcb *inp = sotoinpcb(so);
#else
	struct in6pcb *in6p = sotoin6pcb(so);
#endif
	struct mbuf *m = *mp;
	int error, optval;
	int optlen;
#if defined(__NetBSD__) || (defined(__FreeBSD__) && __FreeBSD__ >= 3)
	struct proc *p = curproc;	/* XXX */
#endif

	optlen = m ? m->m_len : 0;
#endif
	error = optval = 0;

#if defined(__NetBSD__) || (defined(__FreeBSD__) && __FreeBSD__ == 3)
	privileged = (p == 0 || suser(p->p_ucred, &p->p_acflag)) ? 0 : 1;
#elif defined(__FreeBSD__) && __FreeBSD__ >= 4
	privileged = (p == 0 || suser(p)) ? 0 : 1;
#else
#ifdef HAVE_NRL_INPCB
	privileged = (inp->inp_socket->so_state & SS_PRIV);
#else
	privileged = (in6p->in6p_socket->so_state & SS_PRIV);
#endif
#endif

#if defined(HAVE_NRL_INPCB)
	rcvopts = inp->inp_inputopts6;
#else
	rcvopts = in6p->in6p_inputopts;
#endif

	if (level == IPPROTO_IPV6) {
		switch (op) {

#if defined(__FreeBSD__) && __FreeBSD__ >= 3
		case SOPT_SET:
#else
		case PRCO_SETOPT:
#endif
			switch (optname) {
			case IPV6_2292PKTOPTIONS:
#ifdef IPV6_PKTOPTIONS
			case IPV6_PKTOPTIONS:
#endif
			{
#if defined(__FreeBSD__) && __FreeBSD__ >= 3
				struct mbuf *m;

				error = soopt_getm(sopt, &m); /* XXX */
				if (error != NULL)
					break;
				error = soopt_mcopyin(sopt, m); /* XXX */
				if (error != NULL)
					break;
				error = ip6_pcbopts(&in6p->in6p_outputopts,
						    m, so, sopt);
				m_freem(m); /* XXX */
#else
#ifdef HAVE_NRL_INPCB
				error = ip6_pcbopts(&inp->inp_outputopts6,
						    m, so);
#else
				error = ip6_pcbopts(&in6p->in6p_outputopts,
						    m, so);
#endif /* HAVE_NRL_INPCB */
#endif /* FreeBSD >= 3 */
				break;
			}

			/*
			 * Use of some Hop-by-Hop options or some
			 * Destination options, might require special
			 * privilege.  That is, normal applications
			 * (without special privilege) might be forbidden
			 * from setting certain options in outgoing packets,
			 * and might never see certain options in received
			 * packets. [RFC 2292 Section 6]
			 * KAME specific note:
			 *  KAME prevents non-privileged users from sending or
			 *  receiving ANY hbh/dst options in order to avoid
			 *  overhead of parsing options in the kernel.
			 */
			case IPV6_RECVHOPOPTS:
			case IPV6_RECVDSTOPTS:
			case IPV6_RECVRTHDRDSTOPTS:
				if (!privileged) {
					error = EPERM;
					break;
				}
				/* fall through */
			case IPV6_UNICAST_HOPS:
			case IPV6_HOPLIMIT:
			case IPV6_FAITH:

			case IPV6_RECVPKTINFO:
			case IPV6_RECVHOPLIMIT:
			case IPV6_RECVRTHDR:
			case IPV6_USE_MIN_MTU:
			case IPV6_RECVPATHMTU:
			case IPV6_V6ONLY:
			case IPV6_RECVTCLASS:
			case IPV6_AUTOFLOWLABEL:
				if (optlen != sizeof(int)) {
					error = EINVAL;
					break;
				}
#if defined(__FreeBSD__) && __FreeBSD__ >= 3
				error = sooptcopyin(sopt, &optval,
					sizeof optval, sizeof optval);
				if (error)
					break;
#else
				optval = *mtod(m, int *);
#endif
				switch (optname) {

				case IPV6_UNICAST_HOPS:
					if (optval < -1 || optval >= 256)
						error = EINVAL;
					else {
						/* -1 = kernel default */
#ifdef HAVE_NRL_INPCB
						inp->inp_hops = optval;
#else
						in6p->in6p_hops = optval;

#if defined(__FreeBSD__) && __FreeBSD__ >= 3
						if ((in6p->in6p_vflag &
						     INP_IPV4) != 0)
							in6p->inp_ip_ttl = optval;
#endif
#endif
					}
					break;
#ifdef HAVE_NRL_INPCB
#define OPTSET(bit) \
do { \
	if (optval) \
		inp->inp_flags |= (bit); \
	else \
		inp->inp_flags &= ~(bit); \
} while (0)
#define OPTSET2292(bit) \
do { \
	inp->inp_flags |= IN6P_RFC2292; \
	if (optval) \
		inp->inp_flags |= (bit); \
	else \
		inp->inp_flags &= ~(bit); \
} while (0)
#else /*HAVE_NRL_INPCB*/
#define OPTSET(bit) \
do { \
	if (optval) \
		in6p->in6p_flags |= (bit); \
	else \
		in6p->in6p_flags &= ~(bit); \
} while (0)
#define OPTSET2292(bit) \
do { \
	in6p->in6p_flags |= IN6P_RFC2292; \
	if (optval) \
		in6p->in6p_flags |= (bit); \
	else \
		in6p->in6p_flags &= ~(bit); \
} while (0)
#endif /*HAVE_NRL_INPCB*/
#ifdef HAVE_NRL_INPCB
#define OPTBIT(bit) (inp->inp_flags & (bit) ? 1 : 0)
#else
#define OPTBIT(bit) (in6p->in6p_flags & (bit) ? 1 : 0)
#endif

				case IPV6_RECVPKTINFO:
					/* cannot mix with RFC2292 */
					if (OPTBIT(IN6P_RFC2292)) {
						error = EINVAL;
						break;
					}
					OPTSET(IN6P_PKTINFO);
					if (OPTBIT(IN6P_PKTINFO) == 0)
						ip6_reset_rcvopt(rcvopts, IPV6_RECVPKTINFO);
					break;

				case IPV6_HOPLIMIT:
				{
					struct ip6_pktopts **optp;

					/* cannot mix with RFC2292 */
					if (OPTBIT(IN6P_RFC2292)) {
						error = EINVAL;
						break;
					}
#ifdef HAVE_NRL_INPCB
					optp = &inp->inp_outputopts6;
#else
					optp = &in6p->in6p_outputopts;
#endif

					error = ip6_pcbopt(IPV6_HOPLIMIT,
							   (u_char *)&optval,
							   sizeof(optval),
							   optp,
							   privileged);
					break;
				}

				case IPV6_RECVHOPLIMIT:
					/* cannot mix with RFC2292 */
					if (OPTBIT(IN6P_RFC2292)) {
						error = EINVAL;
						break;
					}
					OPTSET(IN6P_HOPLIMIT);
					if (OPTBIT(IN6P_HOPLIMIT) == 0)
						ip6_reset_rcvopt(rcvopts, IPV6_RECVHOPLIMIT);
					break;

				case IPV6_RECVHOPOPTS:
					/* cannot mix with RFC2292 */
					if (OPTBIT(IN6P_RFC2292)) {
						error = EINVAL;
						break;
					}
					OPTSET(IN6P_HOPOPTS);
					if (OPTBIT(IN6P_HOPOPTS) == 0)
						ip6_reset_rcvopt(rcvopts, IPV6_RECVHOPOPTS);
					break;

				case IPV6_RECVDSTOPTS:
					/* cannot mix with RFC2292 */
					if (OPTBIT(IN6P_RFC2292)) {
						error = EINVAL;
						break;
					}
					OPTSET(IN6P_DSTOPTS);
					if (OPTBIT(IN6P_DSTOPTS) == 0)
						ip6_reset_rcvopt(rcvopts, IPV6_RECVDSTOPTS);
					break;

				case IPV6_RECVRTHDRDSTOPTS:
					/* cannot mix with RFC2292 */
					if (OPTBIT(IN6P_RFC2292)) {
						error = EINVAL;
						break;
					}
					OPTSET(IN6P_RTHDRDSTOPTS);
					if (OPTBIT(IN6P_RTHDRDSTOPTS) == 0)
						ip6_reset_rcvopt(rcvopts, IPV6_RECVRTHDRDSTOPTS);
					break;

				case IPV6_RECVRTHDR:
					/* cannot mix with RFC2292 */
					if (OPTBIT(IN6P_RFC2292)) {
						error = EINVAL;
						break;
					}
					OPTSET(IN6P_RTHDR);
					if (OPTBIT(IN6P_RTHDR) == 0)
						ip6_reset_rcvopt(rcvopts, IPV6_RECVRTHDR);
					break;

				case IPV6_FAITH:
					OPTSET(IN6P_FAITH);
					break;

				case IPV6_USE_MIN_MTU:
					OPTSET(IN6P_MINMTU);
					break;

				case IPV6_RECVPATHMTU:
					OPTSET(IN6P_MTU);
					break;

				case IPV6_V6ONLY:
#if (defined(__FreeBSD__) && __FreeBSD__ >= 3) || (defined(__NetBSD__) && !defined(INET6_BINDV6ONLY))
					OPTSET(IN6P_BINDV6ONLY);
#else
					if (ip6_v6only == optval)
						error = 0;
					else
						error = EINVAL;
#endif
					break;
				case IPV6_RECVTCLASS:
					/* cannot mix with RFC2292 XXX */
					if (OPTBIT(IN6P_RFC2292)) {
						error = EINVAL;
						break;
					}
					OPTSET(IN6P_TCLASS);
					break;
				case IPV6_AUTOFLOWLABEL:
					OPTSET(IN6P_AUTOFLOWLABEL);
					break;
				}
				break;

			case IPV6_2292PKTINFO:
			case IPV6_2292HOPLIMIT:
			case IPV6_2292HOPOPTS:
			case IPV6_2292DSTOPTS:
			case IPV6_2292RTHDR:
				/* RFC 2292 */
				if (optlen != sizeof(int)) {
					error = EINVAL;
					break;
				}
#if defined(__FreeBSD__) && __FreeBSD__ >= 3
				error = sooptcopyin(sopt, &optval,
					sizeof optval, sizeof optval);
				if (error)
					break;
#else
				optval = *mtod(m, int *);
#endif
				switch (optname) {
				case IPV6_2292PKTINFO:
					OPTSET2292(IN6P_PKTINFO);
					if (OPTBIT(IN6P_PKTINFO) == 0)
						ip6_reset_rcvopt(rcvopts, IPV6_RECVPKTINFO);
					break;
				case IPV6_2292HOPLIMIT:
					OPTSET2292(IN6P_HOPLIMIT);
					if (OPTBIT(IN6P_HOPLIMIT) == 0)
						ip6_reset_rcvopt(rcvopts, IPV6_RECVHOPLIMIT);
					break;
				case IPV6_2292HOPOPTS:
					/*
					 * Check super-user privilege.
					 * See comments for IPV6_RECVHOPOPTS.
					 */
					if (!privileged)
						return(EPERM);
					OPTSET2292(IN6P_HOPOPTS);
					if (OPTBIT(IN6P_HOPOPTS) == 0)
						ip6_reset_rcvopt(rcvopts, IPV6_RECVHOPOPTS);
					break;
				case IPV6_2292DSTOPTS:
					if (!privileged)
						return(EPERM);
					OPTSET2292(IN6P_DSTOPTS|IN6P_RTHDRDSTOPTS); /* XXX */
					if (OPTBIT(IN6P_DSTOPTS) == 0) {
						ip6_reset_rcvopt(rcvopts, IPV6_RECVDSTOPTS);
						ip6_reset_rcvopt(rcvopts, IPV6_RECVRTHDRDSTOPTS);
					}
					break;
				case IPV6_2292RTHDR:
					OPTSET2292(IN6P_RTHDR);
					if (OPTBIT(IN6P_RTHDR) == 0)
						ip6_reset_rcvopt(rcvopts, IPV6_RECVRTHDR);
					break;
				}
				break;
			case IPV6_PKTINFO:
			case IPV6_HOPOPTS:
			case IPV6_RTHDR:
			case IPV6_DSTOPTS:
			case IPV6_RTHDRDSTOPTS:
			{
				/* new advanced API (2292bis) */
				u_char *optbuf;
				int optlen;
				struct ip6_pktopts **optp;

				/* cannot mix with RFC2292 */
				if (OPTBIT(IN6P_RFC2292)) {
					error = EINVAL;
					break;
				}

#if defined(__FreeBSD__) && __FreeBSD__ >= 3
				optbuf = sopt->sopt_val;
				optlen = sopt->sopt_valsize;
#else  /* !fbsd3 */
				if (m && m->m_next) {
					error = EINVAL;	/* XXX */
					break;
				}
				if (m) {
					optbuf = mtod(m, u_char *);
					optlen = m->m_len;
				} else {
					optbuf = NULL;
					optlen = 0;
				}
#endif

#ifdef HAVE_NRL_INPCB
				optp = &inp->inp_outputopts6;
#else
				optp = &in6p->in6p_outputopts;
#endif

				error = ip6_pcbopt(optname,
						   optbuf, optlen,
						   optp, privileged);
				break;
			}
#undef OPTSET

			case IPV6_MULTICAST_IF:
			case IPV6_MULTICAST_HOPS:
			case IPV6_MULTICAST_LOOP:
			case IPV6_JOIN_GROUP:
			case IPV6_LEAVE_GROUP:
#if defined(__FreeBSD__) && __FreeBSD__ >= 3
			    {
				struct mbuf *m;
				if (sopt->sopt_valsize > MLEN) {
					error = EMSGSIZE;
					break;
				}
				/* XXX */
				MGET(m, sopt->sopt_p ? M_WAIT : M_DONTWAIT, MT_HEADER);
				if (m == 0) {
					error = ENOBUFS;
					break;
				}
				m->m_len = sopt->sopt_valsize;
				error = sooptcopyin(sopt, mtod(m, char *),
						    m->m_len, m->m_len);
				error =	ip6_setmoptions(sopt->sopt_name,
							&in6p->in6p_moptions,
							m);
				(void)m_free(m);
			    }
#else
#ifdef HAVE_NRL_INPCB
				error =	ip6_setmoptions(optname,
					&inp->inp_moptions6, m);
#if defined(__bsdi__) && _BSDI_VERSION >= 199802
				if (inp->inp_moptions6 != NULL)
					inp->inp_flags |= INP_IPV6_MCAST; /* XXX */
#endif
#else
				error =	ip6_setmoptions(optname,
					&in6p->in6p_moptions, m);
#endif
#endif
				break;

#ifndef __bsdi__
			case IPV6_PORTRANGE:
#if defined(__FreeBSD__) && __FreeBSD__ >= 3
				error = sooptcopyin(sopt, &optval,
				    sizeof optval, sizeof optval);
				if (error)
					break;
#else
				optval = *mtod(m, int *);
#endif

#ifdef HAVE_NRL_INPCB
# define in6p		inp
# define in6p_flags	inp_flags
#endif
				switch (optval) {
				case IPV6_PORTRANGE_DEFAULT:
					in6p->in6p_flags &= ~(IN6P_LOWPORT);
					in6p->in6p_flags &= ~(IN6P_HIGHPORT);
					break;

				case IPV6_PORTRANGE_HIGH:
					in6p->in6p_flags &= ~(IN6P_LOWPORT);
					in6p->in6p_flags |= IN6P_HIGHPORT;
					break;

				case IPV6_PORTRANGE_LOW:
					in6p->in6p_flags &= ~(IN6P_HIGHPORT);
					in6p->in6p_flags |= IN6P_LOWPORT;
					break;

				default:
					error = EINVAL;
					break;
				}
#ifdef HAVE_NRL_INPCB
# undef in6p
# undef in6p_flags
#endif
				break;
#endif

#ifdef __OpenBSD__
			case IPSEC6_OUTSA:
#ifndef IPSEC
				error = EINVAL;
#else
				s = spltdb();
				if (m == 0 || m->m_len != sizeof(struct tdb_ident)) {
					error = EINVAL;
				} else {
					tdbip = mtod(m, struct tdb_ident *);
					tdb = gettdb(tdbip->spi, &tdbip->dst,
					    tdbip->proto);
					if (tdb == NULL)
						error = ESRCH;
					else
						tdb_add_inp(tdb, inp);
				}
				splx(s);
#endif
				break;

			case IPV6_AUTH_LEVEL:
			case IPV6_ESP_TRANS_LEVEL:
			case IPV6_ESP_NETWORK_LEVEL:
#ifndef IPSEC
				error = EINVAL;
#else
				if (m == 0 || m->m_len != sizeof(int)) {
					error = EINVAL;
					break;
				}
				optval = *mtod(m, int *);

				if (optval < IPSEC_LEVEL_BYPASS || 
				    optval > IPSEC_LEVEL_UNIQUE) {
					error = EINVAL;
					break;
				}
					
				switch (optname) {
				case IP_AUTH_LEVEL:
				        if (optval < ipsec_auth_default_level &&
					    suser(p->p_ucred, &p->p_acflag)) {
						error = EACCES;
						break;
					}
					inp->inp_seclevel[SL_AUTH] = optval;
					break;

				case IP_ESP_TRANS_LEVEL:
				        if (optval < ipsec_esp_trans_default_level &&
					    suser(p->p_ucred, &p->p_acflag)) {
						error = EACCES;
						break;
					}
					inp->inp_seclevel[SL_ESP_TRANS] = optval;
					break;

				case IP_ESP_NETWORK_LEVEL:
				        if (optval < ipsec_esp_network_default_level &&
					    suser(p->p_ucred, &p->p_acflag)) {
						error = EACCES;
						break;
					}
					inp->inp_seclevel[SL_ESP_NETWORK] = optval;
					break;
				}
				if (!error)
					inp->inp_secrequire = get_sa_require(inp);
#endif
				break;
#endif /*OpenBSD*/

#if defined(IPSEC) && !defined(__OpenBSD__)
			case IPV6_IPSEC_POLICY:
			    {
				caddr_t req = NULL;
				size_t len = 0;
#if defined(__FreeBSD__) && __FreeBSD__ >= 3
				struct mbuf *m;
#endif

#if defined(__FreeBSD__) && __FreeBSD__ >= 3
				if ((error = soopt_getm(sopt, &m)) != 0) /* XXX */
					break;
				if ((error = soopt_mcopyin(sopt, m)) != 0) /* XXX */
					break;
#endif
				if (m) {
					req = mtod(m, caddr_t);
					len = m->m_len;
				}
#ifdef HAVE_NRL_INPCB
				error = ipsec6_set_policy(inp, optname, req,
							  len, privileged);
#else
				error = ipsec6_set_policy(in6p, optname, req,
				                          len, privileged);
#endif
#if defined(__FreeBSD__) && __FreeBSD__ >= 3
				m_freem(m);
#endif
			    }
				break;
#endif /* KAME IPSEC */

#if defined(IPV6FIREWALL) || (defined(__FreeBSD__) && __FreeBSD__ >= 4)
			case IPV6_FW_ADD:
			case IPV6_FW_DEL:
			case IPV6_FW_FLUSH:
			case IPV6_FW_ZERO:
			    {
#if defined(__FreeBSD__) && __FreeBSD__ >= 3
				struct mbuf *m;
				struct mbuf **mp = &m;
#endif

#if defined(__FreeBSD__) && __FreeBSD__ >= 3
				if (ip6_fw_ctl_ptr == NULL)
					return EINVAL;
				if (error = soopt_getm(sopt, &m)) /* XXX */
					break;
				if (error = soopt_mcopyin(sopt, m)) /* XXX */
					break;
#else
				if (ip6_fw_ctl_ptr == NULL) {
					if (m) (void)m_free(m);
					return EINVAL;
				}
#endif
				error = (*ip6_fw_ctl_ptr)(optname, mp);
				m = *mp;
			    }
				break;
#endif

			default:
				error = ENOPROTOOPT;
				break;
			}
#if !(defined(__FreeBSD__) && __FreeBSD__ >= 3)
			if (m)
				(void)m_free(m);
#endif
			break;

#if defined(__FreeBSD__) && __FreeBSD__ >= 3
		case SOPT_GET:
#else
		case PRCO_GETOPT:
#endif
			switch (optname) {

			case IPV6_2292PKTOPTIONS:
#ifdef IPV6_PKTOPTIONS
			case IPV6_PKTOPTIONS:
#endif
#if defined(__FreeBSD__) && __FreeBSD__ >= 3
				if (in6p->in6p_inputopts &&
				    in6p->in6p_inputopts->head) {
					struct mbuf *m;
					m = m_copym(in6p->in6p_inputopts->head,
					    0, M_COPYALL, M_WAIT);
					error = soopt_mcopyout(sopt, m);
					if (error == 0)
						m_freem(m);
				} else
					sopt->sopt_valsize = 0;
#elif defined(HAVE_NRL_INPCB)
				if (inp->inp_options) {
					*mp = m_copym(inp->inp_options, 0,
						      M_COPYALL, M_WAIT);
				} else {
					*mp = m_get(M_WAIT, MT_SOOPTS);
					(*mp)->m_len = 0;
				}
#else
				if (in6p->in6p_inputopts &&
				    in6p->in6p_inputopts->head) {
					*mp = m_copym(in6p->in6p_inputopts->head,
						      0, M_COPYALL, M_WAIT);
				} else {
					*mp = m_get(M_WAIT, MT_SOOPTS);
					(*mp)->m_len = 0;
				}
#endif
				break;

			case IPV6_RECVHOPOPTS:
			case IPV6_RECVDSTOPTS:
			case IPV6_RECVRTHDRDSTOPTS:
				if (!privileged) {
					error = EPERM;
					break;
				}
				/* fall through */
			case IPV6_UNICAST_HOPS:

			case IPV6_RECVPKTINFO:
			case IPV6_RECVHOPLIMIT:
			case IPV6_RECVRTHDR:
			case IPV6_USE_MIN_MTU:
			case IPV6_RECVPATHMTU:

			case IPV6_FAITH:
			case IPV6_V6ONLY:
#ifndef __bsdi__
			case IPV6_PORTRANGE:
#endif
			case IPV6_RECVTCLASS:
			case IPV6_AUTOFLOWLABEL:
				switch (optname) {

				case IPV6_UNICAST_HOPS:
#ifdef HAVE_NRL_INPCB
					optval = inp->inp_hops;
#else
					optval = in6p->in6p_hops;
#endif
					break;

				case IPV6_RECVPKTINFO:
					optval = OPTBIT(IN6P_PKTINFO);
					break;

				case IPV6_RECVHOPLIMIT:
					optval = OPTBIT(IN6P_HOPLIMIT);
					break;

				case IPV6_RECVHOPOPTS:
					optval = OPTBIT(IN6P_HOPOPTS);
					break;

				case IPV6_RECVDSTOPTS:
					optval = OPTBIT(IN6P_DSTOPTS);
					break;

				case IPV6_RECVRTHDRDSTOPTS:
					optval = OPTBIT(IN6P_RTHDRDSTOPTS);
					break;

				case IPV6_USE_MIN_MTU:
					optval = OPTBIT(IN6P_MINMTU);
					break;

				case IPV6_RECVPATHMTU:
					optval = OPTBIT(IN6P_MTU);
					break;

				case IPV6_FAITH:
					optval = OPTBIT(IN6P_FAITH);
					break;

				case IPV6_V6ONLY:
#if (defined(__FreeBSD__) && __FreeBSD__ >= 3) || (defined(__NetBSD__) && !defined(INET6_BINDV6ONLY))
					optval = OPTBIT(IN6P_BINDV6ONLY);
#else
					optval = ip6_v6only;	/*XXX*/
#endif
					break;

#ifndef __bsdi__
				case IPV6_PORTRANGE:
				    {
					int flags;
#ifdef HAVE_NRL_INPCB
					flags = inp->inp_flags;
#else
					flags = in6p->in6p_flags;
#endif
					if (flags & IN6P_HIGHPORT)
						optval = IPV6_PORTRANGE_HIGH;
					else if (flags & IN6P_LOWPORT)
						optval = IPV6_PORTRANGE_LOW;
					else
						optval = 0;
					break;
				    }
#endif
				case IPV6_RECVTCLASS:
					optval = OPTBIT(IN6P_TCLASS);
					break;
				case IPV6_AUTOFLOWLABEL:
					optval = OPTBIT(IN6P_AUTOFLOWLABEL);
					break;
				}
#if defined(__FreeBSD__) && __FreeBSD__ >= 3
				error = sooptcopyout(sopt, &optval,
					sizeof optval);
#else
				*mp = m = m_get(M_WAIT, MT_SOOPTS);
				m->m_len = sizeof(int);
				*mtod(m, int *) = optval;
#endif
				break;

			case IPV6_2292PKTINFO:
			case IPV6_2292HOPLIMIT:
			case IPV6_2292HOPOPTS:
			case IPV6_2292RTHDR:
			case IPV6_2292DSTOPTS:
				if (optname == IPV6_2292HOPOPTS ||
				    optname == IPV6_2292DSTOPTS ||
				    !privileged)
					return(EPERM);
				switch (optname) {
				case IPV6_2292PKTINFO:
					optval = OPTBIT(IN6P_PKTINFO);
					break;
				case IPV6_2292HOPLIMIT:
					optval = OPTBIT(IN6P_HOPLIMIT);
					break;
				case IPV6_2292HOPOPTS:
					if (!privileged)
						return(EPERM);
					optval = OPTBIT(IN6P_HOPOPTS);
					break;
				case IPV6_2292RTHDR:
					optval = OPTBIT(IN6P_RTHDR);
					break;
				case IPV6_2292DSTOPTS:
					if (!privileged)
						return(EPERM);
					optval = OPTBIT(IN6P_DSTOPTS|IN6P_RTHDRDSTOPTS);
					break;
				}
#if defined(__FreeBSD__) && __FreeBSD__ >= 3
				error = sooptcopyout(sopt, &optval,
					sizeof optval);
#else
				*mp = m = m_get(M_WAIT, MT_SOOPTS);
				m->m_len = sizeof(int);
				*mtod(m, int *) = optval;
#endif /* FreeBSD3 */
				break;
			case IPV6_PKTINFO:
			case IPV6_HOPOPTS:
			case IPV6_RTHDR:
			case IPV6_DSTOPTS:
			case IPV6_RTHDRDSTOPTS:
#ifdef HAVE_NRL_INPCB
#define in6p inp
#define in6p_outputopts inp_outputopts6
#endif
				error = ip6_getpcbopt(in6p->in6p_outputopts,
						      optname, &optdata,
						      &optdatalen);
				if (error == 0) {
#if defined(__FreeBSD__) && __FreeBSD__ >= 3
					/* note that optdatalen maybe 0 */
					error = sooptcopyout(sopt, optdata,
							     optdatalen);
#else  /* !FreeBSD3 */
					if (optdatalen > MCLBYTES)
						return(EMSGSIZE); /* XXX */
					*mp = m = m_get(M_WAIT, MT_SOOPTS);
					if (optdatalen > MLEN)
						MCLGET(m, M_WAIT);
					m->m_len = optdatalen;
					bcopy(optdata, mtod(m, void *),
					      optdatalen);
#endif /* FreeBSD3 */
				}
#ifdef HAVE_NRL_INPCB
#undef in6p
#undef in6p_outputopts
#endif
				break;

			case IPV6_MULTICAST_IF:
			case IPV6_MULTICAST_HOPS:
			case IPV6_MULTICAST_LOOP:
			case IPV6_JOIN_GROUP:
			case IPV6_LEAVE_GROUP:
#if defined(__FreeBSD__) && __FreeBSD__ >= 3
			    {
				struct mbuf *m;
				error = ip6_getmoptions(sopt->sopt_name,
						in6p->in6p_moptions, &m);
				if (error == 0)
					error = sooptcopyout(sopt,
						mtod(m, char *), m->m_len);
				m_freem(m);
			    }
#elif defined(HAVE_NRL_INPCB)
				error = ip6_getmoptions(optname, inp->inp_moptions6, mp);
#else
				error = ip6_getmoptions(optname, in6p->in6p_moptions, mp);
#endif
				break;

#ifdef __OpenBSD__
			case IPSEC6_OUTSA:
#ifndef IPSEC
				error = EINVAL;
#else
				s = spltdb();
				if (inp->inp_tdb == NULL) {
					error = ENOENT;
				} else {
					tdbi.spi = inp->inp_tdb->tdb_spi;
					tdbi.dst = inp->inp_tdb->tdb_dst;
					tdbi.proto = inp->inp_tdb->tdb_sproto;
					*mp = m = m_get(M_WAIT, MT_SOOPTS);
					m->m_len = sizeof(tdbi);
					bcopy((caddr_t)&tdbi, mtod(m, caddr_t),
					    (unsigned)m->m_len);
				}
				splx(s);
#endif
				break;

			case IPV6_AUTH_LEVEL:
			case IPV6_ESP_TRANS_LEVEL:
			case IPV6_ESP_NETWORK_LEVEL:
#ifndef IPSEC
				m->m_len = sizeof(int);
				*mtod(m, int *) = IPSEC_LEVEL_NONE;
#else
				m->m_len = sizeof(int);
				switch (optname) {
				case IP_AUTH_LEVEL:
					optval = inp->inp_seclevel[SL_AUTH];
					break;

				case IP_ESP_TRANS_LEVEL:
					optval =
					    inp->inp_seclevel[SL_ESP_TRANS];
					break;

				case IP_ESP_NETWORK_LEVEL:
					optval =
					    inp->inp_seclevel[SL_ESP_NETWORK];
					break;
				}
				*mtod(m, int *) = optval;
#endif
				break;
#endif /*OpenBSD*/

#if defined(IPSEC) && !defined(__OpenBSD__)
			case IPV6_IPSEC_POLICY:
			  {
				caddr_t req = NULL;
				size_t len = 0;
#if defined(__FreeBSD__) && __FreeBSD__ >= 3
				struct mbuf *m = NULL;
				struct mbuf **mp = &m;

				error = soopt_getm(sopt, &m); /* XXX */
				if (error != NULL)
					break;
				error = soopt_mcopyin(sopt, m); /* XXX */
				if (error != NULL)
					break;
#endif
				if (m) {
					req = mtod(m, caddr_t);
					len = m->m_len;
				}
#ifdef HAVE_NRL_INPCB
				error = ipsec6_get_policy(inp, req, len, mp);
#else
				error = ipsec6_get_policy(in6p, req, len, mp);
#endif
#if defined(__FreeBSD__) && __FreeBSD__ >= 3
				if (error == 0)
					error = soopt_mcopyout(sopt, m); /*XXX*/
				if (error == 0 && m)
					m_freem(m);
#endif
				break;
			  }
#endif /* KAME IPSEC */

#if defined(IPV6FIREWALL) || (defined(__FreeBSD__) && __FreeBSD__ >= 4)
			case IPV6_FW_GET:
			  {
#if defined(__FreeBSD__) && __FreeBSD__ >= 3
				struct mbuf *m;
				struct mbuf **mp = &m;
#endif

				if (ip6_fw_ctl_ptr == NULL)
			        {
#if !(defined(__FreeBSD__) && __FreeBSD__ >= 3)
					if (m)
						(void)m_free(m);
#endif
					return EINVAL;
				}
				error = (*ip6_fw_ctl_ptr)(optname, mp);
#if defined(__FreeBSD__) && __FreeBSD__ >= 3
				if (error == 0)
					error = soopt_mcopyout(sopt, m); /* XXX */
				if (error == 0 && m)
					m_freem(m);
#endif
			  }
				break;
#endif

			default:
				error = ENOPROTOOPT;
				break;
			}
			break;
		}
	} else {
		error = EINVAL;
#if !(defined(__FreeBSD__) && __FreeBSD__ >= 3)
		if (op == PRCO_SETOPT && *mp)
			(void)m_free(*mp);
#endif
	}
	return(error);
}

/*
 * Set up IP6 options in pcb for insertion in output packets or
 * specifying behavior of outgoing packets.
 */
static int
#if defined(__FreeBSD__) && __FreeBSD__ >= 3
ip6_pcbopts(pktopt, m, so, sopt)
#else
ip6_pcbopts(pktopt, m, so)
#endif
	struct ip6_pktopts **pktopt;
	struct mbuf *m;
	struct socket *so;
#if defined(__FreeBSD__) && __FreeBSD__ >= 3
	struct sockopt *sopt;
#endif
{
	struct ip6_pktopts *opt = *pktopt;
	int error = 0;
#if defined(__FreeBSD__) && __FreeBSD__ >= 3
	struct proc *p = sopt->sopt_p;
#elif defined(__bsdi__) && _BSDI_VERSION >= 199802
	struct proc *p = PCPU(curproc);	/* XXX */
#else
	struct proc *p = curproc;	/* XXX */
#endif
	int priv = 0;

	/* turn off any old options. */
	if (opt) {
#ifdef DIAGNOSTIC
	    if (opt->ip6po_pktinfo || opt->ip6po_nexthop ||
		opt->ip6po_hbh || opt->ip6po_dest1 || opt->ip6po_dest2 ||
		opt->ip6po_rhinfo.ip6po_rhi_rthdr)
		    printf("ip6_pcbopts: all specified options are cleared.\n");
#endif
		ip6_clearpktopts(opt, 1, -1);
	} else
		opt = malloc(sizeof(*opt), M_IP6OPT, M_WAITOK);
	*pktopt = NULL;

	if (!m || m->m_len == 0) {
		/*
		 * Only turning off any previous options.
		 */
		if (opt)
			free(opt, M_IP6OPT);
		return(0);
	}

	/*  set options specified by user. */
#if defined(__FreeBSD__) && __FreeBSD__ >= 4
	if (p && !suser(p))
		priv = 1;
#else
	if (p && !suser(p->p_ucred, &p->p_acflag))
		priv = 1;
#endif
	if ((error = ip6_setpktoptions(m, opt, priv, 1)) != 0) {
		ip6_clearpktopts(opt, 1, -1); /* XXX: discard all options */
		return(error);
	}
	*pktopt = opt;
	return(0);
}

/*
 * initialize ip6_pktopts.  beware that there are non-zero default values in
 * the struct.
 */
void
init_ip6pktopts(opt)
	struct ip6_pktopts *opt;
{

	bzero(opt, sizeof(*opt));
	opt->ip6po_hlim = -1;	/* -1 means default hop limit */
	opt->ip6po_mtu = -1;	/* -1 means not to specify the MTU */
	opt->ip6po_tclass = 0x00;
}

static int
ip6_pcbopt(optname, buf, len, pktopt, priv)
	int optname, len, priv;
	u_char *buf;
	struct ip6_pktopts **pktopt;
{
	struct ip6_pktopts *opt;
	struct in6_pktinfo *pktinfo;
	
	if (*pktopt == NULL) {
		*pktopt = malloc(sizeof(struct ip6_pktopts), M_IP6OPT,
				 M_WAITOK);
		init_ip6pktopts(*pktopt);
		(*pktopt)->needfree = 1;
	}
	opt = *pktopt;

	switch (optname) {
	case IPV6_PKTINFO:
		if (len == 0) {	/* just remove the option */
			ip6_clearpktopts(opt, 1, IPV6_PKTINFO);
			break;
		}
		
		if (len != sizeof(struct in6_pktinfo))
			return EINVAL;
		pktinfo = (struct in6_pktinfo *)buf;

		/*
		 * An application can clear any sticky IPV6_PKTINFO option by
		 * doing a "regular" setsockopt with ipi6_addr being
		 * in6addr_any and ipi6_ifindex being zero.
		 * [rfc2292bis-01, Section 6]
		 * XXX: Is this a good feature?? (jinmei@kame.net)
		 */
		if (pktinfo->ipi6_ifindex == 0 &&
		    IN6_IS_ADDR_UNSPECIFIED(&pktinfo->ipi6_addr)) {
			ip6_clearpktopts(opt, 1, IPV6_PKTINFO);
			break;
		}

		/* XXX: this overrides the original data space */
		if (pktinfo->ipi6_ifindex &&
		    IN6_IS_ADDR_LINKLOCAL(&pktinfo->ipi6_addr))
			pktinfo->ipi6_addr.s6_addr16[1] =
				htons(pktinfo->ipi6_ifindex);

		if (pktinfo->ipi6_ifindex > if_index ||
		    pktinfo->ipi6_ifindex < 0)
			return(ENXIO);

		/*
		 * Check if the requested source address is indeed a unicast
		 * address assigned to the node.
		 */
		if (!IN6_IS_ADDR_UNSPECIFIED(&pktinfo->ipi6_addr)) {
			struct ifaddr *ia;
			struct sockaddr_in6 sin6;

			bzero(&sin6, sizeof(sin6));
			sin6.sin6_len = sizeof(sin6);
			sin6.sin6_family = AF_INET6;
			sin6.sin6_addr = pktinfo->ipi6_addr;
			ia = ifa_ifwithaddr(sin6tosa(&sin6));
			if (ia == NULL)
				return(EADDRNOTAVAIL);
		}

		if (opt->ip6po_pktinfo == NULL)
			opt->ip6po_pktinfo = malloc(sizeof(struct in6_pktinfo),
						    M_IP6OPT, M_WAITOK);
		bcopy(pktinfo, opt->ip6po_pktinfo, sizeof(*pktinfo));
		
		break;
	case IPV6_HOPLIMIT:
	{
		int *hlimp;

		if (len != sizeof(int))
			return(EINVAL);
		hlimp = (int *)buf;
		if (*hlimp < -1 || *hlimp > 255)
			return(EINVAL);

		opt->ip6po_hlim = *hlimp;
		break;
	}
	case IPV6_TCLASS:
	{
		if (len != sizeof(u_int8_t))
			return(EINVAL);
		opt->ip6po_tclass = *(u_int8_t *)buf;
		break;
	}
	case IPV6_NEXTHOP:
		if (!priv)
			return(EPERM);

		if (len == 0) {	/* just remove the option */
			ip6_clearpktopts(opt, 1, IPV6_NEXTHOP);
			break;
		}

		/* check if cmsg_len is large enough for sa_len */
		if (len < sizeof(u_char) ||
		    len < *buf)
			return(EINVAL);

		/* turn off the previous option */
		ip6_clearpktopts(opt, 1, IPV6_NEXTHOP);

		opt->ip6po_nexthop = malloc(*buf, M_IP6OPT, M_WAITOK);
		bcopy(buf, opt->ip6po_nexthop, *buf);
		break;
	case IPV6_HOPOPTS:
	{
		struct ip6_hbh *hbh;
		int hbhlen;

		/*
		 * XXX: We don't allow a non-privileged user to set ANY HbH
		 * options, since per-option restriction has too much
		 * overhead.
		 */
		if (!priv)
			return(EPERM);
		
		if (len == 0) {
			ip6_clearpktopts(opt, 1, IPV6_HOPOPTS);
			break;	/* just remove the option */
		}

		if (len < sizeof(struct ip6_hbh))
			return(EINVAL);
		hbh = (struct ip6_hbh *)buf;
		hbhlen = (hbh->ip6h_len + 1) << 3;
		if (len != hbhlen)
			return(EINVAL);

		/* turn off the previous option */
		ip6_clearpktopts(opt, 1, IPV6_HOPOPTS);
		
		opt->ip6po_hbh = malloc(hbhlen, M_IP6OPT, M_WAITOK);
		bcopy(buf, opt->ip6po_hbh, hbhlen);

		break;
	}
	case IPV6_DSTOPTS:
	case IPV6_RTHDRDSTOPTS:
	{
		struct ip6_dest *dest, *newdest;
		int destlen;

		if (!priv)	/* XXX: see the comment for IPV6_HOPOPTS */
			return(EPERM);

		if (len == 0) {
			ip6_clearpktopts(opt, 1, optname);
			break;	/* just remove the option */
		}

		if (len < sizeof(struct ip6_dest))
			return(EINVAL);
		dest = (struct ip6_dest *)buf;
		destlen = (dest->ip6d_len + 1) << 3;
		if (len != destlen)
			return(EINVAL);

		/* turn off the previous option */
		ip6_clearpktopts(opt, 1, optname);
		
		newdest = malloc(destlen, M_IP6OPT, M_WAITOK);
		bcopy(buf, newdest, destlen);

		if (optname == IPV6_DSTOPTS)
			opt->ip6po_dest2 = newdest;
		else
			opt->ip6po_dest1 = newdest;

		break;
	}
	case IPV6_RTHDR:
	{
		struct ip6_rthdr *rth;
		int rthlen;

		if (len == 0) {
			ip6_clearpktopts(opt, 1, IPV6_RTHDR);
			break;	/* just remove the option */
		}

		if (len < sizeof(struct ip6_rthdr))
			return(EINVAL);
		rth = (struct ip6_rthdr *)buf;
		rthlen = (rth->ip6r_len + 1) << 3;
		if (len != rthlen)
			return(EINVAL);

		switch (rth->ip6r_type) {
		case IPV6_RTHDR_TYPE_0:
			if (rth->ip6r_len == 0)	/* must contain one addr */
				return(EINVAL);
			if (rth->ip6r_len % 2) /* length must be even */
				return(EINVAL);
			if (rth->ip6r_len / 2 != rth->ip6r_segleft)
				return(EINVAL);
			break;
		default:
			return(EINVAL);	/* not supported */
		}

		/* turn off the previous option */
		ip6_clearpktopts(opt, 1, IPV6_RTHDR);

		opt->ip6po_rthdr = malloc(rthlen, M_IP6OPT, M_WAITOK);
		bcopy(buf, opt->ip6po_rthdr, rthlen);
		
		break;
	}
	default:
		return(ENOPROTOOPT);	
	} /* end of switch */

	return(0);
}

static int
ip6_getpcbopt(pktopt, optname, datap, datalenp)
	struct ip6_pktopts *pktopt;
	int optname, *datalenp;
	void **datap;
{
	void *optdata = NULL;
	struct ip6_ext *ip6e;
	int optdatalen = 0;

	if (pktopt == NULL)
		goto end;

	switch (optname) {
	case IPV6_PKTINFO:
		if (pktopt->ip6po_pktinfo) {
			optdata = (void *)pktopt->ip6po_pktinfo;
			optdatalen = sizeof(struct in6_pktinfo);
		}
		break;
	case IPV6_HOPLIMIT:
		optdata = (void *)&pktopt->ip6po_hlim;
		optdatalen = sizeof(int);
		break;
	case IPV6_TCLASS:
		optdata = (void *)&pktopt->ip6po_tclass;
		optdatalen = sizeof(pktopt->ip6po_tclass);
	case IPV6_HOPOPTS:
		if (pktopt->ip6po_hbh) {
			optdata = (void *)pktopt->ip6po_hbh;
			ip6e = (struct ip6_ext *)pktopt->ip6po_hbh;
			optdatalen = (ip6e->ip6e_len + 1) << 3;
		}
		break;
	case IPV6_RTHDR:
		if (pktopt->ip6po_rthdr) {
			optdata = (void *)pktopt->ip6po_rthdr;
			ip6e = (struct ip6_ext *)pktopt->ip6po_rthdr;
			optdatalen = (ip6e->ip6e_len + 1) << 3;
		}
		break;
	case IPV6_RTHDRDSTOPTS:
		if (pktopt->ip6po_dest1) {
			optdata = (void *)pktopt->ip6po_dest1;
			ip6e = (struct ip6_ext *)pktopt->ip6po_dest1;
			optdatalen = (ip6e->ip6e_len + 1) << 3;
		}
		break;
	case IPV6_DSTOPTS:
		if (pktopt->ip6po_dest2) {
			optdata = (void *)pktopt->ip6po_dest2;
			ip6e = (struct ip6_ext *)pktopt->ip6po_dest2;
			optdatalen = (ip6e->ip6e_len + 1) << 3;
		}
		break;
	}

  end:
	*datap = optdata;
	*datalenp = optdatalen;

	return(0);
}

void
ip6_clearpktopts(pktopt, needfree, optname)
	struct ip6_pktopts *pktopt;
	int needfree, optname;
{
	if (pktopt == NULL)
		return;

#ifdef DIAGNOSTIC
	if ((needfree && !pktopt->needfree) ||
	    (!needfree && pktopt->needfree)) {
#if 0
		panic("needfree inconsistent");
#else
		printf("needfree inconsistent: %p %d %d\n", pktopt, needfree,
		    pktopt->needfree);
#endif
	}
#endif
	if (optname == -1 || optname == IPV6_PKTINFO) {
		if (needfree && pktopt->ip6po_pktinfo)
			free(pktopt->ip6po_pktinfo, M_IP6OPT);
		pktopt->ip6po_pktinfo = NULL;
	}
	if (optname == -1 || optname == IPV6_HOPLIMIT)
		pktopt->ip6po_hlim = -1;
	if (optname == -1 || optname == IPV6_TCLASS)
		pktopt->ip6po_tclass = 0x00;
	if (optname == -1 || optname == IPV6_NEXTHOP) {
		if (needfree && pktopt->ip6po_nexthop)
			free(pktopt->ip6po_nexthop, M_IP6OPT);
		pktopt->ip6po_nexthop = NULL;
	}
	if (optname == -1 || optname == IPV6_HOPOPTS) {
		if (needfree && pktopt->ip6po_hbh)
			free(pktopt->ip6po_hbh, M_IP6OPT);
		pktopt->ip6po_hbh = NULL;
	}
	if (optname == -1 || optname == IPV6_RTHDRDSTOPTS) {
		if (needfree && pktopt->ip6po_dest1)
			free(pktopt->ip6po_dest1, M_IP6OPT);
		pktopt->ip6po_dest1 = NULL;
	}
	if (optname == -1 || optname == IPV6_RTHDR) {
		if (needfree && pktopt->ip6po_rhinfo.ip6po_rhi_rthdr)
			free(pktopt->ip6po_rhinfo.ip6po_rhi_rthdr, M_IP6OPT);
		pktopt->ip6po_rhinfo.ip6po_rhi_rthdr = NULL;
		if (pktopt->ip6po_route.ro_rt) {
			RTFREE(pktopt->ip6po_route.ro_rt);
			pktopt->ip6po_route.ro_rt = NULL;
		}
	}
	if (optname == -1 || optname == IPV6_DSTOPTS) {
		if (needfree && pktopt->ip6po_dest2)
			free(pktopt->ip6po_dest2, M_IP6OPT);
		pktopt->ip6po_dest2 = NULL;
	}
}

#define PKTOPT_EXTHDRCPY(type) \
do {\
	if (src->type) {\
		int hlen =\
			(((struct ip6_ext *)src->type)->ip6e_len + 1) << 3;\
		dst->type = malloc(hlen, M_IP6OPT, canwait);\
		if (dst->type == NULL && canwait == M_NOWAIT)\
			goto bad;\
		bcopy(src->type, dst->type, hlen);\
	}\
} while (0)

struct ip6_pktopts *
ip6_copypktopts(src, canwait)
	struct ip6_pktopts *src;
	int canwait;
{
	struct ip6_pktopts *dst;

	if (src == NULL) {
		printf("ip6_clearpktopts: invalid argument\n");
		return(NULL);
	}

	dst = malloc(sizeof(*dst), M_IP6OPT, canwait);
	if (dst == NULL && canwait == M_NOWAIT)
		goto bad;
	bzero(dst, sizeof(*dst));
	dst->needfree = 1;

	dst->ip6po_hlim = src->ip6po_hlim;
	dst->ip6po_tclass = src->ip6po_tclass;
	dst->ip6po_flags = src->ip6po_flags;
	if (src->ip6po_pktinfo) {
		dst->ip6po_pktinfo = malloc(sizeof(*dst->ip6po_pktinfo),
					    M_IP6OPT, canwait);
		if (dst->ip6po_pktinfo == NULL && canwait == M_NOWAIT)
			goto bad;
		*dst->ip6po_pktinfo = *src->ip6po_pktinfo;
	}
	if (src->ip6po_nexthop) {
		dst->ip6po_nexthop = malloc(src->ip6po_nexthop->sa_len,
					    M_IP6OPT, canwait);
		if (dst->ip6po_nexthop == NULL && canwait == M_NOWAIT)
			goto bad;
		bcopy(src->ip6po_nexthop, dst->ip6po_nexthop,
		      src->ip6po_nexthop->sa_len);
	}
	PKTOPT_EXTHDRCPY(ip6po_hbh);
	PKTOPT_EXTHDRCPY(ip6po_dest1);
	PKTOPT_EXTHDRCPY(ip6po_dest2);
	PKTOPT_EXTHDRCPY(ip6po_rthdr); /* not copy the cached route */
	return(dst);

  bad:
	printf("ip6_copypktopts: copy failed");
	if (dst->ip6po_pktinfo) free(dst->ip6po_pktinfo, M_IP6OPT);
	if (dst->ip6po_nexthop) free(dst->ip6po_nexthop, M_IP6OPT);
	if (dst->ip6po_hbh) free(dst->ip6po_hbh, M_IP6OPT);
	if (dst->ip6po_dest1) free(dst->ip6po_dest1, M_IP6OPT);
	if (dst->ip6po_dest2) free(dst->ip6po_dest2, M_IP6OPT);
	if (dst->ip6po_rthdr) free(dst->ip6po_rthdr, M_IP6OPT);
	return(NULL);
}
#undef PKTOPT_EXTHDRCPY

void
ip6_freepcbopts(pktopt)
	struct ip6_pktopts *pktopt;
{
	if (pktopt == NULL)
		return;

	ip6_clearpktopts(pktopt, 1, -1);

	free(pktopt, M_IP6OPT);
}

/*
 * Set the IP6 multicast options in response to user setsockopt().
 */
static int
ip6_setmoptions(optname, im6op, m)
	int optname;
	struct ip6_moptions **im6op;
	struct mbuf *m;
{
	int error = 0;
	u_int loop, ifindex;
	struct ipv6_mreq *mreq;
	struct ifnet *ifp;
	struct ip6_moptions *im6o = *im6op;
#ifdef NEW_STRUCT_ROUTE
	struct route ro;
#else
	struct route_in6 ro;
#endif
	struct sockaddr_in6 *dst;
	struct in6_multi_mship *imm;
#if defined(__bsdi__) && _BSDI_VERSION >= 199802
	struct proc *p = PCPU(curproc);	/* XXX */
#else
	struct proc *p = curproc;	/* XXX */
#endif
#if defined(__bsdi__) && _BSDI_VERSION < 199802
	struct ifnet *loifp = &loif;
#endif

	if (im6o == NULL) {
		/*
		 * No multicast option buffer attached to the pcb;
		 * allocate one and initialize to default values.
		 */
		im6o = (struct ip6_moptions *)
			malloc(sizeof(*im6o), M_IPMOPTS, M_WAITOK);

		if (im6o == NULL)
			return(ENOBUFS);
		*im6op = im6o;
		im6o->im6o_multicast_ifp = NULL;
		im6o->im6o_multicast_hlim = ip6_defmcasthlim;
		im6o->im6o_multicast_loop = IPV6_DEFAULT_MULTICAST_LOOP;
		LIST_INIT(&im6o->im6o_memberships);
	}

	switch (optname) {

	case IPV6_MULTICAST_IF:
		/*
		 * Select the interface for outgoing multicast packets.
		 */
		if (m == NULL || m->m_len != sizeof(u_int)) {
			error = EINVAL;
			break;
		}
		bcopy(mtod(m, u_int *), &ifindex, sizeof(ifindex));
		if (ifindex < 0 || if_index < ifindex) {
			error = ENXIO;	/* XXX EINVAL? */
			break;
		}
		ifp = ifindex2ifnet[ifindex];
		if (ifp == NULL || (ifp->if_flags & IFF_MULTICAST) == 0) {
			error = EADDRNOTAVAIL;
			break;
		}
		im6o->im6o_multicast_ifp = ifp;
		break;

	case IPV6_MULTICAST_HOPS:
	    {
		/*
		 * Set the IP6 hoplimit for outgoing multicast packets.
		 */
		int optval;
		if (m == NULL || m->m_len != sizeof(int)) {
			error = EINVAL;
			break;
		}
		bcopy(mtod(m, u_int *), &optval, sizeof(optval));
		if (optval < -1 || optval >= 256)
			error = EINVAL;
		else if (optval == -1)
			im6o->im6o_multicast_hlim = ip6_defmcasthlim;
		else
			im6o->im6o_multicast_hlim = optval;
		break;
	    }

	case IPV6_MULTICAST_LOOP:
		/*
		 * Set the loopback flag for outgoing multicast packets.
		 * Must be zero or one.
		 */
		if (m == NULL || m->m_len != sizeof(u_int)) {
			error = EINVAL;
			break;
		}
		bcopy(mtod(m, u_int *), &loop, sizeof(loop));
		if (loop > 1) {
			error = EINVAL;
			break;
		}
		im6o->im6o_multicast_loop = loop;
		break;

	case IPV6_JOIN_GROUP:
		/*
		 * Add a multicast group membership.
		 * Group must be a valid IP6 multicast address.
		 */
		if (m == NULL || m->m_len != sizeof(struct ipv6_mreq)) {
			error = EINVAL;
			break;
		}
		mreq = mtod(m, struct ipv6_mreq *);
		if (IN6_IS_ADDR_UNSPECIFIED(&mreq->ipv6mr_multiaddr)) {
			/*
			 * We use the unspecified address to specify to accept
			 * all multicast addresses. Only super user is allowed
			 * to do this.
			 */
#if defined(__FreeBSD__) && __FreeBSD__ >= 4
			if (suser(p))
#else
			if (suser(p->p_ucred, &p->p_acflag))
#endif
			{
				error = EACCES;
				break;
			}
		} else if (!IN6_IS_ADDR_MULTICAST(&mreq->ipv6mr_multiaddr)) {
			error = EINVAL;
			break;
		}

		/*
		 * If the interface is specified, validate it.
		 */
		if (mreq->ipv6mr_interface < 0
		 || if_index < mreq->ipv6mr_interface) {
			error = ENXIO;	/* XXX EINVAL? */
			break;
		}
		/*
		 * If no interface was explicitly specified, choose an
		 * appropriate one according to the given multicast address.
		 */
		if (mreq->ipv6mr_interface == 0) {
			/*
			 * If the multicast address is in node-local scope,
			 * the interface should be a loopback interface.
			 * Otherwise, look up the routing table for the
			 * address, and choose the outgoing interface.
			 *   XXX: is it a good approach?
			 */
			if (IN6_IS_ADDR_MC_NODELOCAL(&mreq->ipv6mr_multiaddr)) {
#ifdef __bsdi__
				ifp = loifp;
#else
				ifp = &loif[0];
#endif
			} else {
				ro.ro_rt = NULL;
				dst = (struct sockaddr_in6 *)&ro.ro_dst;
				bzero(dst, sizeof(*dst));
				dst->sin6_len = sizeof(struct sockaddr_in6);
				dst->sin6_family = AF_INET6;
				dst->sin6_addr = mreq->ipv6mr_multiaddr;
				rtalloc((struct route *)&ro);
				if (ro.ro_rt == NULL) {
					error = EADDRNOTAVAIL;
					break;
				}
				ifp = ro.ro_rt->rt_ifp;
				rtfree(ro.ro_rt);
			}
		} else
			ifp = ifindex2ifnet[mreq->ipv6mr_interface];

		/*
		 * See if we found an interface, and confirm that it
		 * supports multicast
		 */
		if (ifp == NULL || (ifp->if_flags & IFF_MULTICAST) == 0) {
			error = EADDRNOTAVAIL;
			break;
		}
		/*
		 * Put interface index into the multicast address,
		 * if the address has link-local scope.
		 */
		if (IN6_IS_ADDR_MC_LINKLOCAL(&mreq->ipv6mr_multiaddr)) {
			mreq->ipv6mr_multiaddr.s6_addr16[1]
				= htons(mreq->ipv6mr_interface);
		}
		/*
		 * See if the membership already exists.
		 */
		for (imm = im6o->im6o_memberships.lh_first;
		     imm != NULL; imm = imm->i6mm_chain.le_next)
			if (imm->i6mm_maddr->in6m_ifp == ifp &&
			    IN6_ARE_ADDR_EQUAL(&imm->i6mm_maddr->in6m_addr,
					       &mreq->ipv6mr_multiaddr))
				break;
		if (imm != NULL) {
			error = EADDRINUSE;
			break;
		}
		/*
		 * Everything looks good; add a new record to the multicast
		 * address list for the given interface.
		 */
		imm = malloc(sizeof(*imm), M_IPMADDR, M_WAITOK);
		if (imm == NULL) {
			error = ENOBUFS;
			break;
		}
		if ((imm->i6mm_maddr =
		     in6_addmulti(&mreq->ipv6mr_multiaddr, ifp, &error)) == NULL) {
			free(imm, M_IPMADDR);
			break;
		}
		LIST_INSERT_HEAD(&im6o->im6o_memberships, imm, i6mm_chain);
		break;

	case IPV6_LEAVE_GROUP:
		/*
		 * Drop a multicast group membership.
		 * Group must be a valid IP6 multicast address.
		 */
		if (m == NULL || m->m_len != sizeof(struct ipv6_mreq)) {
			error = EINVAL;
			break;
		}
		mreq = mtod(m, struct ipv6_mreq *);
		if (IN6_IS_ADDR_UNSPECIFIED(&mreq->ipv6mr_multiaddr)) {
#if defined(__FreeBSD__) && __FreeBSD__ >= 4
			if (suser(p))
#else
			if (suser(p->p_ucred, &p->p_acflag))
#endif
			{
				error = EACCES;
				break;
			}
		} else if (!IN6_IS_ADDR_MULTICAST(&mreq->ipv6mr_multiaddr)) {
			error = EINVAL;
			break;
		}
		/*
		 * If an interface address was specified, get a pointer
		 * to its ifnet structure.
		 */
		if (mreq->ipv6mr_interface < 0
		 || if_index < mreq->ipv6mr_interface) {
			error = ENXIO;	/* XXX EINVAL? */
			break;
		}
		ifp = ifindex2ifnet[mreq->ipv6mr_interface];
		/*
		 * Put interface index into the multicast address,
		 * if the address has link-local scope.
		 */
		if (IN6_IS_ADDR_MC_LINKLOCAL(&mreq->ipv6mr_multiaddr)) {
			mreq->ipv6mr_multiaddr.s6_addr16[1]
				= htons(mreq->ipv6mr_interface);
		}
		/*
		 * Find the membership in the membership list.
		 */
		for (imm = im6o->im6o_memberships.lh_first;
		     imm != NULL; imm = imm->i6mm_chain.le_next) {
			if ((ifp == NULL ||
			     imm->i6mm_maddr->in6m_ifp == ifp) &&
			    IN6_ARE_ADDR_EQUAL(&imm->i6mm_maddr->in6m_addr,
					       &mreq->ipv6mr_multiaddr))
				break;
		}
		if (imm == NULL) {
			/* Unable to resolve interface */
			error = EADDRNOTAVAIL;
			break;
		}
		/*
		 * Give up the multicast address record to which the
		 * membership points.
		 */
		LIST_REMOVE(imm, i6mm_chain);
		in6_delmulti(imm->i6mm_maddr);
		free(imm, M_IPMADDR);
		break;

	default:
		error = EOPNOTSUPP;
		break;
	}

	/*
	 * If all options have default values, no need to keep the mbuf.
	 */
	if (im6o->im6o_multicast_ifp == NULL &&
	    im6o->im6o_multicast_hlim == ip6_defmcasthlim &&
	    im6o->im6o_multicast_loop == IPV6_DEFAULT_MULTICAST_LOOP &&
	    im6o->im6o_memberships.lh_first == NULL) {
		free(*im6op, M_IPMOPTS);
		*im6op = NULL;
	}

	return(error);
}

/*
 * Return the IP6 multicast options in response to user getsockopt().
 */
static int
ip6_getmoptions(optname, im6o, mp)
	int optname;
	struct ip6_moptions *im6o;
	struct mbuf **mp;
{
	u_int *hlim, *loop, *ifindex;

#ifdef __FreeBSD__
	*mp = m_get(M_WAIT, MT_HEADER);		/*XXX*/
#else
	*mp = m_get(M_WAIT, MT_SOOPTS);
#endif

	switch (optname) {

	case IPV6_MULTICAST_IF:
		ifindex = mtod(*mp, u_int *);
		(*mp)->m_len = sizeof(u_int);
		if (im6o == NULL || im6o->im6o_multicast_ifp == NULL)
			*ifindex = 0;
		else
			*ifindex = im6o->im6o_multicast_ifp->if_index;
		return(0);

	case IPV6_MULTICAST_HOPS:
		hlim = mtod(*mp, u_int *);
		(*mp)->m_len = sizeof(u_int);
		if (im6o == NULL)
			*hlim = ip6_defmcasthlim;
		else
			*hlim = im6o->im6o_multicast_hlim;
		return(0);

	case IPV6_MULTICAST_LOOP:
		loop = mtod(*mp, u_int *);
		(*mp)->m_len = sizeof(u_int);
		if (im6o == NULL)
			*loop = ip6_defmcasthlim;
		else
			*loop = im6o->im6o_multicast_loop;
		return(0);

	default:
		return(EOPNOTSUPP);
	}
}

/*
 * Discard the IP6 multicast options.
 */
void
ip6_freemoptions(im6o)
	struct ip6_moptions *im6o;
{
	struct in6_multi_mship *imm;

	if (im6o == NULL)
		return;

	while ((imm = im6o->im6o_memberships.lh_first) != NULL) {
		LIST_REMOVE(imm, i6mm_chain);
		if (imm->i6mm_maddr)
			in6_delmulti(imm->i6mm_maddr);
		free(imm, M_IPMADDR);
	}
	free(im6o, M_IPMOPTS);
}

/*
 * Set IPv6 outgoing packet options based on advanced API.
 */
int
ip6_setpktoptions(control, opt, priv, needcopy)
	struct mbuf *control;
	struct ip6_pktopts *opt;
	int priv, needcopy;
{
	struct cmsghdr *cm = 0;

	if (control == 0 || opt == 0)
		return(EINVAL);

	init_ip6pktopts(opt);
	opt->needfree = needcopy;

	/*
	 * XXX: Currently, we assume all the optional information is stored
	 * in a single mbuf.
	 */
	if (control->m_next)
		return(EINVAL);

	for (; control->m_len; control->m_data += CMSG_ALIGN(cm->cmsg_len),
		     control->m_len -= CMSG_ALIGN(cm->cmsg_len)) {
		cm = mtod(control, struct cmsghdr *);
		if (cm->cmsg_len == 0 || cm->cmsg_len > control->m_len)
			return(EINVAL);
		if (cm->cmsg_level != IPPROTO_IPV6)
			continue;

		/*
		 * XXX should check if RFC2292 API is mixed with 2292bis API
		 */
		switch (cm->cmsg_type) {
		case IPV6_2292PKTINFO:
		case IPV6_PKTINFO:
			if (cm->cmsg_len != CMSG_LEN(sizeof(struct in6_pktinfo)))
				return(EINVAL);
			if (needcopy) {
				/* XXX: Is it really WAITOK? */
				opt->ip6po_pktinfo =
					malloc(sizeof(struct in6_pktinfo),
					       M_IP6OPT, M_WAITOK);
				bcopy(CMSG_DATA(cm), opt->ip6po_pktinfo,
				    sizeof(struct in6_pktinfo));
			} else
				opt->ip6po_pktinfo =
					(struct in6_pktinfo *)CMSG_DATA(cm);
			if (opt->ip6po_pktinfo->ipi6_ifindex &&
			    IN6_IS_ADDR_LINKLOCAL(&opt->ip6po_pktinfo->ipi6_addr))
				opt->ip6po_pktinfo->ipi6_addr.s6_addr16[1] =
					htons(opt->ip6po_pktinfo->ipi6_ifindex);

			if (opt->ip6po_pktinfo->ipi6_ifindex > if_index
			 || opt->ip6po_pktinfo->ipi6_ifindex < 0) {
				return(ENXIO);
			}

			/*
			 * Check if the requested source address is indeed a
			 * unicast address assigned to the node.
			 */
			if (!IN6_IS_ADDR_UNSPECIFIED(&opt->ip6po_pktinfo->ipi6_addr)) {
				struct ifaddr *ia;
				struct sockaddr_in6 sin6;

				bzero(&sin6, sizeof(sin6));
				sin6.sin6_len = sizeof(sin6);
				sin6.sin6_family = AF_INET6;
				sin6.sin6_addr =
					opt->ip6po_pktinfo->ipi6_addr;
				ia = ifa_ifwithaddr(sin6tosa(&sin6));
				if (ia == NULL)
					return(EADDRNOTAVAIL);
			}
			break;

		case IPV6_2292HOPLIMIT:
		case IPV6_HOPLIMIT:
			if (cm->cmsg_len != CMSG_LEN(sizeof(int)))
				return(EINVAL);

			opt->ip6po_hlim = *(int *)CMSG_DATA(cm);
			if (opt->ip6po_hlim < -1 || opt->ip6po_hlim > 255)
				return(EINVAL);
			break;

		case IPV6_TCLASS:
			if (cm->cmsg_len != CMSG_LEN(sizeof(u_int8_t)))
				return(EINVAL);

			opt->ip6po_tclass = *(u_int8_t *)CMSG_DATA(cm);
			break;

		case IPV6_2292NEXTHOP:
		case IPV6_NEXTHOP:
			if (!priv)
				return(EPERM);

			if (cm->cmsg_len < sizeof(u_char) ||
			    /* check if cmsg_len is large enough for sa_len */
			    cm->cmsg_len < CMSG_LEN(*CMSG_DATA(cm)))
				return(EINVAL);

			if (needcopy) {
				opt->ip6po_nexthop =
					malloc(*CMSG_DATA(cm),
					       M_IP6OPT, M_WAITOK);
				bcopy(CMSG_DATA(cm),
				      opt->ip6po_nexthop,
				      *CMSG_DATA(cm));
			} else
				opt->ip6po_nexthop =
					(struct sockaddr *)CMSG_DATA(cm);
			break;

		case IPV6_2292HOPOPTS:
		case IPV6_HOPOPTS:
		{
			struct ip6_hbh *hbh;
			int hbhlen;

			if (cm->cmsg_len < CMSG_LEN(sizeof(struct ip6_hbh)))
				return(EINVAL);
			hbh = (struct ip6_hbh *)CMSG_DATA(cm);
			hbhlen = (hbh->ip6h_len + 1) << 3;
			if (cm->cmsg_len != CMSG_LEN(hbhlen))
				return(EINVAL);

			if (needcopy) {
				opt->ip6po_hbh =
					malloc(hbhlen, M_IP6OPT, M_WAITOK);
				bcopy(hbh, opt->ip6po_hbh, hbhlen);
			} else
				opt->ip6po_hbh = hbh;
			break;
		}

		case IPV6_2292DSTOPTS:
		case IPV6_DSTOPTS:
		case IPV6_RTHDRDSTOPTS:
		{
			struct ip6_dest *dest, **newdest;
			int destlen;

			if (cm->cmsg_len < CMSG_LEN(sizeof(struct ip6_dest)))
				return(EINVAL);
			dest = (struct ip6_dest *)CMSG_DATA(cm);
			destlen = (dest->ip6d_len + 1) << 3;
			if (cm->cmsg_len != CMSG_LEN(destlen))
				return(EINVAL);

			/*
			 * Determine the position that the destination options
			 * header should be inserted; before or after the
			 * routing header.
			 */
			switch (cm->cmsg_type) {
			case IPV6_2292DSTOPTS:
				/* 
				 * The old advacned API is ambiguous on this
				 * point. Our approach is to determine the
				 * position based according to the existence
				 * of a routing header. Note, however, that
				 * this depends on the order of the extension
				 * headers in the ancillary data; the 1st part
				 * of the destination options header must
				 * appear before the routing header in the
				 * ancillary data, too.
				 * RFC2292bis solved the ambiguity by
				 * introducing separate cmsg types.
				 */
				if (opt->ip6po_rthdr == NULL)
					newdest = &opt->ip6po_dest1;
				else
					newdest = &opt->ip6po_dest2;
				break;
			case IPV6_RTHDRDSTOPTS:
				newdest = &opt->ip6po_dest1;
				break;
			case IPV6_DSTOPTS:
				newdest = &opt->ip6po_dest2;
				break;
			}

			if (needcopy) {
				*newdest = malloc(destlen, M_IP6OPT, M_WAITOK);
				bcopy(dest, *newdest, destlen);
			} else
				*newdest = dest;

			break;
		}

		case IPV6_2292RTHDR:
		case IPV6_RTHDR:
		{
			struct ip6_rthdr *rth;
			int rthlen;

			if (cm->cmsg_len < CMSG_LEN(sizeof(struct ip6_rthdr)))
				return(EINVAL);
			rth = (struct ip6_rthdr *)CMSG_DATA(cm);
			rthlen = (rth->ip6r_len + 1) << 3;
			if (cm->cmsg_len != CMSG_LEN(rthlen))
				return(EINVAL);

			switch (rth->ip6r_type) {
			case IPV6_RTHDR_TYPE_0:
				/* must contain one addr */
				if (rth->ip6r_len == 0)
					return(EINVAL);
				/* length must be even */
				if (rth->ip6r_len % 2)
					return(EINVAL);
				if (rth->ip6r_len / 2 != rth->ip6r_segleft)
					return(EINVAL);
				break;
			default:
				return(EINVAL);	/* not supported */
			}

			if (needcopy) {
				opt->ip6po_rthdr = malloc(rthlen, M_IP6OPT,
							  M_WAITOK);
				bcopy(rth, opt->ip6po_rthdr, rthlen);
			} else
				opt->ip6po_rthdr = rth;

			break;
		}

		case IPV6_REACHCONF:
#if 0
			/*
			 * it looks dangerous to allow IPV6_REACHCONF to
			 * normal user.  it affects the ND state (system state)
			 * and can affect communication by others - jinmei
			 */
			if (!priv)
				return(EPERM);
#else
			/*
			 * we limit max # of subsequent userland reachability
			 * conformation by using ln->ln_byhint.
			 */
#endif
			if (cm->cmsg_len != CMSG_LEN(0))
				return(EINVAL);
			opt->ip6po_flags |= IP6PO_REACHCONF;
			break;

		case IPV6_USE_MIN_MTU:
			if (cm->cmsg_len != CMSG_LEN(0))
				return(EINVAL);
			opt->ip6po_flags |= IP6PO_MINMTU;
			break;

		case IPV6_USE_MTU:
			if (cm->cmsg_len != CMSG_LEN(sizeof(int)))
				return(EINVAL);
			opt->ip6po_mtu = *(int *)CMSG_DATA(cm);
			if (opt->ip6po_mtu < IPV6_MINMTU)
				return(EINVAL);
			break;

		default:
			return(ENOPROTOOPT);
		}
	}

	return(0);
}

/*
 * Routine called from ip6_output() to loop back a copy of an IP6 multicast
 * packet to the input queue of a specified interface.  Note that this
 * calls the output routine of the loopback "driver", but with an interface
 * pointer that might NOT be &loif -- easier than replicating that code here.
 */
void
ip6_mloopback(ifp, m, dst)
	struct ifnet *ifp;
	struct mbuf *m;
	struct sockaddr_in6 *dst;
{
	struct mbuf *copym;
#ifndef SCOPEDROUTING
	struct ip6_hdr *ip6;
#endif

	copym = m_copy(m, 0, M_COPYALL);
	if (copym == NULL)
		return;

	/*
	 * Make sure to deep-copy IPv6 header portion in case the data
	 * is in an mbuf cluster, so that we can safely override the IPv6
	 * header portion later.
	 */
	if ((copym->m_flags & M_EXT) != 0 ||
	    copym->m_len < sizeof(struct ip6_hdr)) {
		copym = m_pullup(copym, sizeof(struct ip6_hdr));
		if (copym == NULL)
			return;
	}

#ifdef DIAGNOSTIC
	if (copym->m_len < sizeof(*ip6)) {
		m_freem(copym);
		return;
	}
#endif

	ip6 = mtod(copym, struct ip6_hdr *);
#ifndef SCOPEDROUTING
	/*
	 * clear embedded scope identifiers if necessary.
	 * in6_clearscope will touch the addresses only when necessary.
	 */
	in6_clearscope(&ip6->ip6_src);
	in6_clearscope(&ip6->ip6_dst);
#endif

#if defined(__FreeBSD__) && __FreeBSD__ >= 3
#if (__FreeBSD_version >= 410000)
	(void)if_simloop(ifp, copym, dst->sin6_family, NULL);
#else
	(void)if_simloop(ifp, copym, (struct sockaddr *)dst, NULL);
#endif
#else
	(void)looutput(ifp, copym, (struct sockaddr *)dst, NULL);
#endif
}

/*
 * Chop IPv6 header off from the payload.
 */
static int
ip6_splithdr(m, exthdrs)
	struct mbuf *m;
	struct ip6_exthdrs *exthdrs;
{
	struct mbuf *mh;
	struct ip6_hdr *ip6;

	ip6 = mtod(m, struct ip6_hdr *);
	if (m->m_len > sizeof(*ip6)) {
		MGETHDR(mh, M_DONTWAIT, MT_HEADER);
		if (mh == 0) {
			m_freem(m);
			return ENOBUFS;
		}
		M_COPY_PKTHDR(mh, m);
		MH_ALIGN(mh, sizeof(*ip6));
		m->m_flags &= ~M_PKTHDR;
		m->m_len -= sizeof(*ip6);
		m->m_data += sizeof(*ip6);
		mh->m_next = m;
		m = mh;
		m->m_len = sizeof(*ip6);
		bcopy((caddr_t)ip6, mtod(m, caddr_t), sizeof(*ip6));
	}
	exthdrs->ip6e_ip6 = m;
	return 0;
}

/*
 * Compute IPv6 extension header length.
 */
#ifdef HAVE_NRL_INPCB
# define in6pcb	inpcb
# define in6p_outputopts	inp_outputopts6
#endif
int
ip6_optlen(in6p)
	struct in6pcb *in6p;
{
	int len;

	if (!in6p->in6p_outputopts)
		return 0;

	len = 0;
#define elen(x) \
    (((struct ip6_ext *)(x)) ? (((struct ip6_ext *)(x))->ip6e_len + 1) << 3 : 0)

	len += elen(in6p->in6p_outputopts->ip6po_hbh);
	if (in6p->in6p_outputopts->ip6po_rthdr)
		/* dest1 is valid with rthdr only */
		len += elen(in6p->in6p_outputopts->ip6po_dest1);
	len += elen(in6p->in6p_outputopts->ip6po_rthdr);
	len += elen(in6p->in6p_outputopts->ip6po_dest2);
	return len;
#undef elen
}
#ifdef HAVE_NRL_INPCB
# undef in6pcb
# undef in6p_outputopts
#endif
