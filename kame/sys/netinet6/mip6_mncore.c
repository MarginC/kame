/*	$KAME: mip6_mncore.c,v 1.26 2003/08/20 13:31:14 keiichi Exp $	*/

/*
 * Copyright (C) 2003 WIDE Project.  All rights reserved.
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
 * Copyright (c) 1999, 2000 and 2001 Ericsson Radio Systems AB
 * All rights reserved.
 *
 * Authors: Conny Larsson <Conny.Larsson@era.ericsson.se>
 *          Mattias Pettersson <Mattias.Pettersson@era.ericsson.se>
 *
 */

#if defined(__FreeBSD__) && __FreeBSD__ >= 3
#include "opt_inet6.h"
#include "opt_ipsec.h"
#include "opt_mip6.h"
#endif
#ifdef __NetBSD__
#include "opt_ipsec.h"
#endif

#include <sys/param.h>
#include <sys/errno.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/sockio.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/proc.h>
#include <sys/syslog.h>

#include <net/if.h>
#include <net/if_types.h>
#include <net/route.h>
#include <net/if_dl.h>
#include <net/net_osdep.h>

#include <netinet/in.h>
#include <netinet6/in6_var.h>
#include <netinet/ip6.h>
#include <netinet6/ip6_var.h>
#include <netinet/icmp6.h>
#include <netinet6/nd6.h>
#include <netinet6/in6_ifattach.h>
#include <netinet6/scope6_var.h>

#if defined(IPSEC) && !defined(__OpenBSD__)
#include <netinet6/ipsec.h>
#include <netinet6/ah.h>
#endif /* IPSEC && !__OpenBSD__ */

#include <net/if_hif.h>

#include <netinet6/mip6.h>
#include <netinet6/mip6_var.h>
#include <netinet6/mip6_cncore.h>
#include <netinet6/mip6_mncore.h>

struct mip6_unuse_hoa_list mip6_unuse_hoa;
struct mip6_preferred_ifnames mip6_preferred_ifnames;
#ifdef __NetBSD__
struct callout mip6_bu_ch = CALLOUT_INITIALIZER;
#elif (defined(__FreeBSD__) && __FreeBSD__ >= 3)
struct callout mip6_bu_ch;
#endif

static const struct sockaddr_in6 sin6_any = {
	sizeof(struct sockaddr_in6),	/* sin6_len */
	AF_INET6,			/* sin6_family */
	0,				/* sin6_port */
	0,				/* sin6_flowinfo */
	{{{0, 0, 0, 0}}},		/* sin6_addr */
	0				/* sin6_scope_id */
};
static int mip6_bu_count = 0;

/* movement processing. */
static int mip6_prelist_update_sub(struct hif_softc *, struct sockaddr_in6 *,
    union nd_opts *, struct nd_defrouter *, struct mbuf *);
static int mip6_register_current_location(struct hif_softc *);
static int mip6_haddr_config(struct hif_softc *);
static int mip6_attach_haddrs(struct hif_softc *);
static int mip6_add_haddrs(struct hif_softc *, struct ifnet *);
static int mip6_remove_haddrs(struct hif_softc *, struct ifnet *);
static int mip6_remove_addr(struct ifnet *, struct in6_ifaddr *);

/* binding update entry processing. */
static struct mip6_bu *mip6_bu_create(const struct sockaddr_in6 *,
    struct mip6_prefix *, struct sockaddr_in6 *, u_int16_t,
    struct hif_softc *);
static int mip6_bu_list_insert(struct mip6_bu_list *, struct mip6_bu *);
static int mip6_bu_list_notify_binding_change(struct hif_softc *, int);
static int64_t mip6_coa_get_lifetime(struct in6_addr *);
static void mip6_bu_update_firewallstate(struct mip6_bu *);
static void mip6_bu_list_update_firewallstate(struct hif_softc *);
static void mip6_bu_starttimer(void);
static void mip6_bu_stoptimer(void);
static void mip6_bu_timeout(void *);

/* IPv6 extention header processing. */
static caddr_t mip6_add_opt2dh(caddr_t, struct mip6_buffer *);
static void mip6_align_destopt(struct mip6_buffer *);

#if 0 /* mip6_bdt_xxx are not used any more. */
/* bi-directional tunneling proccessing. */
static int mip6_bdt_create(struct hif_softc *, struct sockaddr_in6 *);
static int mip6_bdt_delete(struct sockaddr_in6 *);
#endif /* 0 */

void
mip6_mn_init(void)
{
	/* initialization as a mobile node. */
	mip6_bu_init(); /* binding update routine initialize */
	mip6_halist_init(); /* homeagent list management initialize */
	mip6_prefix_init();

	LIST_INIT(&mip6_unuse_hoa);
}

/*
 * binding update management functions.
 */
void
mip6_bu_init(void)
{
#if defined(__FreeBSD__) && __FreeBSD_version >= 500000
	callout_init(&mip6_bu_ch, NULL);
#elif defined(__NetBSD__) || (defined(__FreeBSD__) && __FreeBSD__ >= 3)
	callout_init(&mip6_bu_ch);
#endif
}

/*
 * we have heard a new router advertisement.  process if it contains
 * prefix information options for updating prefix and home agent
 * lists.
 */
int
mip6_prelist_update(saddr, ndopts, dr, m)
	struct sockaddr_in6 *saddr; /* the addr that sent this RA. */
	union nd_opts *ndopts;
	struct nd_defrouter *dr; /* NULL in case of a router shutdown. */
	struct mbuf *m; /* the received router adv. packet. */
{
	struct mip6_ha *mha;
	struct hif_softc *sc;
	int error = 0;

	/* sanity check. */
	if (saddr == NULL)
		return (EINVAL);

	/* advertizing router is shutting down. */
	if (dr == NULL) {
		mha = mip6_ha_list_find_withaddr(&mip6_ha_list, saddr);
		if (mha) {
			error = mip6_ha_list_remove(&mip6_ha_list, mha);
		}
		return (error);
	}

	/* if no prefix information is included, we have nothing to do. */
	if ((ndopts == NULL) || (ndopts->nd_opts_pi == NULL)) {
		return (0);
	}

	for (sc = TAILQ_FIRST(&hif_softc_list);
	     sc;
	     sc = TAILQ_NEXT(sc, hif_entry)) {
		/* reorganize subnet groups. */
		error = mip6_prelist_update_sub(sc, saddr, ndopts, dr, m);
		if (error) {
			mip6log((LOG_ERR,
			    "%s:%d: failed to reorganize subnet groups.\n",
			    __FILE__, __LINE__));
			return (error);
		}
	}

	return (0);
}

static int
mip6_prelist_update_sub(sc, rtaddr, ndopts, dr, m)
	struct hif_softc *sc;
	struct sockaddr_in6 *rtaddr;
	union nd_opts *ndopts;
	struct nd_defrouter *dr;
	struct mbuf *m;
{
	int location;
	struct nd_opt_hdr *ndopt;
	struct nd_opt_prefix_info *ndopt_pi;
	struct sockaddr_in6 prefix_sa;
	int is_home;
	int mha_is_new, mpfx_is_new;
	struct mip6_ha *mha;
	struct mip6_prefix tmpmpfx, *mpfx;
	struct hif_ha *hha;
	int error = 0;
#if !(defined(__FreeBSD__) && __FreeBSD__ >= 3)
	long time_second = time.tv_sec;
#endif

	/* sanity check. */
	if ((sc == NULL) || (rtaddr == NULL) || (dr == NULL)
	    || (ndopts == NULL) || (ndopts->nd_opts_pi == NULL))
		return (EINVAL);

	/* a router advertisement must be sent from a link-local address. */
	if (!IN6_IS_ADDR_LINKLOCAL(&rtaddr->sin6_addr)) {
		mip6log((LOG_NOTICE,
		    "%s:%d: the source address of a router advertisement "
		    "is not a link-local address(%s).\n",
		    __FILE__, __LINE__, ip6_sprintf(&rtaddr->sin6_addr)));
		    /* ignore. */
		    return (0);
	}

	location = HIF_LOCATION_UNKNOWN;
	is_home = 0;

	for (ndopt = (struct nd_opt_hdr *)ndopts->nd_opts_pi;
	     ndopt <= (struct nd_opt_hdr *)ndopts->nd_opts_pi_end;
	     ndopt = (struct nd_opt_hdr *)((caddr_t)ndopt
		 + (ndopt->nd_opt_len << 3))) {
		if (ndopt->nd_opt_type != ND_OPT_PREFIX_INFORMATION)
			continue;
		ndopt_pi = (struct nd_opt_prefix_info *)ndopt;

		/* sanity check of prefix information. */
		if (ndopt_pi->nd_opt_pi_len != 4) {
			nd6log((LOG_INFO,
			    "nd6_ra_input: invalid option "
			    "len %d for prefix information option, "
			    "ignored\n", ndopt_pi->nd_opt_pi_len));
		}
		if (128 < ndopt_pi->nd_opt_pi_prefix_len) {
			nd6log((LOG_INFO,
			    "nd6_ra_input: invalid prefix "
			    "len %d for prefix information option, "
			    "ignored\n", ndopt_pi->nd_opt_pi_prefix_len));
			continue;
		}
		if (IN6_IS_ADDR_MULTICAST(&ndopt_pi->nd_opt_pi_prefix)
		    || IN6_IS_ADDR_LINKLOCAL(&ndopt_pi->nd_opt_pi_prefix)) {
			nd6log((LOG_INFO,
			    "nd6_ra_input: invalid prefix "
			    "%s, ignored\n",
			    ip6_sprintf(&ndopt_pi->nd_opt_pi_prefix)));
			continue;
		}
		/* aggregatable unicast address, rfc2374 */
		if ((ndopt_pi->nd_opt_pi_prefix.s6_addr8[0] & 0xe0) == 0x20
		    && ndopt_pi->nd_opt_pi_prefix_len != 64) {
			nd6log((LOG_INFO,
			    "nd6_ra_input: invalid prefixlen "
			    "%d for rfc2374 prefix %s, ignored\n",
			    ndopt_pi->nd_opt_pi_prefix_len,
			    ip6_sprintf(&ndopt_pi->nd_opt_pi_prefix)));
			continue;
		}

		bzero(&prefix_sa, sizeof(prefix_sa));
		prefix_sa.sin6_family = AF_INET6;
		prefix_sa.sin6_len = sizeof(prefix_sa);
		prefix_sa.sin6_addr = ndopt_pi->nd_opt_pi_prefix;
		/* XXX scope? */
		hha = hif_ha_list_find_withprefix(&sc->hif_ha_list_home,
		    &prefix_sa, ndopt_pi->nd_opt_pi_prefix_len);
		if (hha != NULL)
			is_home++;

		if (ndopt_pi->nd_opt_pi_flags_reserved
		    & ND_OPT_PI_FLAG_ROUTER) {
			bzero(&prefix_sa, sizeof(prefix_sa));
			prefix_sa.sin6_family = AF_INET6;
			prefix_sa.sin6_len = sizeof(prefix_sa);
			prefix_sa.sin6_addr = ndopt_pi->nd_opt_pi_prefix;
			/* XXX scope? */
			hha = hif_ha_list_find_withaddr(&sc->hif_ha_list_home,
			    &prefix_sa);
			if (hha != NULL)
				is_home++;
		}
	}

	/* check is the router is on our home agent list. */
	hha = hif_ha_list_find_withaddr(&sc->hif_ha_list_home, rtaddr);

	if ((is_home != 0) || (hha != NULL)) {
		/* we are home. */
		location = HIF_LOCATION_HOME;
	} else {
		/* we are foreign. */
		location = HIF_LOCATION_FOREIGN;
	}

	for (ndopt = (struct nd_opt_hdr *)ndopts->nd_opts_pi;
	     ndopt <= (struct nd_opt_hdr *)ndopts->nd_opts_pi_end;
	     ndopt = (struct nd_opt_hdr *)((caddr_t)ndopt
		 + (ndopt->nd_opt_len << 3))) {
		if (ndopt->nd_opt_type != ND_OPT_PREFIX_INFORMATION)
			continue;
		ndopt_pi = (struct nd_opt_prefix_info *)ndopt;

#if 0 /* we can skip these checks because we have already done above. */
		/* sanity check of prefix information. */
		if (ndopt_pi->nd_opt_pi_len != 4) {
			nd6log((LOG_INFO,
			    "nd6_ra_input: invalid option "
			    "len %d for prefix information option, "
			    "ignored\n", ndopt_pi->nd_opt_pi_len));
		}
		if (128 < ndopt_pi->nd_opt_pi_prefix_len) {
			nd6log((LOG_INFO,
			    "nd6_ra_input: invalid prefix "
			    "len %d for prefix information option, "
			    "ignored\n", ndopt_pi->nd_opt_pi_prefix_len));
			continue;
		}
		if (IN6_IS_ADDR_MULTICAST(&ndopt_pi->nd_opt_pi_prefix)
		    || IN6_IS_ADDR_LINKLOCAL(&ndopt_pi->nd_opt_pi_prefix)) {
			nd6log((LOG_INFO,
			    "nd6_ra_input: invalid prefix "
			    "%s, ignored\n",
			    ip6_sprintf(&ndopt_pi->nd_opt_pi_prefix)));
			continue;
		}
		/* aggregatable unicast address, rfc2374 */
		if ((ndopt_pi->nd_opt_pi_prefix.s6_addr8[0] & 0xe0) == 0x20
		    && ndopt_pi->nd_opt_pi_prefix_len != 64) {
			nd6log((LOG_INFO,
			    "nd6_ra_input: invalid prefixlen "
			    "%d for rfc2374 prefix %s, ignored\n",
			    ndopt_pi->nd_opt_pi_prefix_len,
			    ip6_sprintf(&ndopt_pi->nd_opt_pi_prefix)));
			continue;
		}
#endif

		bzero(&prefix_sa, sizeof(prefix_sa));
		prefix_sa.sin6_family = AF_INET6;
		prefix_sa.sin6_len = sizeof(prefix_sa);
		prefix_sa.sin6_addr = ndopt_pi->nd_opt_pi_prefix;

		/* update mip6_prefix_list. */
		bzero(&tmpmpfx, sizeof(tmpmpfx));
		tmpmpfx.mpfx_prefix.sin6_family = AF_INET6;
		tmpmpfx.mpfx_prefix.sin6_len = sizeof(tmpmpfx.mpfx_prefix);
		tmpmpfx.mpfx_prefix.sin6_addr = ndopt_pi->nd_opt_pi_prefix;
		tmpmpfx.mpfx_prefixlen = ndopt_pi->nd_opt_pi_prefix_len;
		tmpmpfx.mpfx_vltime = ntohl(ndopt_pi->nd_opt_pi_valid_time);
		tmpmpfx.mpfx_pltime = ntohl(ndopt_pi->nd_opt_pi_preferred_time);
		mpfx_is_new = 0;
		mpfx = mip6_prefix_list_find(&tmpmpfx);
		if (mpfx) {
			/* found an existing entry.  just update it. */
			mpfx->mpfx_vltime = tmpmpfx.mpfx_vltime;
			mpfx->mpfx_vlexpire = time_second + mpfx->mpfx_vltime;
			mpfx->mpfx_pltime = tmpmpfx.mpfx_pltime;
			mpfx->mpfx_plexpire = time_second + mpfx->mpfx_pltime;
			/* XXX mpfx->mpfx_haddr; */
			mip6_prefix_settimer(mpfx,
			    MIP6_PREFIX_EXPIRE_TIME(mpfx->mpfx_pltime) * hz);
			mpfx->mpfx_state = MIP6_PREFIX_STATE_PREFERRED;
		} else {
			/* this is a new prefix. */
			mpfx = mip6_prefix_create(&tmpmpfx.mpfx_prefix,
			    tmpmpfx.mpfx_prefixlen,
			    tmpmpfx.mpfx_vltime,
			    tmpmpfx.mpfx_pltime);
			if (mpfx == NULL) {
				mip6log((LOG_ERR,
				    "%s:%d: "
				    "mip6_prefix memory allocation failed.\n",
				    __FILE__, __LINE__));
				goto skip_prefix_update;
			}
			error = mip6_prefix_list_insert(&mip6_prefix_list,
			    mpfx);
			if (error) {
				mip6log((LOG_ERR,
				    "%s:%d: "
				    "mip6_prefix_insert_failed(%d).\n",
				    __FILE__, __LINE__, error));
				goto skip_prefix_update;
			}

			mpfx_is_new = 1;
			mip6log((LOG_INFO,
			    "%s:%d: receive a new prefix %s\n",
			    __FILE__, __LINE__,
			    ip6_sprintf(&ndopt_pi->nd_opt_pi_prefix)));
		}
	skip_prefix_update:

		/* update mip6_ha_list. */
		mha_is_new = 0;
		mha = mip6_ha_list_find_withaddr(&mip6_ha_list, rtaddr);
		if (mha) {
			/* the entry for rtaddr exists.  update information. */
			if (ndopt_pi->nd_opt_pi_flags_reserved
			    & ND_OPT_PI_FLAG_ROUTER) {
				/*
				 * if prefix information has a router flag,
				 * that entry includes a global address
				 * of a home agent.
				 */
				mha->mha_gaddr = tmpmpfx.mpfx_prefix;
			}
			mha->mha_flags = dr->flags;
		} else {
			/* this is a new ha or a router. */
			mha = mip6_ha_create(rtaddr,
			    (ndopt_pi->nd_opt_pi_flags_reserved
			    & ND_OPT_PI_FLAG_ROUTER)
			    ? &tmpmpfx.mpfx_prefix : NULL,
			    dr->flags, 0, dr->rtlifetime);
			if (mha == NULL) {
				mip6log((LOG_ERR,
				    "%s:%d mip6_ha memory allcation failed.\n",
				    __FILE__, __LINE__));
				goto skip_ha_update;
			}
			error = mip6_ha_list_insert(&mip6_ha_list, mha);
			if (error) {
				mip6log((LOG_ERR,
				    "%s:%d "
				    "mip6_ha_list_insert failed(%d).\n",
				    __FILE__, __LINE__, error));
				goto skip_ha_update;
			}

			mha_is_new = 1;
			mip6log((LOG_INFO,
			    "%s:%d: found a new router %s(%s)\n",
			    __FILE__, __LINE__,
			    ip6_sprintf(&rtaddr->sin6_addr),
			    ip6_sprintf(&tmpmpfx.mpfx_prefix.sin6_addr)));
		}
	skip_ha_update:

		if (mpfx_is_new || mha_is_new)
			mip6_prefix_ha_list_insert(&mpfx->mpfx_ha_list, mha);

		if (location == HIF_LOCATION_HOME) {
			hha = hif_ha_list_find_withmha(
			    &sc->hif_ha_list_foreign, mha);
			if (hha != NULL)
				hif_ha_list_remove(&sc->hif_ha_list_foreign,
				    hha);
			if (hif_ha_list_find_withmha(&sc->hif_ha_list_home,
			    mha) == NULL)
				hif_ha_list_insert(&sc->hif_ha_list_home, mha);
		} else {
			hha = hif_ha_list_find_withmha(&sc->hif_ha_list_home,
			    mha);
			if (hha != NULL)
				hif_ha_list_remove(&sc->hif_ha_list_home, hha);
			if (hif_ha_list_find_withmha(&sc->hif_ha_list_foreign,
			    mha) == NULL)
				hif_ha_list_insert(&sc->hif_ha_list_foreign,
				    mha);
		}
	}
	return (0);
}

/*
 * set all nd cache state of routers to ND6_LLINFO_PROBE.  this forces
 * the NUD for each router and make it quick to detach addresses those
 * are not usable.
 */
void
mip6_probe_routers(void)
{
	struct llinfo_nd6 *ln;

	ln = llinfo_nd6.ln_next;
	while (ln && ln != &llinfo_nd6) {
		if ((ln->ln_router) &&
		    ((ln->ln_state == ND6_LLINFO_REACHABLE) ||
		     (ln->ln_state == ND6_LLINFO_STALE))) {
			ln->ln_asked = 0;
			ln->ln_state = ND6_LLINFO_DELAY;
			nd6_llinfo_settimer(ln, 0);
		}
		ln = ln->ln_next;
	}
}

/*
 * mip6_process_movement() is called (1) after prefix onlink checking
 * has finished and (2) p2p address is configured by calling
 * in6_control().  if the CoA has changed, call
 * mip6_register_current_location() to make a home registration.
 */
void
mip6_process_movement(void)
{
	struct hif_softc *sc;
	int coa_changed = 0;

	for (sc = TAILQ_FIRST(&hif_softc_list); sc;
	     sc = TAILQ_NEXT(sc, hif_entry)) {
		hif_save_location(sc);
		coa_changed = mip6_select_coa(sc);
		if (coa_changed == 1) {
			if (mip6_process_pfxlist_status_change(sc)) {
				hif_restore_location(sc);
				continue;
			}
			if (mip6_register_current_location(sc)) {
				hif_restore_location(sc);
				continue;
			}
			mip6_bu_list_update_firewallstate(sc);
		} else
			hif_restore_location(sc);
	}
}

int
mip6_process_pfxlist_status_change(sc)
	struct hif_softc *sc;
{
	struct mip6_prefix *mpfx;
	struct sockaddr_in6 hif_coa;
	int error = 0;

	sc->hif_location = HIF_LOCATION_UNKNOWN;

	for (mpfx = LIST_FIRST(&mip6_prefix_list); mpfx;
	    mpfx = LIST_NEXT(mpfx, mpfx_entry)) {
		if (hif_ha_list_find_withmpfx(&sc->hif_ha_list_home,
		    mpfx) == NULL)
			continue;
		hif_coa = sc->hif_coa_ifa->ia_addr;
		if (in6_addr2zoneid(sc->hif_coa_ifa->ia_ifp,
		    &hif_coa.sin6_addr,	&hif_coa.sin6_scope_id)) {
			/* must not happen. */
		}
		if (in6_embedscope(&hif_coa.sin6_addr, &hif_coa)) {
		    /* must not happen. */
		}
		if (in6_are_prefix_equal(&hif_coa.sin6_addr,
		    &mpfx->mpfx_prefix.sin6_addr, mpfx->mpfx_prefixlen)) {
			sc->hif_location = HIF_LOCATION_HOME;
			goto i_know_where_i_am;
		}
	}
	sc->hif_location = HIF_LOCATION_FOREIGN;
 i_know_where_i_am:
	mip6log((LOG_INFO,
	    "location = %d\n", sc->hif_location));

	/*
	 * configure home addresses according to the home
	 * prefixes and the current location determined above.
	 */
	error = mip6_haddr_config(sc);
	if (error) {
		mip6log((LOG_ERR,
		    "%s:%d: home address configuration error.\n",
		     __FILE__, __LINE__));
		return (error);
	}

	return (0);
}

/*
 * mip6_register_current_location() is called only when CoA has
 * changed.  therefore, we can call mip6_home_registration() in any
 * case because we must have moved from somewhere to somewhere.
 */
static int
mip6_register_current_location(sc)
	struct hif_softc *sc;
{
	int error = 0;

	switch (sc->hif_location) {
	case HIF_LOCATION_HOME:
		/*
		 * we moved to home.  unregister our home address.
		 */
		error = mip6_home_registration(sc);
		break;

	case HIF_LOCATION_FOREIGN:
		/*
		 * we moved to foreign.  register the current CoA to
		 * our home agent.
		 */
		/* XXX: TODO register to the old subnet's AR. */
		error = mip6_home_registration(sc);
		break;

	case HIF_LOCATION_UNKNOWN:
		break;
	}

	return (error);
}

/*
 * source address selection like CoA selection.
 *
 *  1) prefer a home address
 *  2) prefer an appropriate scope address
 *  3) avoid a deprecated address
 *  4) prefer an address on an alived interface
 *  5) prefer an address on a preferred interface
 *  6) prefer an longest match address comparerd to a HoA
 *  7) try not to change a CoA
 */
int
mip6_select_coa(sc)
	struct hif_softc *sc;
{
	int hoa_scope, ia_best_scope, ia_scope;
	int ia_best_matchlen, ia_matchlen;
	struct in6_ifaddr *ia, *ia_best;
	struct sockaddr_in6 ia_addr, *hoa;
	struct mip6_prefix *mpfx;
	int i;

	hoa = NULL;
	hoa_scope = ia_best_scope = -1;
	ia_best_matchlen = -1;

	for (mpfx = LIST_FIRST(&mip6_prefix_list); mpfx;
	    mpfx = LIST_NEXT(mpfx, mpfx_entry)) {
		if (hif_ha_list_find_withmpfx(&sc->hif_ha_list_home, mpfx)
		    == NULL)
			continue;
		if (SA6_IS_ADDR_UNSPECIFIED(&mpfx->mpfx_haddr))
			continue;
		hoa = &mpfx->mpfx_haddr;
		hoa_scope = in6_addrscope(&hoa->sin6_addr);
	}

	ia_best = NULL;
	for (ia = in6_ifaddr; ia; ia = ia->ia_next) {
		ia_scope = -1;
		ia_matchlen = -1;

		/* IFT_HIF has only home addresses. */
		if (ia->ia_ifp->if_type == IFT_HIF)
			goto next;

		if (ia->ia6_flags &
		    (IN6_IFF_ANYCAST
#ifdef MIP6_STATIC_HADDR
		    | IN6_IFF_HOME
#endif
		    /* | IN6_IFF_TENTATIVE */
		    | IN6_IFF_DETACHED
		    | IN6_IFF_DUPLICATED))
			goto next;

		/* loopback address cannot be used as a CoA. */
		if (IN6_IS_ADDR_LOOPBACK(&ia->ia_addr.sin6_addr))
			goto next;

		/* link-locall addr as a CoA is impossible? */
		if (IN6_IS_ADDR_LINKLOCAL(&ia->ia_addr.sin6_addr))
			goto next;

		/* tempaddr as a CoA is not supported. */
		if (ia->ia6_flags & IN6_IFF_TEMPORARY)
			goto next;

		/* XXX need sockaddr_in6 format for comparing. */
		ia_addr = ia->ia_addr;
		if (in6_addr2zoneid(ia->ia_ifp,	&ia_addr.sin6_addr,
		    &ia_addr.sin6_scope_id))
			goto next; /* XXX */
		if (in6_embedscope(&ia_addr.sin6_addr, &ia_addr))
			goto next; /* XXX */

		/* prefer a home address. */
		for (mpfx = LIST_FIRST(&mip6_prefix_list); mpfx;
		    mpfx = LIST_NEXT(mpfx, mpfx_entry)) {
			if (hif_ha_list_find_withmpfx(&sc->hif_ha_list_home,
			    mpfx) == NULL)
				continue;
			if (SA6_ARE_ADDR_EQUAL(&mpfx->mpfx_haddr, &ia_addr)) {
				ia_best = ia;
				goto out;
			}
		}

		if (ia_best == NULL)
			goto replace;

		/* prefer appropriate scope. */
		ia_scope = in6_addrscope(&ia_addr.sin6_addr);
		if (IN6_ARE_SCOPE_CMP(ia_best_scope, ia_scope) < 0) {
			if (IN6_ARE_SCOPE_CMP(ia_best_scope, hoa_scope) < 0)
				goto replace;
			goto next;
		} else if (IN6_ARE_SCOPE_CMP(ia_scope, ia_best_scope) < 0) {
			if (IN6_ARE_SCOPE_CMP(ia_scope, hoa_scope) < 0)
				goto next;
			goto replace;
		}

		/* avoid a deprecated address. */
		if (!IFA6_IS_DEPRECATED(ia_best) && IFA6_IS_DEPRECATED(ia))
			goto next;
		if (IFA6_IS_DEPRECATED(ia_best) && !IFA6_IS_DEPRECATED(ia))
			goto replace;

		/* prefer an address on an alive interface. */
		if ((ia_best->ia_ifp->if_flags & IFF_UP) &&
		    !(ia->ia_ifp->if_flags & IFF_UP))
			goto next;
		if (!(ia_best->ia_ifp->if_flags & IFF_UP) &&
		    (ia->ia_ifp->if_flags & IFF_UP))
			goto replace;

		/* prefer an address on a preferred interface. */
		for (i = 0; i < sizeof(mip6_preferred_ifnames.mip6pi_ifname);
		    i++) {
			if ((strncmp(if_name(ia_best->ia_ifp),
			    mip6_preferred_ifnames.mip6pi_ifname[i],
			    IFNAMSIZ) == 0)
			    && (strncmp(if_name(ia->ia_ifp),
			    mip6_preferred_ifnames.mip6pi_ifname[i],
			    IFNAMSIZ) != 0))
				goto next;
			if ((strncmp(if_name(ia_best->ia_ifp),
			    mip6_preferred_ifnames.mip6pi_ifname[i],
			    IFNAMSIZ) != 0)
			    && (strncmp(if_name(ia->ia_ifp),
			    mip6_preferred_ifnames.mip6pi_ifname[i],
			    IFNAMSIZ) == 0))
				goto replace;
		}

		/* prefer a longest match address. */
		if (hoa != NULL) {
			ia_matchlen = in6_matchlen(&ia->ia_addr.sin6_addr,
			    &hoa->sin6_addr);
			if (ia_best_matchlen < ia_matchlen)
				goto replace;
			if (ia_matchlen < ia_best_matchlen)
				goto next;
		}

		/* prefer same CoA. */
		if ((ia_best == sc->hif_coa_ifa)
		    && (ia != sc->hif_coa_ifa))
			goto next;
		if ((ia_best != sc->hif_coa_ifa)
		    && (ia == sc->hif_coa_ifa))
			goto replace;

	replace:
		ia_best = ia;
		ia_best_scope = (ia_scope >= 0 ? ia_scope :
		    in6_addrscope(&ia_best->ia_addr.sin6_addr));
		if (hoa != NULL)
			ia_best_matchlen = (ia_matchlen >= 0 ? ia_matchlen :
			    in6_matchlen(&ia_best->ia_addr.sin6_addr,
				&hoa->sin6_addr));
	next:
		continue;
	out:
		break;
	}

	if (ia_best == NULL) {
		mip6log((LOG_INFO,
		    "%s:%d: no available CoA is found.\n",
		    __FILE__, __LINE__));
		return (0);
	}

	/* check if the CoA has been changed. */
	if (sc->hif_coa_ifa == ia_best) {
		/* CoA has not been changed. */
		return (0);
	}

	if (sc->hif_coa_ifa != NULL)
		IFAFREE(&sc->hif_coa_ifa->ia_ifa);
	sc->hif_coa_ifa = ia_best;
	IFAREF(&sc->hif_coa_ifa->ia_ifa);
	mip6log((LOG_INFO,
		 "%s:%d: CoA has changed to %s\n",
		 __FILE__, __LINE__,
		 ip6_sprintf(&ia_best->ia_addr.sin6_addr)));
	return (1);
}

static int
mip6_haddr_config(sc)
	struct hif_softc *sc;
{
	int error = 0;

	switch (sc->hif_location) {
	case HIF_LOCATION_HOME:
		/*
		 * remove all home addresses attached to hif.
		 * all physical addresses are assigned in a
		 * address autoconfiguration manner.
		 */
		error = mip6_detach_haddrs(sc);

		break;

	case HIF_LOCATION_FOREIGN:
		/*
		 * attach all home addresses to the hif interface.
		 * before attach home addresses, remove home addresses
		 * from physical i/f to avoid the duplication of
		 * address.
		 */
		error = mip6_attach_haddrs(sc);
		break;

	case HIF_LOCATION_UNKNOWN:
		break;
	}

	return (error);
}

/*
 * 1. remove all haddr assinged to ifp.
 * 2. add all haddr for sc to scifp.
 */
static int
mip6_attach_haddrs(sc)
	struct hif_softc *sc;
{
	struct ifnet *ifp;
	int error = 0;

	/* remove all home addresses for sc from phisical I/F. */
#if defined(__bsdi__) || (defined(__FreeBSD__) && __FreeBSD__ < 3)
	for (ifp = ifnet; ifp; ifp = ifp->if_next)
#else
	for (ifp = ifnet.tqh_first; ifp; ifp = ifp->if_list.tqe_next)
#endif
	{
		if (ifp->if_type == IFT_HIF)
			continue;

		error = mip6_remove_haddrs(sc, ifp);
		if (error) {
			mip6log((LOG_ERR,
				 "%s:%d: remove haddrs from %s failed.\n",
				 __FILE__, __LINE__,
				 if_name(ifp)));
			return (error);
		}
	}

	/* add home addresses for sc to hif(itself) */
	error = mip6_add_haddrs(sc, (struct ifnet *)sc);
	if (error) {
		mip6log((LOG_ERR,
			 "%s:%d: add haddrs to %s failed.\n",
			 __FILE__, __LINE__,
			 if_name((struct ifnet*)sc)));
		return (error);
	}

	return (0);
}

/*
 * remove all haddr for sc (the home network) from scifp.
 */
int
mip6_detach_haddrs(sc)
	struct hif_softc *sc;
{
	struct ifnet *hif_ifp = (struct ifnet *)sc;
	struct ifaddr *ia, *ia_next;
	struct in6_ifaddr *ia6;
	int error = 0;

#if defined(__bsdi__) || (defined(__FreeBSD__) && __FreeBSD__ < 3)
	for (ia = hif_ifp->if_addrlist;
	     ia;
	     ia = ia_next)
#elif defined(__FreeBSD__) && __FreeBSD__ >= 4
	for (ia = TAILQ_FIRST(&hif_ifp->if_addrhead);
	     ia;
	     ia = ia_next)
#else
	for (ia = hif_ifp->if_addrlist.tqh_first;
	     ia;
	     ia = ia_next)
#endif
	{
#if defined(__bsdi__) || (defined(__FreeBSD__) && __FreeBSD__ < 3)
		ia_next = ia->ifa_next;
#elif defined(__FreeBSD__) && __FreeBSD__ >= 4
		ia_next = TAILQ_NEXT(ia, ifa_link);
#else
		ia_next = ia->ifa_list.tqe_next;
#endif

		if (ia->ifa_addr->sa_family != AF_INET6)
			continue;
		ia6 = (struct in6_ifaddr *)ia;
		if (IN6_IS_ADDR_LINKLOCAL(&ia6->ia_addr.sin6_addr))
			continue;

		error = mip6_remove_addr(hif_ifp, ia6);
		if (error) {
			mip6log((LOG_ERR,
				 "%s:%d: address deletion failed (%s)\n",
				 __FILE__, __LINE__,
				 ip6_sprintf(&ia6->ia_addr.sin6_addr)));
			return (error);
		}
	}
#ifdef MIP6_STATIC_HADDR
	{
		struct nd_prefix *pr;
		for (pr = nd_prefix.lh_first;
		     pr;
		     pr = pr->ndpr_next) {
			if (hif_subnet_list_find_withprefix(
				    &sc->hif_hs_list_home, &pr->ndpr_prefix,
				    pr->ndpr_plen))
				break;
		}
		if (pr != NULL)
			mip6_add_haddrs(sc, pr->ndpr_ifp);
	}
#endif

	return (error);
}

/*
 * add all haddrs for sc to ifp.
 */
static int
mip6_add_haddrs(sc, ifp)
	struct hif_softc *sc;
	struct ifnet *ifp;
{
	struct mip6_prefix *mpfx;
	struct in6_aliasreq ifra;
	struct in6_ifaddr *ia6;
	int error = 0;
#ifdef __FreeBSD__
	struct timeval mono_time;
#endif /* __FreeBSD__ */

#ifdef __FreeBSD__
	microtime(&mono_time);
#endif /* __FreeBSD__ */

	if ((sc == NULL) || (ifp == NULL)) {
		return (EINVAL);
	}

	for (mpfx = LIST_FIRST(&mip6_prefix_list); mpfx;
	     mpfx = LIST_NEXT(mpfx, mpfx_entry)) {
		if (hif_ha_list_find_withmpfx(&sc->hif_ha_list_home, mpfx)
		    == NULL)
			continue;

		/*
		 * assign home address to mip6_prefix if not
		 * assigned yet.
		 */
		if (SA6_IS_ADDR_UNSPECIFIED(&mpfx->mpfx_haddr)) {
			error = mip6_prefix_haddr_assign(mpfx, sc);
			if (error) {
				mip6log((LOG_ERR,
				    "%s:%d: can't assign home address for prefix %s.\n",
				    __FILE__, __LINE__,
				    ip6_sprintf(&mpfx->mpfx_prefix.sin6_addr)));
				return (error);
			}
		}

		/* skip a prefix that has 0 lifetime. */
		if (mpfx->mpfx_vltime == 0)
			continue;

		/* construct in6_aliasreq. */
		bzero(&ifra, sizeof(ifra));
		bcopy(if_name(ifp), ifra.ifra_name, sizeof(ifra.ifra_name));
		ifra.ifra_addr.sin6_len = sizeof(struct sockaddr_in6);
		ifra.ifra_addr.sin6_family = AF_INET6;
		ifra.ifra_addr.sin6_addr = mpfx->mpfx_haddr.sin6_addr;
		ifra.ifra_prefixmask.sin6_len = sizeof(struct sockaddr_in6);
		ifra.ifra_prefixmask.sin6_family = AF_INET6;
		ifra.ifra_flags = IN6_IFF_HOME | IN6_IFF_AUTOCONF;
		if (ifp->if_type == IFT_HIF) {
			in6_prefixlen2mask(&ifra.ifra_prefixmask.sin6_addr,
			    128);
		} else {
			in6_prefixlen2mask(&ifra.ifra_prefixmask.sin6_addr,
			    mpfx->mpfx_prefixlen);
		}
		ifra.ifra_lifetime.ia6t_vltime = mpfx->mpfx_vltime;
		ifra.ifra_lifetime.ia6t_pltime = mpfx->mpfx_pltime;
		if (ifra.ifra_lifetime.ia6t_vltime == ND6_INFINITE_LIFETIME)
			ifra.ifra_lifetime.ia6t_expire = 0;
		else
			ifra.ifra_lifetime.ia6t_expire = mono_time.tv_sec
			    + ifra.ifra_lifetime.ia6t_vltime;
		if (ifra.ifra_lifetime.ia6t_pltime == ND6_INFINITE_LIFETIME)
			ifra.ifra_lifetime.ia6t_preferred = 0;
		else
			ifra.ifra_lifetime.ia6t_preferred = mono_time.tv_sec
			    + ifra.ifra_lifetime.ia6t_pltime;
		ia6 = in6ifa_ifpwithaddr(ifp, &ifra.ifra_addr.sin6_addr);
		error = in6_update_ifa(ifp, &ifra, ia6);
		if (error) {
			mip6log((LOG_ERR,
			    "%s:%d: add address (%s) failed. errno = %d\n",
			    __FILE__, __LINE__,
			    ip6_sprintf(&ifra.ifra_addr.sin6_addr),
				    error));
			return (error);
		}
	}

	return (0);
}

/*
 * remove all haddr for sc from ifp.
 */
static int
mip6_remove_haddrs(sc, ifp)
	struct hif_softc *sc;
	struct ifnet *ifp;
{
	struct ifaddr *ia, *ia_next;
	struct in6_ifaddr *ia6;
	struct mip6_prefix *mpfx;
	int error = 0;

#if defined(__bsdi__) || (defined(__FreeBSD__) && __FreeBSD__ < 3)
	for (ia = ifp->if_addrlist;
	     ia;
	     ia = ia_next)
#elif defined(__FreeBSD__) && __FreeBSD__ >= 4
	for (ia = TAILQ_FIRST(&ifp->if_addrhead);
	     ia;
	     ia = ia_next)
#else
	for (ia = ifp->if_addrlist.tqh_first;
	     ia;
	     ia = ia_next)
#endif
	{
#if defined(__bsdi__) || (defined(__FreeBSD__) && __FreeBSD__ < 3)
		ia_next = ia->ifa_next;
#elif defined(__FreeBSD__) && __FreeBSD__ >= 4
		ia_next = TAILQ_NEXT(ia, ifa_link);
#else
		ia_next = ia->ifa_list.tqe_next;
#endif

		if (ia->ifa_addr->sa_family != AF_INET6)
			continue;
		ia6 = (struct in6_ifaddr *)ia;

		for (mpfx = LIST_FIRST(&mip6_prefix_list); mpfx;
		     mpfx = LIST_NEXT(mpfx, mpfx_entry)) {
			if (hif_ha_list_find_withmpfx(&sc->hif_ha_list_home,
				mpfx) == NULL)
				continue;
			
			if (!in6_are_prefix_equal(&ia6->ia_addr.sin6_addr,
				&mpfx->mpfx_prefix.sin6_addr,
				mpfx->mpfx_prefixlen)) {
				continue;
			}
			error = mip6_remove_addr(ifp, ia6);
			if (error) {
				mip6log((LOG_ERR, "%s:%d: deletion %s from %s failed\n",
				    __FILE__, __LINE__, if_name(ifp),
				    ip6_sprintf(&ia6->ia_addr.sin6_addr)));
				continue;
			}
		}
	}

	return (error);
}

/*
 * remove addr specified by ia6 from ifp.
 */
static int
mip6_remove_addr(ifp, ia6)
	struct ifnet *ifp;
	struct in6_ifaddr *ia6;
{
	struct in6_aliasreq ifra;
	int i = 0, purgeprefix = 0;
	struct nd_prefixctl pr0;
	struct nd_prefix *pr = NULL;

	bcopy(if_name(ifp), ifra.ifra_name, sizeof(ifra.ifra_name));
	bcopy(&ia6->ia_addr, &ifra.ifra_addr, sizeof(struct sockaddr_in6));
	bcopy(&ia6->ia_prefixmask, &ifra.ifra_prefixmask,
	      sizeof(struct sockaddr_in6));

	/* address purging code is copyed from in6_control(). */

	/*
	 * If the address being deleted is the only one that owns
	 * the corresponding prefix, expire the prefix as well.
	 * XXX: theoretically, we don't have to worry about such
	 * relationship, since we separate the address management
	 * and the prefix management.  We do this, however, to provide
	 * as much backward compatibility as possible in terms of
	 * the ioctl operation.
	 */
	bzero(&pr0, sizeof(pr0));
	pr0.ndpr_ifp = ifp;
	pr0.ndpr_plen = in6_mask2len(&ia6->ia_prefixmask.sin6_addr, NULL);
	if (pr0.ndpr_plen == 128)
		goto purgeaddr;
	pr0.ndpr_prefix = ia6->ia_addr;
	for (i = 0; i < 4; i++) {
		pr0.ndpr_prefix.sin6_addr.s6_addr32[i] &=
		    ia6->ia_prefixmask.sin6_addr.s6_addr32[i];
	}
	/*
	 * The logic of the following condition is a bit complicated.
	 * We expire the prefix when
	 * 1. the address obeys autoconfiguration and it is the
	 *    only owner of the associated prefix, or
	 * 2. the address does not obey autoconf and there is no
	 *    other owner of the prefix.
	 */
	if ((pr = nd6_prefix_lookup(&pr0)) != NULL &&
	    (((ia6->ia6_flags & IN6_IFF_AUTOCONF) != 0 &&
	    pr->ndpr_refcnt == 1) ||
	    ((ia6->ia6_flags & IN6_IFF_AUTOCONF) == 0 &&
	    pr->ndpr_refcnt == 0)))
		purgeprefix = 1;

  purgeaddr:
	in6_purgeaddr(&ia6->ia_ifa);
	if (pr && purgeprefix)
		prelist_remove(pr);

	return (0);
}

/*
 * check whether this address needs dad or not
 */
int
mip6_ifa_need_dad(ia)
	struct in6_ifaddr *ia;
{
	struct sockaddr_in6 sin6;
	struct hif_softc *sc = NULL;
	struct mip6_bu *mbu = NULL;
	int need_dad = 0;

	sin6.sin6_len = sizeof(sin6);
	sin6.sin6_family = AF_INET6;
	sin6.sin6_addr = ia->ia_addr.sin6_addr;
	if (in6_addr2zoneid(ia->ia_ifp, &sin6.sin6_addr, &sin6.sin6_scope_id))
		return (EINVAL);

	for (sc = TAILQ_FIRST(&hif_softc_list); sc;
	     sc = TAILQ_NEXT(sc, hif_entry)) {
		mbu = mip6_bu_list_find_home_registration(
			&sc->hif_bu_list,
			&sin6);
		if (mbu != NULL)
			break;
	}
#ifdef MIP6_DEBUG
if (mbu)	mip6_bu_print(mbu);
#endif
	if ((mbu == NULL) || (mbu->mbu_lifetime <= 0))
		need_dad = 1;

	return (need_dad);
}

/*
 * if packet is tunneled, send BU to the peer for route optimization.
 */
/*
 * the algorithm below is worth considering
 *
 * from Hesham Soliman on mobile-ip
 * <034BEFD03799D411A59F00508BDF754603008B1F@esealnt448.al.sw.ericsson.se>
 *
 * - Did the packet contain a routing header ?
 * - Did the routing header contain the Home address of the
 *  MN as the last segment and its CoA (as specified in the BU list) ?
 *
 * If the answer to both is yes then the packet was route optimised.
 * if no then it wasn't and it doesn't really matter whether it was
 * tunnelled by THE HA or another node.
 * This will have two advantages (outside the HMIPv6 area) :
 *
 * - Simpler processing in the kernel since the MIPv6 code would
 *   not have to "remember" whether the inner packet being processed
 *   now was originally tunnelled.
 *
 * - Will allow for future HA redundancy mechanisms because if the
 *   HA crashes and another HA starts tunnelling the packet the
 *   MN does not need to know or care. Excet of course when it's
 *   about to refresh the Binding Cache but that can be handled
 *   by the HA redundancy protocol.
 */
int
mip6_route_optimize(m)
	struct mbuf *m;
{
	struct m_tag *mtag;
	struct in6_ifaddr *ia;
	struct ip6aux *ip6a;
	struct ip6_hdr *ip6;
	struct sockaddr_in6 sin6src, sin6dst;
	struct mip6_prefix *mpfx;
	struct mip6_bu *mbu;
	struct hif_softc *sc;
	struct sockaddr_in6 hif_coa;
	int error = 0;

	if (!MIP6_IS_MN) {
		/* only MN does the route optimization. */
		return (0);
	}

	ip6 = mtod(m, struct ip6_hdr *);
	if (ip6_getpktaddrs(m, &sin6src, &sin6dst))
		return (EINVAL);

	if (IN6_IS_ADDR_LINKLOCAL(&sin6src.sin6_addr) ||
	    IN6_IS_ADDR_SITELOCAL(&sin6src.sin6_addr)) {	/* XXX */
		return (0);
	}
	/* Quick check */
	if (IN6_IS_ADDR_LINKLOCAL(&sin6dst.sin6_addr) ||
	    IN6_IS_ADDR_SITELOCAL(&sin6dst.sin6_addr) ||	/* XXX */
	    IN6_IS_ADDR_MULTICAST(&sin6dst.sin6_addr)) {
		return (0);
	}

	for (ia = in6_ifaddr; ia; ia = ia->ia_next) {
		if (IN6_ARE_ADDR_EQUAL(&ia->ia_addr.sin6_addr,
		    &sin6src.sin6_addr)) {
			return (0);
		}
	}

	mtag = ip6_findaux(m);
	if (mtag) {
		ip6a = (struct ip6aux *) (mtag + 1);
		if (ip6a->ip6a_flags & IP6A_ROUTEOPTIMIZED) {
			/* no need to optimize route. */
			return (0);
		}
	}
	/*
	 * this packet has no rthdr or has a rthdr not related mip6
	 * route optimization.
	 */

	/* check if we are home. */
	sc = hif_list_find_withhaddr(&sin6dst);
	if (sc == NULL) {
		/* this dst addr is not one of our home addresses. */
		return (0);
	}
	if (sc->hif_location == HIF_LOCATION_HOME) {
		/* we are home.  no route optimization is required. */
		return (0);
	}

	/*
	 * find a mip6_prefix which has a home address of received
	 * packet.
	 */
	mpfx = mip6_prefix_list_find_withhaddr(&mip6_prefix_list,
					       &sin6dst);
	if (mpfx == NULL) {
		/*
		 * no related prefix found.  this packet is
		 * destined to another address of this node
		 * that is not a home address.
		 */
		return (0);
	}

	/* get current CoA and recover its scope information. */
	hif_coa = sc->hif_coa_ifa->ia_addr;
	if (in6_addr2zoneid(sc->hif_coa_ifa->ia_ifp, &hif_coa.sin6_addr,
		&hif_coa.sin6_scope_id)) {
		/* must not happen. */
	}
	if (in6_embedscope(&hif_coa.sin6_addr, &hif_coa)) {
		/* must not happen. */
	}

	/*
	 * search all binding update entries with the address of the
	 * peer sending this un-optimized packet.
	 */
	mbu = mip6_bu_list_find_withpaddr(&sc->hif_bu_list,
					  &sin6src, &sin6dst);
	if (mbu == NULL) {
		/*
		 * if no binding update entry is found, this is a
		 * first packet from the peer.  create a new binding
		 * update entry for this peer.
		 */
		mbu = mip6_bu_create(&sin6src,
				     mpfx,
				     &hif_coa,
				     0, sc);
		if (mbu == NULL) {
			error = ENOMEM;
			goto bad;
		}
		mip6_bu_list_insert(&sc->hif_bu_list, mbu);
	} else {
#if 0
		int32_t coa_lifetime;
#if !(defined(__FreeBSD__) && __FreeBSD__ >= 3)
		long time_second = time.tv_sec;
#endif
		/*
		 * found a binding update entry.  we should resend a
		 * binding update to this peer because he is not add
		 * routing header for the route optimization.
		 */
		mbu->mbu_coa = hif_coa;
		coa_lifetime = mip6_coa_get_lifetime(&mbu->mbu_coa.sin6_addr);
		if (coa_lifetime < mpfx->mpfx_vltime) {
			mbu->mbu_lifetime = coa_lifetime;
		} else {
			mbu->mbu_lifetime = mpfx->mpfx_vltime;
		}
		if (mip6_config.mcfg_bu_maxlifetime > 0 &&
		    mbu->mbu_lifetime > mip6_config.mcfg_bu_maxlifetime)
			mbu->mbu_lifetime = mip6_config.mcfg_bu_maxlifetime;
		mbu->mbu_expire = time_second + mbu->mbu_lifetime;
		/* sanity check for overflow */
		if (mbu->mbu_expire < time_second)
			mbu->mbu_expire = 0x7fffffff;
		mbu->mbu_refresh = mbu->mbu_lifetime;
#endif
	}
	mip6_bu_fsm(mbu, MIP6_BU_PRI_FSM_EVENT_REVERSE_PACKET, NULL);

	return (0);
 bad:
	m_freem(m);
	return (error);
}

static struct mip6_bu *
mip6_bu_create(paddr, mpfx, coa, flags, sc)
	const struct sockaddr_in6 *paddr;
	struct mip6_prefix *mpfx;
	struct sockaddr_in6 *coa;
	u_int16_t flags;
	struct hif_softc *sc;
{
	struct mip6_bu *mbu;
	u_int32_t coa_lifetime, cookie;
#if !(defined(__FreeBSD__) && __FreeBSD__ >= 3)
	long time_second = time.tv_sec;
#endif

	MALLOC(mbu, struct mip6_bu *, sizeof(struct mip6_bu),
	       M_TEMP, M_NOWAIT);
	if (mbu == NULL) {
		mip6log((LOG_ERR,
			 "%s:%d: memory allocation failed.\n",
			 __FILE__, __LINE__));
		return (NULL);
	}

	coa_lifetime = mip6_coa_get_lifetime(&coa->sin6_addr);

	bzero(mbu, sizeof(*mbu));
	mbu->mbu_flags = flags;
	mbu->mbu_paddr = *paddr;
	mbu->mbu_haddr = mpfx->mpfx_haddr;
	if (sc->hif_location == HIF_LOCATION_HOME) {
		/* un-registration. */
		mbu->mbu_coa = mpfx->mpfx_haddr;
		mbu->mbu_pri_fsm_state =
		    (mbu->mbu_flags & IP6MU_HOME)
		    ? MIP6_BU_PRI_FSM_STATE_WAITD
		    : MIP6_BU_PRI_FSM_STATE_IDLE;
	} else {
		/* registration. */
		mbu->mbu_coa = *coa;
		mbu->mbu_pri_fsm_state =
		    (mbu->mbu_flags & IP6MU_HOME)
		    ? MIP6_BU_PRI_FSM_STATE_WAITA
		    : MIP6_BU_PRI_FSM_STATE_IDLE;
	}
	if (coa_lifetime < mpfx->mpfx_vltime) {
		mbu->mbu_lifetime = coa_lifetime;
	} else {
		mbu->mbu_lifetime = mpfx->mpfx_vltime;
	}
	if (mip6_config.mcfg_bu_maxlifetime > 0 &&
	    mbu->mbu_lifetime > mip6_config.mcfg_bu_maxlifetime)
		mbu->mbu_lifetime = mip6_config.mcfg_bu_maxlifetime;
	mbu->mbu_expire = time_second + mbu->mbu_lifetime;
	/* sanity check for overflow */
	if (mbu->mbu_expire < time_second)
		mbu->mbu_expire = 0x7fffffff;
	mbu->mbu_refresh = mbu->mbu_lifetime;
	/* Sequence Number SHOULD start at a random value */
	mbu->mbu_seqno = (u_int16_t)arc4random();
	cookie = arc4random();
	bcopy(&cookie, &mbu->mbu_mobile_cookie[0], 4);
	cookie = arc4random();
	bcopy(&cookie, &mbu->mbu_mobile_cookie[4], 4);
	mbu->mbu_hif = sc;
	/* *mbu->mbu_encap = NULL; */
	mip6_bu_update_firewallstate(mbu);

	return (mbu);
}

static int
mip6_bu_list_insert(bu_list, mbu)
	struct mip6_bu_list *bu_list;
	struct mip6_bu *mbu;
{
	LIST_INSERT_HEAD(bu_list, mbu, mbu_entry);

	if (mip6_bu_count == 0) {
		mip6log((LOG_INFO, "%s:%d: BU timer started.\n",
			__FILE__, __LINE__));
		mip6_bu_starttimer();
	}
	mip6_bu_count++;

	return (0);
}

int
mip6_bu_list_remove(mbu_list, mbu)
	struct mip6_bu_list *mbu_list;
	struct mip6_bu *mbu;
{
	if ((mbu_list == NULL) || (mbu == NULL)) {
		return (EINVAL);
	}

	LIST_REMOVE(mbu, mbu_entry);
	FREE(mbu, M_TEMP);

	mip6_bu_count--;
	if (mip6_bu_count == 0) {
		mip6_bu_stoptimer();
		mip6log((LOG_INFO,
			 "%s:%d: BU timer stopped.\n",
			__FILE__, __LINE__));
	}

	return (0);
}

int
mip6_bu_list_remove_all(mbu_list, all)
	struct mip6_bu_list *mbu_list;
	int all;
{
	struct mip6_bu *mbu, *mbu_next;
	int error = 0;

	if (mbu_list == NULL) {
		return (EINVAL);
	}

	for (mbu = LIST_FIRST(mbu_list);
	     mbu;
	     mbu = mbu_next) {
		mbu_next = LIST_NEXT(mbu, mbu_entry);

		if (!all &&
		    (mbu->mbu_flags & IP6MU_HOME) == 0 &&
		    (mbu->mbu_state & MIP6_BU_STATE_DISABLE) == 0)
			continue;

		error = mip6_bu_list_remove(mbu_list, mbu);
		if (error) {
			mip6log((LOG_ERR,
				 "%s:%d: can't remove BU.\n",
				 __FILE__, __LINE__));
			continue;
		}
	}

	return (0);
}

struct mip6_bu *
mip6_bu_list_find_home_registration(bu_list, haddr)
     struct mip6_bu_list *bu_list;
     struct sockaddr_in6 *haddr;
{
	struct mip6_bu *mbu;

	for (mbu = LIST_FIRST(bu_list); mbu;
	     mbu = LIST_NEXT(mbu, mbu_entry)) {
		if (SA6_ARE_ADDR_EQUAL(&mbu->mbu_haddr, haddr) &&
		    (mbu->mbu_flags & IP6MU_HOME) != 0)
			break;
	}
	return (mbu);
}

/*
 * find the binding update entry specified by the destination (peer)
 * address and optionally the home address of the mobile node.  if the
 * home address is not specified, the first binding update entry found
 * that matches the specified destination address will be returned.
 */
struct mip6_bu *
mip6_bu_list_find_withpaddr(bu_list, paddr, haddr)
	struct mip6_bu_list *bu_list;
	struct sockaddr_in6 *paddr;
	struct sockaddr_in6 *haddr;
{
	struct mip6_bu *mbu;

	/* sanity check. */
	if (paddr == NULL)
		return (NULL);

	for (mbu = LIST_FIRST(bu_list); mbu;
	     mbu = LIST_NEXT(mbu, mbu_entry)) {
		if (SA6_ARE_ADDR_EQUAL(&mbu->mbu_paddr, paddr)
		    && ((haddr != NULL)
			? SA6_ARE_ADDR_EQUAL(&mbu->mbu_haddr, haddr)
			: 1))
			break;
	}
	return (mbu);
}

int
mip6_home_registration(sc)
	struct hif_softc *sc;
{
	struct sockaddr_in6 hif_coa;
	struct mip6_prefix *mpfx;
	struct mip6_bu *mbu;
	const struct sockaddr_in6 *haaddr;
	struct hif_ha *hha;

	/* get current CoA and recover its scope information. */
	hif_coa = sc->hif_coa_ifa->ia_addr;
	if (in6_addr2zoneid(sc->hif_coa_ifa->ia_ifp, &hif_coa.sin6_addr,
		&hif_coa.sin6_scope_id)) {
		/* must not happen. */
	}
	if (in6_embedscope(&hif_coa.sin6_addr, &hif_coa)) {
		/* must not happen. */
	}

	for (mpfx = LIST_FIRST(&mip6_prefix_list); mpfx;
	     mpfx = LIST_NEXT(mpfx, mpfx_entry)) {
		if (hif_ha_list_find_withmpfx(&sc->hif_ha_list_home, mpfx)
		    == NULL)
			continue;

		for (mbu = LIST_FIRST(&sc->hif_bu_list); mbu;
		     mbu = LIST_NEXT(mbu, mbu_entry)) {
			if ((mbu->mbu_flags & IP6MU_HOME) == 0)
				continue;
			if (SA6_ARE_ADDR_EQUAL(&mbu->mbu_haddr,
				&mpfx->mpfx_haddr))
				break;
		}
		if (mbu == NULL) {
			/* not exist */
			if (sc->hif_location == HIF_LOCATION_HOME) {
				/*
				 * we are home and we have no binding
				 * update entry for home registration.
				 * this will happen when either of the
				 * following two cases happens.
				 *
				 * 1. enabling MN function at home
				 * subnet.
				 *
				 * 2. returning home with expired home
				 * registration.
				 *
				 * in either case, we should do
				 * nothing.
				 */
				continue;
			}

			/*
			 * no home registration found.  create a new
			 * binding update entry.
			 */

			/* pick the preferable HA from the list. */
			hha = hif_ha_list_find_preferable(
				&sc->hif_ha_list_home, mpfx);
				    
			if (hha == NULL) {
				/*
				 * if no HA is found, try to find a HA
				 * using Dynamic Home Agent Discovery.
				 */
				mip6log((LOG_INFO,
				    "%s:%d: no home agent.  start ha discovery.\n",
				    __FILE__, __LINE__));
				mip6_icmp6_dhaad_req_output(sc);
				haaddr = &sin6_any;
			} else {
				haaddr = &hha->hha_mha->mha_gaddr;
			}

			mbu = mip6_bu_create(haaddr, mpfx, &hif_coa,
			    IP6MU_ACK|IP6MU_HOME
#ifndef MIP6_STATIC_HADDR
			    |IP6MU_LINK
#endif
			    , sc);
			if (mbu == NULL)
				return (ENOMEM);
			/*
			 * for the first registration to the home
			 * agent, the ack timeout value should be
			 * (retrans * dadtransmits) * 1.5.
			 */
			/*
			 * XXX: TODO: KAME has different dad retrans
			 * values for each interfaces.  which retrans
			 * value should be selected ?
			 */

			mip6_bu_list_insert(&sc->hif_bu_list, mbu);

			/* XXX */
			if (sc->hif_location != HIF_LOCATION_HOME)
				mip6_bu_fsm(mbu,
				    MIP6_BU_PRI_FSM_EVENT_MOVEMENT, NULL);
			else
				mip6_bu_fsm(mbu,
				    MIP6_BU_PRI_FSM_EVENT_RETURNING_HOME,
				    NULL);
		} else {
			if (sc->hif_location != HIF_LOCATION_HOME)
				mip6_bu_fsm(mbu,
				    MIP6_BU_PRI_FSM_EVENT_MOVEMENT, NULL);
			else
				mip6_bu_fsm(mbu,
				    MIP6_BU_PRI_FSM_EVENT_RETURNING_HOME,
				    NULL);
		}
	}

	return (0);
}

int
mip6_home_registration2(mbu)
	struct mip6_bu *mbu;
{
	struct sockaddr_in6 hif_coa;
	struct mip6_prefix *mpfx;
	int32_t coa_lifetime, prefix_lifetime;
	int error;
#if defined(__FreeBSD__) && __FreeBSD__ >= 3
	struct timeval mono_time;
#endif

	/* sanity check. */
	if (mbu == NULL)
		return (EINVAL);

#if defined(__FreeBSD__) && __FreeBSD__ >= 3
	mono_time.tv_sec = time_second;
#endif

	/* get current CoA and recover its scope information. */
	hif_coa = mbu->mbu_hif->hif_coa_ifa->ia_addr;
	if (in6_addr2zoneid(mbu->mbu_hif->hif_coa_ifa->ia_ifp,
	    &hif_coa.sin6_addr, &hif_coa.sin6_scope_id)) {
		/* must not happen. */
	}
	if (in6_embedscope(&hif_coa.sin6_addr, &hif_coa)) {
		/* must not happen. */
	}

	/*
	 * a binding update entry exists. update information.
	 */

	/* update CoA. */
	if (mbu->mbu_hif->hif_location == HIF_LOCATION_HOME) {
		/* home de-registration. */
		mbu->mbu_coa = mbu->mbu_haddr;
	} else {
		/* home registration. */
		mbu->mbu_coa = hif_coa;
	}

	/* update lifetime. */
	coa_lifetime = mip6_coa_get_lifetime(&mbu->mbu_coa.sin6_addr);
	prefix_lifetime = 0x7fffffff;
	for (mpfx = LIST_FIRST(&mip6_prefix_list); mpfx;
	     mpfx = LIST_NEXT(mpfx, mpfx_entry)) {
		if (hif_ha_list_find_withmpfx(&mbu->mbu_hif->hif_ha_list_home,
			mpfx) == NULL)
			continue;
		if (mpfx->mpfx_vltime < prefix_lifetime)
			prefix_lifetime = mpfx->mpfx_vltime;
	}
	if (coa_lifetime < prefix_lifetime) {
		mbu->mbu_lifetime = coa_lifetime;
	} else {
		mbu->mbu_lifetime = prefix_lifetime;
	}
	mbu->mbu_expire = mono_time.tv_sec + mbu->mbu_lifetime;
	/* sanity check for overflow */
	if (mbu->mbu_expire < mono_time.tv_sec)
		mbu->mbu_expire = 0x7fffffff;
	mbu->mbu_refresh = mbu->mbu_lifetime;
	/* mbu->mbu_flags |= IP6MU_DAD ;*/

	/* send a binding update. */
	error = mip6_bu_send_bu(mbu);

	return (error);
}

int
mip6_bu_encapcheck(m, off, proto, arg)
	const struct mbuf *m;
	int off;
	int proto;
	void *arg;
{
	struct ip6_hdr *ip6;
	struct mip6_bu *mbu = (struct mip6_bu *)arg;
	struct hif_softc *sc;
	struct mip6_prefix *mpfx;
	struct sockaddr_in6 encap_src, encap_dst;
	struct sockaddr_in6 *haaddr, *myaddr, *mycoa;

	if (mbu == NULL) {
		return (0);
	}
	if ((sc = mbu->mbu_hif) == NULL) {
		return (0);
	}

	ip6 = mtod(m, struct ip6_hdr*);
	if (ip6_getpktaddrs((struct mbuf *)m, &encap_src, &encap_dst))
		return (0);

	haaddr = &mbu->mbu_paddr;
	myaddr = &mbu->mbu_haddr;
	mycoa = &mbu->mbu_coa;

	/*
	 * check whether this packet is from the correct sender (that
	 * is, our home agent) to the CoA or the HoA the mobile node
	 * has registered before.
	 */
	if (!SA6_ARE_ADDR_EQUAL(&encap_src, haaddr) ||
	    !(SA6_ARE_ADDR_EQUAL(&encap_dst, mycoa) ||
	      SA6_ARE_ADDR_EQUAL(&encap_dst, myaddr))) {
		return (0);
	}

	/*
	 * XXX: should we compare the ifid of the inner dstaddr of the
	 * incoming packet and the ifid of the mobile node's?  these
	 * check will be done in the ip6_input and later.
	 */

	/* check mn prefix */
	for (mpfx = LIST_FIRST(&mip6_prefix_list); mpfx;
	     mpfx = LIST_NEXT(mpfx, mpfx_entry)) {
		if (!in6_are_prefix_equal(&myaddr->sin6_addr,
			&mpfx->mpfx_prefix.sin6_addr,
			mpfx->mpfx_prefixlen)) {
			/* this prefix doesn't match my prefix.
			   check next. */
			continue;
		}
		goto match;
	}
	return (0);
 match:
	return (128);
}

static int
mip6_bu_list_notify_binding_change(sc, home)
	struct hif_softc *sc;
	int home;
{
	struct sockaddr_in6 hif_coa;
	struct mip6_prefix *mpfx;
	struct mip6_bu *mbu, *mbu_next;
	int32_t coa_lifetime;
#if !(defined(__FreeBSD__) && __FreeBSD__ >= 3)
	long time_second = time.tv_sec;
#endif

	/* get current CoA and recover its scope information. */
	hif_coa = sc->hif_coa_ifa->ia_addr;
	if (in6_addr2zoneid(sc->hif_coa_ifa->ia_ifp, &hif_coa.sin6_addr,
	    &hif_coa.sin6_scope_id)) {
		/* must not happen. */
	}
	if (in6_embedscope(&hif_coa.sin6_addr, &hif_coa)) {
		/* must not happen. */
	}

	/* for each BU entry, update COA and make them about to send. */
	for (mbu = LIST_FIRST(&sc->hif_bu_list);
	     mbu;
	     mbu = mbu_next) {
		mbu_next = LIST_NEXT(mbu, mbu_entry);

		if (mbu->mbu_flags & IP6MU_HOME) {
			/* this is a BU for our home agent */
			/*
			 * XXX
			 * must send bu with ack flag to a previous ar.
			 */
			continue;
		}
		if (SA6_ARE_ADDR_EQUAL(&mbu->mbu_coa, &hif_coa)) {
			/* XXX no need */
			continue;
		}
		mbu->mbu_coa = hif_coa;
		coa_lifetime = mip6_coa_get_lifetime(&mbu->mbu_coa.sin6_addr);
		mpfx = mip6_prefix_list_find_withhaddr(&mip6_prefix_list,
						       &mbu->mbu_haddr);
		if (mpfx == NULL) {
			mip6log((LOG_NOTICE,
				 "%s:%d: expired prefix (%s).\n",
				 __FILE__, __LINE__,
				 ip6_sprintf(&mbu->mbu_haddr.sin6_addr)));
			mip6_bu_list_remove(&sc->hif_bu_list, mbu);
			continue;
		}
		if (coa_lifetime < mpfx->mpfx_vltime) {
			mbu->mbu_lifetime = coa_lifetime;
		} else {
			mbu->mbu_lifetime = mpfx->mpfx_vltime;
		}
		if (mip6_config.mcfg_bu_maxlifetime > 0 &&
		    mbu->mbu_lifetime > mip6_config.mcfg_bu_maxlifetime)
			mbu->mbu_lifetime = mip6_config.mcfg_bu_maxlifetime;
		mbu->mbu_expire = time_second + mbu->mbu_lifetime;
		/* sanity check for overflow */
		if (mbu->mbu_expire < time_second)
			mbu->mbu_expire = 0x7fffffff;
		mbu->mbu_refresh = mbu->mbu_lifetime;
		if (mip6_bu_fsm(mbu,
			(home ?
			 MIP6_BU_PRI_FSM_EVENT_RETURNING_HOME : 
			 MIP6_BU_PRI_FSM_EVENT_MOVEMENT), NULL) != 0) {
			mip6log((LOG_ERR,
			    "%s:%d: "
			    "state transition failed.\n",
			    __FILE__, __LINE__));
		}
	}

	return (0);
}

static void
mip6_bu_update_firewallstate(mbu)
	struct mip6_bu *mbu;
{
	int coa_is_in, paddr_is_in;
	struct hif_site_prefix *hsp;

	if ((mbu->mbu_flags & IP6MU_HOME) != 0)
		return;

	coa_is_in = paddr_is_in = 0;
	for (hsp = LIST_FIRST(&mbu->mbu_hif->hif_sp_list); hsp;
	     hsp = LIST_NEXT(hsp, hsp_entry)) {
		if (in6_are_prefix_equal(&hsp->hsp_prefix.sin6_addr,
			&mbu->mbu_paddr.sin6_addr, hsp->hsp_prefixlen))
			paddr_is_in = 1;
		if (in6_are_prefix_equal(&hsp->hsp_prefix.sin6_addr,
			&mbu->mbu_coa.sin6_addr, hsp->hsp_prefixlen))
			coa_is_in = 1;
	}
	if (((coa_is_in == 0) && (paddr_is_in == 0))
	    || ((coa_is_in != 0) && (paddr_is_in != 0))) 
		mbu->mbu_state &= ~MIP6_BU_STATE_FIREWALLED;
	else
		mbu->mbu_state |= MIP6_BU_STATE_FIREWALLED;
}

static void
mip6_bu_list_update_firewallstate(sc)
	struct hif_softc *sc;
{
	struct mip6_bu *mbu;


	for (mbu = LIST_FIRST(&sc->hif_bu_list); mbu;
	     mbu = LIST_NEXT(mbu, mbu_entry)) {
		mip6_bu_update_firewallstate(mbu);
	}
}

static int64_t
mip6_coa_get_lifetime(coa)
	struct in6_addr *coa;
{
	struct in6_ifaddr *ia;
	int64_t lifetime;
#if !(defined(__FreeBSD__) && __FreeBSD__ >= 3)
	long time_second = time.tv_sec;
#endif

	for (ia = in6_ifaddr; ia; ia = ia->ia_next) {
		if (IN6_ARE_ADDR_EQUAL(coa, &ia->ia_addr.sin6_addr))
			break;
	}

	if (ia != NULL) {
		lifetime = ia->ia6_lifetime.ia6t_expire - time_second;
	} else {
		lifetime = 0;
	}

	return (lifetime);
}

int
mip6_bu_send_hoti(mbu)
	struct mip6_bu *mbu;
{
	struct mbuf *m;
	struct ip6_pktopts opt;
	int error = 0;

	init_ip6pktopts(&opt);

	m = mip6_create_ip6hdr(&mbu->mbu_haddr, &mbu->mbu_paddr,
	    IPPROTO_NONE, 0);
	if (m == NULL) {
		mip6log((LOG_ERR,
		    "%s:%d: creating ip6hdr failed.\n",
		    __FILE__, __LINE__));
		return (ENOMEM);
	}

	error = mip6_ip6mhi_create(&opt.ip6po_mobility, mbu);
	if (error) {
		mip6log((LOG_ERR,
		    "%s:%d: HoTI creation error (%d)\n",
		    __FILE__, __LINE__, error));
		m_freem(m);
 		goto free_ip6pktopts;
	}

	mip6stat.mip6s_ohoti++;
	error = ip6_output(m, &opt, NULL, 0, NULL, NULL
#if defined(__FreeBSD__) && __FreeBSD_version >= 480000
			   , NULL
#endif
			  );
	if (error) {
		mip6log((LOG_ERR,
		    "%s:%d: sending ip packet error. (%d)\n",
		    __FILE__, __LINE__, error));
 		goto free_ip6pktopts;
	}

 free_ip6pktopts:
	if (opt.ip6po_mobility)
		FREE(opt.ip6po_mobility, M_IP6OPT);

	return (0);
}

int
mip6_bu_send_coti(mbu)
	struct mip6_bu *mbu;
{
	struct mbuf *m;
	struct ip6_pktopts opt;
	int error = 0;

	init_ip6pktopts(&opt);
	opt.ip6po_flags |= IP6PO_USECOA;

	m = mip6_create_ip6hdr(&mbu->mbu_coa, &mbu->mbu_paddr,
	    IPPROTO_NONE, 0);
	if (m == NULL) {
		mip6log((LOG_ERR,
		    "%s:%d: creating ip6hdr failed.\n",
		    __FILE__, __LINE__));
		return (ENOMEM);
	}

	error = mip6_ip6mci_create(&opt.ip6po_mobility, mbu);
	if (error) {
		mip6log((LOG_ERR,
		    "%s:%d: CoTI creation error (%d)\n",
		    __FILE__, __LINE__, error));
		m_freem(m);
 		goto free_ip6pktopts;
	}

	mip6stat.mip6s_ocoti++;
	error = ip6_output(m, &opt, NULL, 0, NULL, NULL
#if defined(__FreeBSD__) && __FreeBSD_version >= 480000
			   , NULL
#endif
			  );
	if (error) {
		mip6log((LOG_ERR,
		    "%s:%d: sending ip packet error. (%d)\n",
		    __FILE__, __LINE__, error));
		goto free_ip6pktopts;
	}

 free_ip6pktopts:
	if (opt.ip6po_mobility)
		FREE(opt.ip6po_mobility, M_IP6OPT);

	return (0);
}

/*
 * Some BUs are sent with IPv6 datagram.  But when we have no traffic to
 * the BU destination, we may have some BUs left in the BU list.  Push
 * them out.
 */
int
mip6_bu_send_bu(mbu)
	struct mip6_bu *mbu;
{
	struct mbuf *m;
	struct ip6_pktopts opt;
	int error;

	/* sanity check. */
	if (mbu == NULL)
		return (EINVAL);

	/* init local variables. */
	error = 0;

	if (IN6_IS_ADDR_UNSPECIFIED(&mbu->mbu_paddr.sin6_addr)) {
		/* we do not know where to send a binding update. */
		if ((mbu->mbu_flags & IP6MU_HOME) != 0) {
			mip6log((LOG_INFO,
			    "%s:%d: "
			    "no home agent.  start DHAAD.\n",
			    __FILE__, __LINE__));
			error = mip6_icmp6_dhaad_req_output(mbu->mbu_hif);
			if (error) {
				mip6log((LOG_ERR,
				    "%s:%d: failed to send DHAAD request.\n",
				    __FILE__, __LINE__));
				/* continue, anyway. */
			}
			/*
			 * a binding update will be sent immediately
			 * after receiving DHAAD reply.
			 */
			goto bu_send_bu_end;
		}
		panic("a peer address must be known when sending a binding update.");
	}

	/* create an ipv6 header to send a binding update. */
	m = mip6_create_ip6hdr(&mbu->mbu_haddr, &mbu->mbu_paddr,
	    IPPROTO_NONE, 0);
	if (m == NULL) {
		mip6log((LOG_ERR,
		    "%s:%d: memory allocation failed.\n",
		    __FILE__, __LINE__));
		error = ENOBUFS;
		goto bu_send_bu_end;
	}

	/* initialize packet options structure. */
	init_ip6pktopts(&opt);

	/* create a binding update mobility header. */
	error = mip6_ip6mu_create(&opt.ip6po_mobility, &mbu->mbu_haddr,
	    &mbu->mbu_paddr, mbu->mbu_hif);
	if (error) {
		mip6log((LOG_ERR,
		    "%s:%d: a binding update mobility header "
		    "creation failed (%d).\n",
		    __FILE__, __LINE__, error));
		m_freem(m);
		goto free_ip6pktopts;
	}

	/* send a binding update. */
	mip6stat.mip6s_obu++;
	if (!ip6_setpktaddrs(m, &mbu->mbu_haddr, &mbu->mbu_paddr)) {
		/* should not happen. */
		m_freem(m);
		error = EINVAL;
		goto free_ip6pktopts;
	}
	error = ip6_output(m, &opt, NULL, 0, NULL, NULL
#if defined(__FreeBSD__) && __FreeBSD_version >= 480000
			   , NULL
#endif
			  );
	if (error) {
		mip6log((LOG_ERR,
			 "%s:%d: ip6_output returns error (%d) "
			 "when sending NULL packet to send BU.\n",
			 __FILE__, __LINE__,
			 error));
		goto free_ip6pktopts;
	}

 free_ip6pktopts:
	if (opt.ip6po_mobility)
		FREE(opt.ip6po_mobility, M_IP6OPT);

 bu_send_bu_end:
	return (error);
}

int
mip6_bu_send_cbu(mbu)
	struct mip6_bu *mbu;
{
	struct mbuf *m;
	struct ip6_pktopts opt;
	int error = 0;

	/* sanity check. */
	if (mbu == NULL)
		return (EINVAL);

	init_ip6pktopts(&opt);

	m = mip6_create_ip6hdr(&mbu->mbu_haddr, &mbu->mbu_paddr, IPPROTO_NONE, 0);
	if (m == NULL) {
		mip6log((LOG_ERR,
		    "%s:%d: creating ip6hdr failed.\n", __FILE__, __LINE__));
		return (ENOMEM);
	}

	error = mip6_ip6mu_create(&opt.ip6po_mobility, &mbu->mbu_haddr,
	    &mbu->mbu_paddr, mbu->mbu_hif);
	if (error) {
		mip6log((LOG_ERR,
		    "%s:%d: a binding update mobility header "
		    "creation failed (%d).\n",
		    __FILE__, __LINE__, error));
		m_freem(m);
		goto free_ip6pktopts;
	}

	mip6stat.mip6s_obu++;
	error = ip6_output(m, &opt, NULL, 0, NULL, NULL
#if defined(__FreeBSD__) && __FreeBSD_version >= 480000
			   , NULL
#endif
			  );
	if (error) {
		mip6log((LOG_ERR,
		    "%s:%d: sending a binding update falied. (%d)\n",
		    __FILE__, __LINE__, error));
 		goto free_ip6pktopts;
	}

 free_ip6pktopts:
	if (opt.ip6po_mobility)
		FREE(opt.ip6po_mobility, M_IP6OPT);

	return (error);
}

static void
mip6_bu_starttimer()
{
#if defined(__NetBSD__) || (defined(__FreeBSD__) && __FreeBSD__ >= 3)
	callout_reset(&mip6_bu_ch,
		      MIP6_BU_TIMEOUT_INTERVAL * hz,
		      mip6_bu_timeout, NULL);
#elif defined(__OpenBSD__)
	timeout_set(&mip6_bu_ch, mip6_bu_timeout, NULL);
	timeout_add(&mip6_bu_ch,
		    MIP6_BU_TIMEOUT_INTERVAL * hz);
#else
	timeout(mip6_bu_timeout, (void *)0,
		MIP6_BU_TIMEOUT_INTERVAL * hz);
#endif
}

static void
mip6_bu_stoptimer()
{
#if defined(__NetBSD__) || (defined(__FreeBSD__) && __FreeBSD__ >= 3)
	callout_stop(&mip6_bu_ch);
#elif defined(__OpenBSD__)
	timeout_del(&mip6_bu_ch);
#else
	untimeout(mip6_bu_timeout, (void *)0);
#endif
}

static void
mip6_bu_timeout(arg)
	void *arg;
{
	int s;
	struct hif_softc *sc;
	int error = 0;
#if defined(__FreeBSD__) && __FreeBSD__ >= 3
	struct timeval mono_time;
#endif

#if defined(__FreeBSD__) && __FreeBSD__ >= 3
	mono_time.tv_sec = time_second;
#endif

#if defined(__NetBSD__) || defined(__OpenBSD__)
	s = splsoftnet();
#else
	s = splnet();
#endif
	mip6_bu_starttimer();

	for (sc = TAILQ_FIRST(&hif_softc_list); sc;
	     sc = TAILQ_NEXT(sc, hif_entry)) {
		struct mip6_bu *mbu, *mbu_entry;

		for (mbu = LIST_FIRST(&sc->hif_bu_list);
		     mbu != NULL;
		     mbu = mbu_entry) {
			mbu_entry = LIST_NEXT(mbu, mbu_entry);

			/* check expiration. */
			if (mbu->mbu_expire < mono_time.tv_sec) {
				if ((mbu->mbu_flags & IP6MU_HOME) != 0) {
					/*
					 * the binding update entry for
					 * the home registration
					 * should not be removed.
					 */
					mip6_bu_fsm(mbu,
					    MIP6_BU_PRI_FSM_EVENT_RETRANS_TIMER,
					    NULL);
				} else {
					error = mip6_bu_list_remove(
						&sc->hif_bu_list, mbu);
					if (error) {
						mip6log((LOG_ERR,
							 "%s:%d: can't remove a binding update entry (0x%p)\n",
							 __FILE__, __LINE__,
							 mbu));
						/* continue anyway... */
					}
					continue;
				}
			}

			/* check if we need retransmit something. */
			if ((mbu->mbu_state & MIP6_BU_STATE_NEEDTUNNEL) != 0)
				continue;

			/* check timeout. */
			if ((mbu->mbu_retrans != 0)
			    && (mbu->mbu_retrans < mono_time.tv_sec)) {
				/* order is important. */
				if(MIP6_IS_BU_RR_STATE(mbu)) {
					/* retransmit RR signals. */
					error = mip6_bu_fsm(mbu,
					    MIP6_BU_SEC_FSM_EVENT_RETRANS_TIMER,
					    NULL);
				} else if (((mbu->mbu_flags & IP6MU_ACK) != 0)
				    && MIP6_IS_BU_WAITA_STATE(mbu)) {
					/* retransmit a binding update
					 * to register. */
					error = mip6_bu_fsm(mbu,
					    MIP6_BU_PRI_FSM_EVENT_RETRANS_TIMER,
					    NULL);
				} else if (MIP6_IS_BU_BOUND_STATE(mbu)) {
					/* retransmit a binding update
					 * for to refresh binding. */
					error = mip6_bu_fsm(mbu,
					    MIP6_BU_PRI_FSM_EVENT_REFRESH_TIMER,
					    NULL);
				}
				if (error) {
					mip6log((LOG_ERR,
					    "%s:%d: "
					    "fsm state transition filed.\n",
					    __FILE__, __LINE__));
					/* continue, anyway... */
				}
			}
		}
	}

	splx(s);
}

int
mip6_haddr_destopt_create(pktopt_haddr, src, dst, sc)
	struct ip6_dest **pktopt_haddr;
	struct sockaddr_in6 *src;
	struct sockaddr_in6 *dst;
	struct hif_softc *sc;
{
	struct sockaddr_in6 hif_coa;
	struct ip6_opt_home_address haddr_opt;
	struct mip6_buffer optbuf;
	struct mip6_bu *mbu;
	struct sockaddr_in6 *coa;

	if (*pktopt_haddr) {
		/* already allocated ? */
		return (0);
	}

	/* get current CoA and recover its scope information. */
	hif_coa = sc->hif_coa_ifa->ia_addr;
	if (in6_addr2zoneid(sc->hif_coa_ifa->ia_ifp, &hif_coa.sin6_addr,
	    &hif_coa.sin6_scope_id)) {
		/* must not happen. */
	}
	if (in6_embedscope(&hif_coa.sin6_addr, &hif_coa)) {
		/* must not happen. */
	}

	bzero(&haddr_opt, sizeof(struct ip6_opt_home_address));
	haddr_opt.ip6oh_type = IP6OPT_HOME_ADDRESS;
	haddr_opt.ip6oh_len = IP6OPT_HALEN;

	mbu = mip6_bu_list_find_withpaddr(&sc->hif_bu_list, dst, src);
	if (mbu && ((mbu->mbu_state & MIP6_BU_STATE_NEEDTUNNEL) != 0)) {
		return (0);
	}
	if (mbu)
		coa = &mbu->mbu_coa;
	else
		coa = &hif_coa;
	bcopy((caddr_t)&coa->sin6_addr, haddr_opt.ip6oh_addr,
	      sizeof(struct in6_addr));

	MALLOC(optbuf.buf, u_int8_t *, MIP6_BUFFER_SIZE, M_IP6OPT, M_NOWAIT);
	if (optbuf.buf == NULL) {
		return (ENOMEM);
	}
	bzero((caddr_t)optbuf.buf, MIP6_BUFFER_SIZE);
	optbuf.off = 2;

	/* Add Home Address option */
	mip6_add_opt2dh((u_int8_t *)&haddr_opt, &optbuf);
	mip6_align_destopt(&optbuf);

	*pktopt_haddr = (struct ip6_dest *)optbuf.buf;

	mip6stat.mip6s_ohao++;

	return (0);
}

int
mip6_mobile_node_exthdr_size(src, dst)
	struct sockaddr_in6 *src;
	struct sockaddr_in6 *dst;
{
	int hdrsiz;
	struct hif_softc *sc;
	struct mip6_bu *mbu;

	if (!MIP6_IS_MN)
		return (0);

	hdrsiz = 0;
	for (sc = TAILQ_FIRST(&hif_softc_list); sc;
	     sc = TAILQ_NEXT(sc, hif_entry)) {
		mbu = mip6_bu_list_find_withpaddr(&sc->hif_bu_list, dst, src);
		if (mbu != NULL) {
			if (MIP6_IS_BU_BOUND_STATE(mbu)) {
				/* a packet will have HAO. */
				/*
				 * XXX this calculation may cause
				 * larger result than actually needed,
				 * if there are some other destination
				 * options.
				 */
				hdrsiz += (((sizeof(struct ip6_dest) + sizeof(struct ip6_opt_home_address)) + 7) / 8) * 8;
			} else {
				/* a packet will be encapsulated. */
				hdrsiz += sizeof(struct ip6_hdr);
			}
			break;
		}
	}

	return (hdrsiz);
}

/*
 ******************************************************************************
 * Function:    mip6_addr_exchange
 * Description: Exchange IPv6 header source address with contents in Home
 *              Address option address field.
 * Ret value:   Void
 ******************************************************************************
 */
int
mip6_addr_exchange(m, dstm)
	struct mbuf *m;    /* includes IPv6 header */
	struct mbuf *dstm; /* includes homeaddress destopt */
{
	struct ip6_opt_home_address *haopt;
	struct ip6_dest *dstopt;
	struct ip6_hdr *ip6;
	struct in6_addr ip6_src;
	u_int8_t *opt;
	int ii, dstoptlen;
	struct m_tag *n;

	/* sanity check */
	if (!MIP6_IS_MN) {
		return (0);
	}

	if (dstm == NULL) {
		/* home address destopt does not exist. */
		return (0);
	}

	/* Find Home Address option */
	dstopt = mtod(dstm, struct ip6_dest *);
	dstoptlen = (dstopt->ip6d_len + 1) << 3;
	if (dstoptlen > dstm->m_len) {
		mip6log((LOG_ERR,
			 "%s:%d: haddr destopt illegal mbuf length.\n",
			 __FILE__, __LINE__));
		return (EINVAL);
	}

	haopt = NULL;
	ii = 2;

	opt = (u_int8_t *)dstopt + ii;
	while (ii < dstoptlen) {
		switch (*opt) {
			case IP6OPT_PAD1:
				ii += 1;
				opt += 1;
				break;
			case IP6OPT_HOME_ADDRESS:
				haopt = (struct ip6_opt_home_address *)opt;
				break;
			default:
				ii += *(opt + 1) + 2;
				opt += *(opt + 1) + 2;
				break;
		}
		if (haopt) break;
	}

	if (haopt == NULL) {
		mip6log((LOG_INFO,
			 "%s:%d: haddr dest opt not found.\n",
			 __FILE__, __LINE__));
		return (0);
	}

	/* Swap the IPv6 homeaddress and the care-of address. */
	ip6 = mtod(m, struct ip6_hdr *);
	bcopy(&ip6->ip6_src, &ip6_src, sizeof(ip6->ip6_src));
	n = ip6_findaux(m);
	if (n) {
		struct ip6aux *ip6a;
		ip6a = (struct ip6aux *) (n + 1);
		/* XXX scope */
		bcopy(haopt->ip6oh_addr, &ip6a->ip6a_src.sin6_addr,
		      sizeof(haopt->ip6oh_addr));
	}
	bcopy(haopt->ip6oh_addr, &ip6->ip6_src, sizeof(haopt->ip6oh_addr));
	bcopy(&ip6_src, haopt->ip6oh_addr, sizeof(ip6_src));

	return (0);
}

/*
 ******************************************************************************
 * Function:    mip6_add_opt2dh
 * Description: Add Binding Update, Binding Acknowledgement, Binding Request
 *              or Home Address option to a Destination Header.  The option
 *              must be aligned when added.
 * Ret value:   Ptr where the MIPv6 option is located in the Destination header
 *              or NULL.
 ******************************************************************************
 */
static caddr_t
mip6_add_opt2dh(opt, dh)
	caddr_t opt;            /* BU, BR, BA or Home Address option */
	struct mip6_buffer *dh; /* Buffer containing the IPv6 DH */
{
	struct ip6_opt_home_address    *ha;
	caddr_t                         pos;
	u_int8_t                        type, len, off;
	int                             rest;

	/* Verify input */
	pos = NULL;
	if (opt == NULL || dh == NULL)
		return pos;
	if (dh->off < 2) {
		bzero((caddr_t)dh->buf, 2);
		dh->off = 2;
	}

	/* Add option to Destination header */
	type = *(u_int8_t*)opt;
	switch (type) {
		case IP6OPT_HOME_ADDRESS:
			/* HA alignment requirement (8n + 6) */
			rest = dh->off % 8;
			if (rest <= 4) {
				/* Add a PADN option with length X */
				len = 6 - rest - 2;
				bzero((caddr_t)dh->buf + dh->off, len + 2);
				*(u_int8_t *)((caddr_t)dh->buf + dh->off++) = IP6OPT_PADN;
				*(u_int8_t *)((caddr_t)dh->buf + dh->off++) = len; /* PADN length */
				dh->off += len;
			} else if (rest == 5) {
				/* Add a PAD1 option */
				*(u_int8_t *)((caddr_t)dh->buf + dh->off++) = IP6OPT_PAD1;
			} else if (rest == 7) {
				/* Add a PADN option with length 5 */
				bzero((caddr_t)dh->buf + dh->off, 5 + 2);
				*(u_int8_t *)((caddr_t)dh->buf + dh->off++) = IP6OPT_PADN;
				*(u_int8_t *)((caddr_t)dh->buf + dh->off++) = 5; /* PADN length */
				dh->off += 5;
			}

			/* Copy option to DH */
			len = IP6OPT_HALEN + IP6OPT_MINLEN;
			off = dh->off;
			ha = (struct ip6_opt_home_address *)opt;

			bzero((caddr_t)dh->buf + off, len);
			bcopy((caddr_t)ha, (caddr_t)dh->buf + off, len);

			pos = dh->buf + off;
			dh->off += len;
			break;
	}
	return pos;
}

/*
 ******************************************************************************
 * Function:    mip6_align_destopt
 * Description: Align a destination header to a multiple of 8 octets.
 * Ret value:   Void
 ******************************************************************************
 */
static void
mip6_align_destopt(buf)
	struct mip6_buffer *buf;     /* IPv6 destination header to align */
{
	struct ip6_ext  *ext_hdr;
	int              rest;     /* Rest of modulo division */
	u_int8_t         padlen;   /* Number of bytes to pad */

	rest = buf->off % 8;

	if (rest == 7) {
		/* Add a PAD1 option */
		bzero((caddr_t)buf->buf + buf->off, 1);
		buf->off += 1;
	} else if (rest > 0 && rest < 7) {
		/* Add a PADN option */
		padlen = 8 - rest;
		bzero((caddr_t)buf->buf + buf->off, padlen);
		*(u_int8_t *)((caddr_t)buf->buf + buf->off) = IP6OPT_PADN;
		*(u_int8_t *)((caddr_t)buf->buf + buf->off + 1) = padlen - 2;
		buf->off += padlen;
	}

	/* Adjust the extension header length */
	ext_hdr = (struct ip6_ext *)buf->buf;
	ext_hdr->ip6e_len = (buf->off >> 3) - 1;
	return;
}

int
mip6_ip6mh_input(m, ip6mh, ip6mhlen)
	struct mbuf *m;
	struct ip6m_home_test *ip6mh;
	int ip6mhlen;
{
	struct sockaddr_in6 src_sa, dst_sa;
	struct hif_softc *sc;
	struct mip6_bu *mbu;
	int error = 0;

	mip6stat.mip6s_hot++;

	if (ip6_getpktaddrs(m, &src_sa, &dst_sa)) {
		/* must not happen. */
		m_freem(m);
		return (EINVAL);
	}

	/* packet length check. */
	if (ip6mhlen < sizeof(struct ip6m_home_test)) {
		mip6log((LOG_NOTICE,
			 "%s:%d: too short home test (len = %d) "
			 "from host %s.\n",
			 __FILE__, __LINE__,
			 ip6mhlen,
			 ip6_sprintf(&src_sa.sin6_addr)));
		ip6stat.ip6s_toosmall++;
		/* send ICMP parameter problem. */
		icmp6_error(m, ICMP6_PARAM_PROB, ICMP6_PARAMPROB_HEADER,
		    (caddr_t)&ip6mh->ip6mh_len - (caddr_t)mtod(m, struct ip6_hdr *));
		return (EINVAL);
	}

	sc = hif_list_find_withhaddr(&dst_sa);
	if (sc == NULL) {
                mip6log((LOG_NOTICE,
		    "%s:%d: no related hif interface found with this HoT "
		    "for %s.\n",
		    __FILE__, __LINE__, ip6_sprintf(&dst_sa.sin6_addr)));
		m_freem(m);
		mip6stat.mip6s_nohif++;
                return (EINVAL);
	}
	mbu = mip6_bu_list_find_withpaddr(&sc->hif_bu_list, &src_sa, &dst_sa);
	if (mbu == NULL) {
		mip6log((LOG_NOTICE,
		    "%s:%d: no related binding update entry found with "
		    "this HoT for %s.\n",
		    __FILE__, __LINE__, ip6_sprintf(&src_sa.sin6_addr)));
		m_freem(m);
		mip6stat.mip6s_nobue++;
		return (EINVAL);
	}

	/* check mobile cookie. */
	if (bcmp(&mbu->mbu_mobile_cookie, ip6mh->ip6mh_cookie,
	    sizeof(ip6mh->ip6mh_cookie)) != 0) {
		mip6log((LOG_INFO,
		    "%s:%d: home init cookie mismatch from %s.\n",
		    __FILE__, __LINE__, ip6_sprintf(&src_sa.sin6_addr)));
		m_freem(m);
		mip6stat.mip6s_hinitcookie++;
		return (EINVAL);
	}

	error = mip6_bu_fsm(mbu, MIP6_BU_SEC_FSM_EVENT_HOT, ip6mh);
	if (error) {
		mip6log((LOG_ERR,
		    "%s:%d: state transition failed. (%d)\n",
		    __FILE__, __LINE__, error));
		m_freem(m);
		return (error);
	}

	mbu->mbu_home_nonce_index = ntohs(ip6mh->ip6mh_nonce_index);
	mip6log((LOG_INFO,
		 "%s:%d: Got HoT Nonce index: %d.\n",
		 __FILE__, __LINE__,mbu->mbu_home_nonce_index));

	return (0);
}

int
mip6_ip6mc_input(m, ip6mc, ip6mclen)
	struct mbuf *m;
	struct ip6m_careof_test *ip6mc;
	int ip6mclen;
{
	struct sockaddr_in6 src_sa, dst_sa;
	struct hif_softc *sc;
	struct mip6_bu *mbu = NULL;
	int error = 0;

	mip6stat.mip6s_cot++;

	if (ip6_getpktaddrs(m, &src_sa, &dst_sa)) {
		/* must not happen. */
		m_freem(m);
		return (EINVAL);
	}

	if (IN6_IS_ADDR_UNSPECIFIED(&src_sa.sin6_addr) ||
	    IN6_IS_ADDR_LOOPBACK(&src_sa.sin6_addr)) {
		m_freem(m);
		return (EINVAL);
	}

	/* packet length check. */
	if (ip6mclen < sizeof(struct ip6m_careof_test)) {
		mip6log((LOG_NOTICE,
			 "%s:%d: too short care-of test (len = %d) "
			 "from host %s.\n",
			 __FILE__, __LINE__,
			 ip6mclen,
			 ip6_sprintf(&src_sa.sin6_addr)));
		ip6stat.ip6s_toosmall++;
		/* send ICMP parameter problem. */
		icmp6_error(m, ICMP6_PARAM_PROB, ICMP6_PARAMPROB_HEADER,
		    (caddr_t)&ip6mc->ip6mc_len - (caddr_t)mtod(m, struct ip6_hdr *));
		return (EINVAL);
	}

	/* too ugly... */
	for (sc = TAILQ_FIRST(&hif_softc_list);
	     sc;
	     sc = TAILQ_NEXT(sc, hif_entry)) {
		for (mbu = LIST_FIRST(&sc->hif_bu_list);
		     mbu;
		     mbu = LIST_NEXT(mbu, mbu_entry)) {
			if (SA6_ARE_ADDR_EQUAL(&dst_sa, &mbu->mbu_coa) &&
			    SA6_ARE_ADDR_EQUAL(&src_sa, &mbu->mbu_paddr))
				break;
		}
		if (mbu != NULL)
			break;
	}
	if (mbu == NULL) {
		mip6log((LOG_NOTICE,
		    "%s:%d: no related binding update entry found with "
		    "this CoT for %s.\n",
		    __FILE__, __LINE__, ip6_sprintf(&src_sa.sin6_addr)));
		m_freem(m);
		mip6stat.mip6s_nobue++;
		return (EINVAL);
	}

	/* check mobile cookie. */
	if (bcmp(&mbu->mbu_mobile_cookie, ip6mc->ip6mc_cookie,
	    sizeof(ip6mc->ip6mc_cookie)) != 0) {
		mip6log((LOG_INFO,
		    "%s:%d: careof init cookie mismatch from %s.\n",
		    __FILE__, __LINE__, ip6_sprintf(&src_sa.sin6_addr)));
		m_freem(m);
		mip6stat.mip6s_cinitcookie++;
		return (EINVAL);
	}

	error = mip6_bu_fsm(mbu, MIP6_BU_SEC_FSM_EVENT_COT, ip6mc);
	if (error) {
		mip6log((LOG_ERR,
		    "%s:%d: state transition failed. (%d)\n",
		    __FILE__, __LINE__, error));
		m_freem(m);
		return (error);
	}

	mbu->mbu_careof_nonce_index = ntohs(ip6mc->ip6mc_nonce_index);
	mip6log((LOG_INFO,
		 "%s:%d: Got CoT Nonce index: %d.\n",
		 __FILE__, __LINE__, mbu->mbu_careof_nonce_index));

	return (0);
}

int
mip6_ip6ma_input(m, ip6ma, ip6malen)
	struct mbuf *m;
	struct ip6m_binding_ack *ip6ma;
	int ip6malen;
{
	struct ip6_hdr *ip6;
	struct sockaddr_in6 src_sa, dst_sa;
	struct hif_softc *sc;
	struct mip6_bu *mbu;
	u_int16_t seqno;
	u_int32_t lifetime, refresh;
#if !(defined(__FreeBSD__) && __FreeBSD__ >= 3)
	long time_second = time.tv_sec;
#endif
	int error = 0;
	struct mip6_mobility_options mopt;
	u_int8_t ba_safe = 0;

	mip6stat.mip6s_ba++;

#ifdef IPSEC
	/*
	 * Check ESP(IPsec)
	 */
	if (ipsec6_in_reject(m, NULL)) {
		ipsec6stat.in_polvio++;
		m_freem(m);
		return (EINVAL);	/* XXX */
	}
#endif /* IPSEC */

	ip6 = mtod(m, struct ip6_hdr *);
	if (ip6_getpktaddrs(m, &src_sa, &dst_sa)) {
		/* must not happen. */
		m_freem(m);
		return (EINVAL);
	}

	/* packet length check. */
	if (ip6malen < sizeof(struct ip6m_binding_ack)) {
		mip6log((LOG_NOTICE,
			 "%s:%d: too short binding ack (len = %d) "
			 "from host %s.\n",
			 __FILE__, __LINE__,
			 ip6malen,
			 ip6_sprintf(&src_sa.sin6_addr)));
		ip6stat.ip6s_toosmall++;
		/* send ICMP parameter problem. */
		icmp6_error(m, ICMP6_PARAM_PROB, ICMP6_PARAMPROB_HEADER,
		    (caddr_t)&ip6ma->ip6ma_len - (caddr_t)ip6);
		return (EINVAL);
	}

	if (((m->m_flags & M_DECRYPTED) != 0)
	    || ((m->m_flags & M_AUTHIPHDR) != 0)) {
		ba_safe = 1;
	}

	if ((error = mip6_get_mobility_options((struct ip6_mobility *)ip6ma,
					       sizeof(*ip6ma),
					       ip6malen, &mopt))) {
		m_freem(m);
		mip6stat.mip6s_invalidopt++;
		return (error);
	}
#ifdef __NetBSD__
{
	char bitmask_buf[128];
	bitmask_snprintf(mopt.valid_options,
		 "\20\5REFRESH\4AUTH\3NONCE\2ALTCOA\1UID",
		 bitmask_buf, sizeof(bitmask_buf));
	mip6log((LOG_INFO, "%s:%d: Mobility options: %s\n", 
			 __FILE__, __LINE__, bitmask_buf));
}
#else
	mip6log((LOG_INFO, "%s:%d: Mobility options: %b\n", 
			 __FILE__, __LINE__, mopt.valid_options,
		 "\20\5REFRESH\4AUTH\3NONCE\2ALTCOA\1UID\n"));
#endif

	mip6stat.mip6s_ba_hist[ip6ma->ip6ma_status]++;

	/*
         * check if the sequence number of the binding update sent ==
         * the sequence number of the binding ack received.
         */
	sc = hif_list_find_withhaddr(&dst_sa);
	if (sc == NULL) {
                /*
                 * if we receive a binding ack before sending binding
                 * updates(!), sc will be NULL.
                 */
                mip6log((LOG_NOTICE,
                         "%s:%d: no hif interface found.\n",
                         __FILE__, __LINE__));
                /* silently ignore. */
		m_freem(m);
                return (EINVAL);
	}
	mbu = mip6_bu_list_find_withpaddr(&sc->hif_bu_list, &src_sa, &dst_sa);
	if (mbu == NULL) {
		mip6log((LOG_NOTICE,
                         "%s:%d: no matching binding update entry found.\n",
                         __FILE__, __LINE__));
                /* silently ignore */
		m_freem(m);
		mip6stat.mip6s_nobue++;
                return (EINVAL);
	}

	if (mopt.valid_options & MOPT_AUTHDATA) {
		/* Check Autheticator */
		u_int8_t key_bm[MIP6_KBM_LEN];
		u_int8_t authdata[MIP6_AUTHENTICATOR_LEN];
		u_int16_t cksum_backup;
		int ignore_co_nonce;
		ignore_co_nonce = SA6_ARE_ADDR_EQUAL(&mbu->mbu_haddr, &mbu->mbu_coa);

		cksum_backup = ip6ma->ip6ma_cksum;
		ip6ma->ip6ma_cksum = 0;
		/* Calculate Kbm */
		mip6_calculate_kbm(&mbu->mbu_home_token,
				   ignore_co_nonce ? NULL : &mbu->mbu_careof_token,
				   key_bm);
		/* Calculate Authenticator */
		mip6_calculate_authenticator(key_bm, authdata,
			&mbu->mbu_coa.sin6_addr, &ip6->ip6_dst,
			(caddr_t)ip6ma, ip6malen,
			(caddr_t)mopt.mopt_auth + 2 - (caddr_t)ip6ma,
			min(MOPT_AUTH_LEN(&mopt) + 2, MIP6_AUTHENTICATOR_LEN));
		ip6ma->ip6ma_cksum = cksum_backup;
		if (bcmp(authdata, mopt.mopt_auth + 2,
			 min(MOPT_AUTH_LEN(&mopt) + 2, MIP6_AUTHENTICATOR_LEN))
			 == 0)
			goto accept_binding_ack;
	}

	if (!mip6_config.mcfg_use_ipsec && (mbu->mbu_flags & IP6MU_HOME)) {
		ba_safe = 1;
		goto accept_binding_ack;
	}

	if (mip6_config.mcfg_use_ipsec
	    && (mbu->mbu_flags & IP6MU_HOME) != 0
	    && ba_safe == 1)
		goto accept_binding_ack;

	if ((mbu->mbu_flags & IP6MU_HOME) == 0) {
		goto accept_binding_ack;
	}

	/* otherwise, discard this packet. */
	m_freem(m);
	mip6stat.mip6s_haopolicy++; /* XXX */
	return (EINVAL);

 accept_binding_ack:

	seqno = htons(ip6ma->ip6ma_seqno);
	if (ip6ma->ip6ma_status == IP6MA_STATUS_SEQNO_TOO_SMALL) {
                /*
                 * our home agent has a greater sequence number in its
                 * binging cache entriy of mine.  we should resent
                 * binding update with greater than the sequence
                 * number of the binding cache already exists in our
                 * home agent.  this binding ack is valid though the
                 * sequence number doesn't match.
                 */
		goto check_mobility_options;
	}

	if (seqno != mbu->mbu_seqno) {
                mip6log((LOG_NOTICE,
                         "%s:%d: unmached sequence no "
                         "(%d recv, %d sent) from host %s.\n",
                         __FILE__, __LINE__,
                         seqno,
                         mbu->mbu_seqno,
                         ip6_sprintf(&ip6->ip6_src)));
                /* silently ignore. */
                /* discard */
		m_freem(m);
		mip6stat.mip6s_seqno++;
                return (EINVAL);
	}

 check_mobility_options:

	if (!ba_safe) {
		/* XXX authorization */
                mip6log((LOG_NOTICE,
                         "%s:%d: BA authentication not supported\n",
                         __FILE__, __LINE__));
	}

	if (ip6ma->ip6ma_status >= IP6MA_STATUS_ERRORBASE) {
                mip6log((LOG_NOTICE,
                         "%s:%d: a binding update was rejected "
			 "(error code %d).\n",
                         __FILE__, __LINE__, ip6ma->ip6ma_status));
		if (ip6ma->ip6ma_status == IP6MA_STATUS_NOT_HOME_AGENT &&
		    mbu->mbu_flags & IP6MU_HOME &&
		    mbu->mbu_pri_fsm_state == MIP6_BU_PRI_FSM_STATE_WAITA) {
			/* XXX no registration? */
			goto success;
		}
		if (ip6ma->ip6ma_status == IP6MA_STATUS_SEQNO_TOO_SMALL) {
			/* seqno is too small.  adjust it and resend. */
			mbu->mbu_seqno = ntohs(ip6ma->ip6ma_seqno) + 1;
			/* XXX */
			mip6_bu_send_bu(mbu);
			return (0);
		}

                /* sending binding update failed. */
                error = mip6_bu_list_remove(&sc->hif_bu_list, mbu);
                if (error) {
                        mip6log((LOG_ERR,
                                 "%s:%d: can't remove BU.\n",
                                 __FILE__, __LINE__));
			m_freem(m);
                        return (error);
                }
                /* XXX some error recovery process needed. */
                return (0);
        }

 success:
	/*
	 * the binding update has been accepted.
	 */

	/* update lifetime and refresh time. */
	lifetime = htons(ip6ma->ip6ma_lifetime) << 2;	/* units of 4 secs */
	if (lifetime < mbu->mbu_lifetime) {
		mbu->mbu_expire -= (mbu->mbu_lifetime - lifetime);
		if (mbu->mbu_expire < time_second)
			mbu->mbu_expire = time_second;
	}
	/* binding refresh advice option */
	if ((mbu->mbu_flags & IP6MU_HOME) &&
	    (mopt.valid_options & MOPT_REFRESH)) {
		refresh = mopt.mopt_refresh << 2;
		if (refresh > lifetime || refresh == 0)
			refresh = lifetime;
	}
	else
		refresh = lifetime;
	mbu->mbu_refresh = refresh;

        if (mbu->mbu_refresh > mbu->mbu_expire)
                mbu->mbu_refresh = mbu->mbu_expire;

	if (ip6ma->ip6ma_status == IP6MA_STATUS_PREFIX_DISC) {
		/* XXX; Need prefix discovery */
	}

	if (mbu->mbu_flags & IP6MU_HOME) {
		/* this is from our home agent. */
		if (mbu->mbu_pri_fsm_state == MIP6_BU_PRI_FSM_STATE_WAITD) {
			struct sockaddr_in6 daddr; /* XXX */
			struct sockaddr_in6 lladdr;
			struct ifaddr *ifa;

			/* 
			 * home unregsitration has completed.
			 * send an unsolicited neighbor advertisement.
			 */
			if ((ifa = ifa_ifwithaddr((struct sockaddr *)&mbu->mbu_coa))
			    == NULL) {
				mip6log((LOG_ERR,
					 "%s:%d: can't find CoA interface\n",
					 __FILE__, __LINE__));
				m_freem(m);
				return (EINVAL);	/* XXX */
			}

			bzero(&daddr, sizeof(daddr));
			daddr.sin6_family = AF_INET6;
			daddr.sin6_len = sizeof(daddr);
			daddr.sin6_addr = in6addr_linklocal_allnodes;
			if (in6_addr2zoneid(ifa->ifa_ifp, &daddr.sin6_addr,
					    &daddr.sin6_scope_id)) {
				/* XXX: should not happen */
				mip6log((LOG_ERR,
					 "%s:%d: in6_addr2zoneid failed\n",
					 __FILE__, __LINE__));
				m_freem(m);
				return (EIO);
			}
			if ((error = in6_embedscope(&daddr.sin6_addr,
						    &daddr))) {
				/* XXX: should not happen */
				mip6log((LOG_ERR,
					 "%s:%d: in6_embedscope failed\n",
					 __FILE__, __LINE__));
				m_freem(m);
				return (error);
			}

			nd6_na_output(ifa->ifa_ifp, &daddr,
					      &mbu->mbu_haddr,
					      ND_NA_FLAG_OVERRIDE,
					      1, NULL);
			mip6log((LOG_INFO,
			    "%s:%d: send a unsolicited na for %s\n",
			    __FILE__, __LINE__,
			    ip6_sprintf(&mbu->mbu_haddr.sin6_addr)));

			/*
			 * if the binding update entry has the L flag on,
			 * send unsolicited neighbor advertisement to my
			 * link-local address.
			 */
			if (mbu->mbu_flags & IP6MU_LINK) {
				bzero(&lladdr, sizeof(lladdr));
				lladdr.sin6_len = sizeof(lladdr);
				lladdr.sin6_family = AF_INET6;
				lladdr.sin6_addr.s6_addr16[0]
				    = IPV6_ADDR_INT16_ULL;
				lladdr.sin6_addr.s6_addr32[2]
				    = mbu->mbu_haddr.sin6_addr.s6_addr32[2];
				lladdr.sin6_addr.s6_addr32[3]
				    = mbu->mbu_haddr.sin6_addr.s6_addr32[3];
				
				if (in6_addr2zoneid(ifa->ifa_ifp,
					&lladdr.sin6_addr,
					&lladdr.sin6_scope_id)) {
					/* XXX: should not happen */
					mip6log((LOG_ERR,
					    "%s:%d: in6_addr2zoneid failed\n",
					    __FILE__, __LINE__));
					m_freem(m);
					return (EIO);
				}
				if ((error = in6_embedscope(&lladdr.sin6_addr,
					 &lladdr))) {
					/* XXX: should not happen */
					mip6log((LOG_ERR,
					    "%s:%d: in6_embedscope failed\n",
					    __FILE__, __LINE__));
					m_freem(m);
					return (error);
				}

				nd6_na_output(ifa->ifa_ifp, &daddr,
				    &lladdr, ND_NA_FLAG_OVERRIDE, 1,
				    NULL);

				mip6log((LOG_INFO,
				    "%s:%d: send a unsolicited na for %s\n",
				    __FILE__, __LINE__,
				    ip6_sprintf(&lladdr.sin6_addr)));
			}

			/* notify all the CNs that we are home. */
			error = mip6_bu_list_notify_binding_change(sc, 1);
			if (error) {
				mip6log((LOG_ERR,
					 "%s:%d: removing the bining cache entries of all CNs failed.\n",
					 __FILE__, __LINE__));
				m_freem(m);
				return (error);
			}

			/* remove a tunnel to our homeagent. */
			error = mip6_tunnel_control(MIP6_TUNNEL_DELETE,
						   mbu,
						   mip6_bu_encapcheck,
						   &mbu->mbu_encap);
			if (error) {
				mip6log((LOG_ERR,
					 "%s:%d: tunnel removal failed.\n",
					 __FILE__, __LINE__));
				m_freem(m);
				return (error);
			}
			error = mip6_bu_list_remove_all(&sc->hif_bu_list, 0);
			if (error) {
				mip6log((LOG_ERR,
					 "%s:%d: BU remove all failed.\n",
					 __FILE__, __LINE__));
				m_freem(m);
				return (error);
			}
			mbu = NULL; /* free in mip6_bu_list_remove_all() */

		} else if ((mbu->mbu_pri_fsm_state
			   == MIP6_BU_PRI_FSM_STATE_WAITA)
			   || (mbu->mbu_pri_fsm_state
			   == MIP6_BU_PRI_FSM_STATE_WAITAR)) {
			if (lifetime == 0) {
				mip6log((LOG_WARNING,
					 "%s:%d: lifetime are zero.\n",
					 __FILE__, __LINE__));
				/* XXX ignored */
			}
			/* home registration completed */
			error = mip6_bu_fsm(mbu, MIP6_BU_PRI_FSM_EVENT_BA, NULL);
			/* create tunnel to HA */
			error = mip6_tunnel_control(MIP6_TUNNEL_CHANGE,
						    mbu,
						    mip6_bu_encapcheck,
						    &mbu->mbu_encap);
			if (error) {
				mip6log((LOG_ERR,
					 "%s:%d: tunnel move failed.\n",
					 __FILE__, __LINE__));
				m_freem(m);
				return (error);
			}

			/* notify all the CNs that we have a new coa. */
			error = mip6_bu_list_notify_binding_change(sc, 0);
			if (error) {
				mip6log((LOG_ERR,
					 "%s:%d: updating the bining cache entries of all CNs failed.\n",
					 __FILE__, __LINE__));
				m_freem(m);
				return (error);
			}
		} else if (MIP6_IS_BU_BOUND_STATE(mbu)) {
			/* nothing to do. */
		} else {
			mip6log((LOG_NOTICE,
				 "%s:%d: unexpected condition.\n",
				 __FILE__, __LINE__));
		}
	}

	return (0);
}

int
mip6_ip6mr_input(m, ip6mr, ip6mrlen)
	struct mbuf *m;
	struct ip6m_binding_request *ip6mr;
	int ip6mrlen;
{
	struct sockaddr_in6 src_sa, dst_sa;
	struct hif_softc *sc;
	struct mip6_bu *mbu;
	int error;

	mip6stat.mip6s_br++;

	/* get packet source and destination addresses. */
	if (ip6_getpktaddrs(m, &src_sa, &dst_sa)) {
		/* must not happen. */
		goto bad;
	}

	/* packet length check. */
	if (ip6mrlen < sizeof (struct ip6m_binding_request)) {
		mip6log((LOG_NOTICE,
		    "%s:%d: too short binding request (len = %d) "
		    "from host %s.\n",
		    __FILE__, __LINE__,
		    ip6mrlen, ip6_sprintf(&src_sa.sin6_addr)));
		ip6stat.ip6s_toosmall++;
		/* send ICMP parameter problem. */
		icmp6_error(m, ICMP6_PARAM_PROB, ICMP6_PARAMPROB_HEADER,
		    (caddr_t)&ip6mr->ip6mr_len - (caddr_t)mtod(m, struct ip6_hdr *));
		return(EINVAL);
	}

	/* find hif corresponding to the home address. */
	sc = hif_list_find_withhaddr(&dst_sa);
	if (sc == NULL) {
		/* we have no such home address. */
		mip6stat.mip6s_nohif++;
		goto bad;
	}

	/* find a corresponding binding update entry. */
	mbu = mip6_bu_list_find_withpaddr(&sc->hif_bu_list, &src_sa, &dst_sa);
	if (mbu == NULL) {
		/* we have no binding update entry for dst_sa. */
		return (0);
	}

	error = mip6_bu_fsm(mbu, MIP6_BU_PRI_FSM_EVENT_BRR, ip6mr);
	if (error) {
		mip6log((LOG_ERR,
		    "%s:%d: state transition failed. (%d)\n",
		    __FILE__, __LINE__, error));
		goto bad;
	}

	return (0);
 bad:
	m_freem(m);
	return (EINVAL);
}

int
mip6_ip6me_input(m, ip6me, ip6melen)
	struct mbuf *m;
	struct ip6m_binding_error *ip6me;
	int ip6melen;
{
	struct sockaddr_in6 src_sa;
	struct sockaddr_in6 hoa;
	u_int32_t hoazone;
	struct hif_softc *sc;
	struct mip6_bu *mbu;
	int error = 0;

	mip6stat.mip6s_be++;

	/* get packet source and destination addresses. */
	if (ip6_getpktaddrs(m, &src_sa, NULL)) {
		/* must not happen. */
		goto bad;
	}

	/* packet length check. */
	if (ip6melen < sizeof (struct ip6m_binding_error)) {
		mip6log((LOG_NOTICE,
		    "%s:%d: too short binding error (len = %d) "
		    "from host %s.\n",
		    __FILE__, __LINE__,
		    ip6melen, ip6_sprintf(&src_sa.sin6_addr)));
		ip6stat.ip6s_toosmall++;
		/* send ICMP parameter problem. */
		icmp6_error(m, ICMP6_PARAM_PROB, ICMP6_PARAMPROB_HEADER,
		    (caddr_t)&ip6me->ip6me_len - (caddr_t)mtod(m, struct ip6_hdr *));
		return(EINVAL);
	}

	/* extract the home address of the sending node. */
	bzero (&hoa, sizeof (hoa));
	hoa.sin6_len = sizeof (hoa);
	hoa.sin6_family = AF_INET6;
	bcopy(&ip6me->ip6me_addr, &hoa.sin6_addr,
	    sizeof(struct in6_addr));
	if (in6_addr2zoneid(m->m_pkthdr.rcvif, &hoa.sin6_addr, &hoazone)) {
		ip6stat.ip6s_badscope++;
		goto bad;
	}
	hoa.sin6_scope_id = hoazone;
	if (in6_embedscope(&hoa.sin6_addr, &hoa)) {
		ip6stat.ip6s_badscope++;
		goto bad;
	}

	/* find hif corresponding to the home address. */
	sc = hif_list_find_withhaddr(&hoa);
	if (sc == NULL) {
		/* we have no such home address. */
		mip6stat.mip6s_nohif++;
		goto bad;
	}

	/* find the corresponding binding update entry. */
	mip6stat.mip6s_be_hist[ip6me->ip6me_status]++;
	switch (ip6me->ip6me_status) {
	case IP6ME_STATUS_UNKNOWN_BINDING:
	case IP6ME_STATUS_UNRECOGNIZED_TYPE:
		mbu = mip6_bu_list_find_withpaddr(&sc->hif_bu_list,
		    &src_sa, &hoa);
		if (mbu == NULL) {
			/* we have no binding update entry for the CN. */
			goto bad;
		}
		break;

	default:
		mip6log((LOG_INFO,
		    "%s:%d: unknown BE status code (status = %u) "
		    "from host %s.\n",
		    __FILE__, __LINE__,
		    ip6me->ip6me_status, ip6_sprintf(&src_sa.sin6_addr)));
		goto bad;
		break;
	}

	switch (ip6me->ip6me_status) {
	case IP6ME_STATUS_UNKNOWN_BINDING:
		/* the CN doesn't have a binding cache entry.  start RR. */
		error = mip6_bu_fsm(mbu,
		    MIP6_BU_PRI_FSM_EVENT_UNVERIFIED_HAO, ip6me);
		if (error) {
			mip6log((LOG_ERR,
			    "%s:%d: state transition failed. (%d)\n",
			    __FILE__, __LINE__, error));
			goto bad;
		}

		break;

	case IP6ME_STATUS_UNRECOGNIZED_TYPE:
		/* XXX future extension? */
		error = mip6_bu_fsm(mbu,
		    MIP6_BU_PRI_FSM_EVENT_UNKNOWN_MH_TYPE, ip6me);
		if (error) {
			mip6log((LOG_ERR,
			    "%s:%d: state transition failed. (%d)\n",
			    __FILE__, __LINE__, error));
			goto bad;
		}

		break;

	default:
		mip6log((LOG_INFO,
		    "%s:%d: unknown BE status code (status = %u) "
		    "from host %s.\n",
		    __FILE__, __LINE__,
		    ip6me->ip6me_status, ip6_sprintf(&src_sa.sin6_addr)));

		/* XXX what to do? */
	}

	return (0);

 bad:
	m_freem(m);
	return (EINVAL);
}

int
mip6_ip6mhi_create(pktopt_mobility, mbu)
	struct ip6_mobility **pktopt_mobility;
	struct mip6_bu *mbu;
{
	struct ip6m_home_test_init *ip6mhi;
	int ip6mhi_size;

	/* sanity check. */
	if (mbu == NULL)
		return (EINVAL);

	*pktopt_mobility = NULL;

	ip6mhi_size =
	    ((sizeof(struct ip6m_home_test_init) +7) >> 3) * 8;

	MALLOC(ip6mhi, struct ip6m_home_test_init *,
	    ip6mhi_size, M_IP6OPT, M_NOWAIT);
	if (ip6mhi == NULL)
		return (ENOMEM);

	bzero(ip6mhi, ip6mhi_size);
	ip6mhi->ip6mhi_pproto = IPPROTO_NONE;
	ip6mhi->ip6mhi_len = (ip6mhi_size >> 3) - 1;
	ip6mhi->ip6mhi_type = IP6M_HOME_TEST_INIT;
	bcopy(mbu->mbu_mobile_cookie, ip6mhi->ip6mhi_cookie,
	      sizeof(ip6mhi->ip6mhi_cookie));

	/* calculate checksum. */
	ip6mhi->ip6mhi_cksum = mip6_cksum(&mbu->mbu_haddr, &mbu->mbu_paddr,
	    ip6mhi_size, IPPROTO_MOBILITY, (char *)ip6mhi);

	*pktopt_mobility = (struct ip6_mobility *)ip6mhi;

	return (0);
}

int
mip6_ip6mci_create(pktopt_mobility, mbu)
	struct ip6_mobility **pktopt_mobility;
	struct mip6_bu *mbu;
{
	struct ip6m_careof_test_init *ip6mci;
	int ip6mci_size;

	/* sanity check. */
	if (mbu == NULL)
		return (EINVAL);

	*pktopt_mobility = NULL;

	ip6mci_size =
	    ((sizeof(struct ip6m_careof_test_init) + 7) >> 3) * 8;

	MALLOC(ip6mci, struct ip6m_careof_test_init *,
	    ip6mci_size, M_IP6OPT, M_NOWAIT);
	if (ip6mci == NULL)
		return (ENOMEM);

	bzero(ip6mci, ip6mci_size);
	ip6mci->ip6mci_pproto = IPPROTO_NONE;
	ip6mci->ip6mci_len = (ip6mci_size >> 3) - 1;
	ip6mci->ip6mci_type = IP6M_CAREOF_TEST_INIT;
	bcopy(mbu->mbu_mobile_cookie, ip6mci->ip6mci_cookie,
	      sizeof(ip6mci->ip6mci_cookie));

	/* calculate checksum. */
	ip6mci->ip6mci_cksum = mip6_cksum(&mbu->mbu_coa, &mbu->mbu_paddr,
	    ip6mci_size, IPPROTO_MOBILITY, (char *)ip6mci);

	*pktopt_mobility = (struct ip6_mobility *)ip6mci;

	return (0);
}

int
mip6_ip6mu_create(pktopt_mobility, src, dst, sc)
	struct ip6_mobility **pktopt_mobility;
	struct sockaddr_in6 *src, *dst;
	struct hif_softc *sc;
{
	struct ip6m_binding_update *ip6mu;
	struct ip6m_opt_nonce *mopt_nonce = NULL;
	struct ip6m_opt_authdata *mopt_auth = NULL;
	struct ip6m_opt_altcoa *mopt_altcoa = NULL;
	struct sockaddr_in6 altcoa;
	int ip6mu_size, pad;
	int bu_size = 0, nonce_size = 0, auth_size = 0, altcoa_size = 0;
	struct mip6_bu *mbu, *hrmbu;
	int need_rr = 0, ignore_co_nonce = 0;
	u_int8_t key_bm[MIP6_KBM_LEN]; /* Stated as 'Kbm' in the spec */
#if !(defined(__FreeBSD__) && __FreeBSD__ >= 3)
	long time_second = time.tv_sec;
#endif
	*pktopt_mobility = NULL;

	mbu = mip6_bu_list_find_withpaddr(&sc->hif_bu_list, dst, src);
	hrmbu = mip6_bu_list_find_home_registration(&sc->hif_bu_list, src);
	if ((mbu == NULL) &&
	    (hrmbu != NULL) &&
	    (MIP6_IS_BU_BOUND_STATE(hrmbu))) {
		/* XXX */
		/* create a binding update entry and send CoTI/HoTI. */
		return (0);
	}
	if (mbu == NULL) {
		/*
		 * this is the case that the home registration is on
		 * going.  that is, (mbu == NULL) && (hrmbu != NULL)
		 * but hrmbu->mbu_fsm_state != STATE_REG.
		 */
		return (0);
	}
	if ((mbu->mbu_state & MIP6_BU_STATE_NEEDTUNNEL) != 0) {
		return (0);
	}
	if (SA6_IS_ADDR_UNSPECIFIED(&mbu->mbu_paddr)) {
		/*
		 * the peer addr is unspecified.  this happens when
		 * home registration occurs but no home agent address
		 * is known.
		 */
		mip6log((LOG_INFO,
			 "%s:%d: the peer addr is unspecified.\n",
			 __FILE__, __LINE__));
		mip6_icmp6_dhaad_req_output(sc);
		return (0);
	}

	if (!(mbu->mbu_flags & IP6MU_HOME)) {
		need_rr = 1;
	}

	bu_size = sizeof(struct ip6m_binding_update);
	if (need_rr) {
		/*
		  |<- bu_size -> <- nonce_size -> <- auth_size ->
		  +-------------+----------------+---------------+
		  |  bind. up.  |   nonce opt.   |   auth. opt.  |
		  +-------------+----------------+---------------+
		   <------->
		   sizeof(struct ip6m_binding_update)
		            <-->
			  Padding for nonce opt. alignment
		 */
		bu_size += MIP6_PADLEN(bu_size, 2, 0);
		nonce_size = sizeof(struct ip6m_opt_nonce);
		nonce_size += MIP6_PADLEN(bu_size + nonce_size, 8, 2);
		/* (6.2.7)
		   The Binding Authorization Data option does not
		   have alignment requirements as such.  However,
		   since this option must be the last mobility option,
		   an implicit alignment requirement is 8n + 2. 
		*/
		auth_size = IP6MOPT_AUTHDATA_SIZE;
		auth_size += MIP6_PADLEN(bu_size + nonce_size + auth_size, 8, 0);
#ifdef RR_DBG
printf("MN: bu_size = %d, nonce_size= %d, auth_size = %d(AUTHSIZE:%d)\n", bu_size, nonce_size, auth_size, IP6MOPT_AUTHDATA_SIZE);
#endif
		altcoa_size = 0;
	} else {
		bu_size += MIP6_PADLEN(bu_size, 8, 6);
		altcoa_size = sizeof(struct ip6m_opt_altcoa);
		nonce_size = auth_size = 0;
	}
	ip6mu_size = bu_size + nonce_size + auth_size + altcoa_size;

	MALLOC(ip6mu, struct ip6m_binding_update *,
	       ip6mu_size, M_IP6OPT, M_NOWAIT);
	if (ip6mu == NULL)
		return (ENOMEM);

	if (need_rr) {
		mopt_nonce = (struct ip6m_opt_nonce *)((u_int8_t *)ip6mu + bu_size);
		mopt_auth = (struct ip6m_opt_authdata *)((u_int8_t *)mopt_nonce + nonce_size);
	} else {
		mopt_altcoa = (struct ip6m_opt_altcoa *)((u_int8_t *)ip6mu + bu_size);
	}

	/* update sequence number of this binding update entry. */
	mbu->mbu_seqno++;

	bzero(ip6mu, ip6mu_size);

	ip6mu->ip6mu_pproto = IPPROTO_NONE;
	ip6mu->ip6mu_len = (ip6mu_size >> 3) - 1;
	ip6mu->ip6mu_type = IP6M_BINDING_UPDATE;
	ip6mu->ip6mu_flags = mbu->mbu_flags;
	ip6mu->ip6mu_seqno = htons(mbu->mbu_seqno);
	if (SA6_ARE_ADDR_EQUAL(&mbu->mbu_haddr, &mbu->mbu_coa)) {
		/* this binding update is for home un-registration. */
		ip6mu->ip6mu_lifetime = 0;
		if (need_rr) {
			ignore_co_nonce = 1;
		}
	} else {
		struct mip6_prefix *mpfx;
		u_int32_t haddr_lifetime, coa_lifetime, lifetime;

		mpfx = mip6_prefix_list_find_withhaddr(&mip6_prefix_list,
						       src);
		haddr_lifetime = mpfx->mpfx_vltime;
		coa_lifetime = mip6_coa_get_lifetime(&mbu->mbu_coa.sin6_addr);
		lifetime = haddr_lifetime < coa_lifetime ?
			haddr_lifetime : coa_lifetime;
		if ((mbu->mbu_flags & IP6MU_HOME) == 0) {
			if (mip6_config.mcfg_bu_maxlifetime > 0 &&
			    lifetime > mip6_config.mcfg_bu_maxlifetime)
				lifetime = mip6_config.mcfg_bu_maxlifetime;
		} else {
			if (mip6_config.mcfg_hrbu_maxlifetime > 0 &&
			    lifetime > mip6_config.mcfg_hrbu_maxlifetime)
				lifetime = mip6_config.mcfg_hrbu_maxlifetime;
		}
		mbu->mbu_lifetime = lifetime;
		mbu->mbu_expire = time_second + mbu->mbu_lifetime;
		mbu->mbu_refresh = mbu->mbu_lifetime;
		ip6mu->ip6mu_lifetime =
		    htons((u_int16_t)(mbu->mbu_lifetime >> 2));	/* units 4 secs */
	}

	if ((pad = bu_size - sizeof(struct ip6m_binding_update)) >= 2) {
		u_char *p =
			(u_int8_t *)ip6mu + sizeof(struct ip6m_binding_update);
		*p = IP6MOPT_PADN;
		*(p + 1) = pad - 2;
	}

	if (need_rr) {
		/* nonce indices and authdata insertion. */
		if (nonce_size) {
			if ((pad = nonce_size - sizeof(struct ip6m_opt_nonce))
			    >= 2) {
				u_char *p = (u_int8_t *)ip6mu + bu_size
				    + sizeof(struct ip6m_opt_nonce);
				*p = IP6MOPT_PADN;
				*(p + 1) = pad - 2;
			}
		}
		if (auth_size) {
			if ((pad = auth_size - IP6MOPT_AUTHDATA_SIZE) >= 2) {
				u_char *p = (u_int8_t *)ip6mu
				    + bu_size + nonce_size + IP6MOPT_AUTHDATA_SIZE;
				*p = IP6MOPT_PADN;
				*(p + 1) = pad - 2;
			}
		}

		/* Nonce Indicies */
		mopt_nonce->ip6mon_type = IP6MOPT_NONCE;
		mopt_nonce->ip6mon_len = sizeof(struct ip6m_opt_nonce) - 2;
		SET_NETVAL_S(&mopt_nonce->ip6mon_home_nonce_index,
			     mbu->mbu_home_nonce_index);
		if (!ignore_co_nonce) {
			SET_NETVAL_S(&mopt_nonce->ip6mon_careof_nonce_index,
				     mbu->mbu_careof_nonce_index);
		}

		/* Auth. data */
		mopt_auth->ip6moau_type = IP6MOPT_AUTHDATA;
		mopt_auth->ip6moau_len = IP6MOPT_AUTHDATA_SIZE - 2;

		if (auth_size > IP6MOPT_AUTHDATA_SIZE) {
			*((u_int8_t *)ip6mu + bu_size + nonce_size + IP6MOPT_AUTHDATA_SIZE)
			    = IP6MOPT_PADN;
			*((u_int8_t *)ip6mu + bu_size + nonce_size + IP6MOPT_AUTHDATA_SIZE + 1)
			    = auth_size - IP6MOPT_AUTHDATA_SIZE - 2;
		}

#ifdef RR_DBG
mip6_hexdump("MN: Home keygen token: ", sizeof(mbu->mbu_home_token), (caddr_t)&mbu->mbu_home_token);
mip6_hexdump("MN: Care-of keygen token: ", sizeof(mbu->mbu_careof_token), (caddr_t)&mbu->mbu_careof_token);
#endif
		/* Calculate Kbm */
		mip6_calculate_kbm(&mbu->mbu_home_token,
				   ignore_co_nonce ? NULL : &mbu->mbu_careof_token,
				   key_bm);
#ifdef RR_DBG
mip6_hexdump("MN: Kbm: ", sizeof(key_bm), key_bm);
#endif

		/* Calculate authenticator (5.2.6) */
		/* First(96, HMAC_SHA1(Kbm, (coa, | cn | BU))) */
		mip6_calculate_authenticator(key_bm, (u_int8_t *)(mopt_auth + 1), 
			&mbu->mbu_coa.sin6_addr, &dst->sin6_addr, 
			(caddr_t)ip6mu, bu_size + nonce_size + auth_size, 
			bu_size + nonce_size + sizeof(struct ip6m_opt_authdata) ,
			MIP6_AUTHENTICATOR_LEN);
#ifdef RR_DBG
mip6_hexdump("MN: Authenticator: ", (u_int8_t *)(mopt_auth + 1), MIP6_AUTHENTICATOR_LEN);
#endif
	} else {
		if (altcoa_size) {
			if ((pad = altcoa_size
			    - sizeof(struct ip6m_opt_altcoa)) >= 2) {
				u_char *p = (u_int8_t *)ip6mu + bu_size
				    + sizeof(struct ip6m_opt_nonce);
				*p = IP6MOPT_PADN;
				*(p + 1) = pad - 2;
			}
		}
		mopt_altcoa->ip6moa_type = IP6MOPT_ALTCOA;
		mopt_altcoa->ip6moa_len = sizeof(struct ip6m_opt_altcoa) - 2;
		altcoa = mbu->mbu_coa;
		in6_clearscope(&altcoa.sin6_addr);
		bcopy(&altcoa.sin6_addr, mopt_altcoa->ip6moa_addr,
		    sizeof(struct in6_addr));
	}

	/* calculate checksum. */
	ip6mu->ip6mu_cksum = mip6_cksum(&mbu->mbu_haddr, dst, ip6mu_size,
					IPPROTO_MOBILITY, (char *)ip6mu);

	*pktopt_mobility = (struct ip6_mobility *)ip6mu;

	return (0);
}

#if 0 /* mip6_bdt_xxx are not used any more. */
static int
mip6_bdt_create(sc, paddr)
	struct hif_softc *sc;
	struct sockaddr_in6 *paddr;
{
	struct ifnet *ifp;
	struct ifaddr *ifa;
	struct rt_addrinfo rti;
	struct sockaddr_in6 dst, mask;
	struct rtentry *retrt;
	int error = 0;

	if ((sc == NULL) || (paddr == NULL))
		return (EINVAL);

	(void)mip6_bdt_delete(paddr);

	ifp = &sc->hif_if;
	dst = *paddr;

	/* search for a link-local addr */
	ifa = (struct ifaddr *)in6ifa_ifpforlinklocal(ifp,
	    IN6_IFF_NOTREADY | IN6_IFF_ANYCAST);
	if (ifa == NULL) {
		/* XXX: freebsd does not have ifa_ifwithaf */
#if defined(__bsdi__) || (defined(__FreeBSD__) && __FreeBSD__ < 3)
		for (ifa = ifp->if_addrlist; ifa; ifa = ifa->ifa_next)
#elif defined(__FreeBSD__) && __FreeBSD__ >= 4
		TAILQ_FOREACH(ifa, &ifp->if_addrlist, ifa_list)
#else
		for (ifa = ifp->if_addrlist.tqh_first;
		     ifa;
		     ifa = ifa->ifa_list.tqe_next)
#endif
		{
			if (ifa->ifa_addr->sa_family == AF_INET6)
				break;
		}
		/* should we care about ia6_flags? */
	}
	if (ifa == NULL) {
		mip6log((LOG_ERR,
		    "%s:%d: no associated address to this if(%s)\n",
		    __FILE__, __LINE__, if_name(ifp)));
		return (EINVAL);
	}

        bzero(&mask, sizeof(mask));
        mask.sin6_len = sizeof(struct sockaddr_in6);
        mask.sin6_family = AF_INET6;
        mask.sin6_addr = in6mask128;

	bzero((caddr_t)&rti, sizeof(rti));
	rti.rti_flags = RTF_UP|RTF_HOST;
	rti.rti_info[RTAX_DST] = (struct sockaddr *)&dst;
	rti.rti_info[RTAX_GATEWAY] = (struct sockaddr *)ifa->ifa_addr;
	rti.rti_info[RTAX_NETMASK] = (struct sockaddr *)&mask;
	rti.rti_ifp = (struct ifnet *)sc;

#ifndef SCOPEDROUTING
	dst.sin6_scope_id = 0;
	mask.sin6_scope_id = 0;
#endif
	error = rtrequest1(RTM_ADD, &rti, &retrt);
	if (error == 0) {
		retrt->rt_refcnt--;
	}

	return (error);
}

static int
mip6_bdt_delete(paddr)
	struct sockaddr_in6 *paddr;
{
	struct rtentry *rt;
	struct sockaddr_in6 dst;
	int error = 0;

	dst = *paddr;
#ifndef SCOPEDROUTING
	dst.sin6_scope_id = 0;
#endif /* !SCOPEDROUTING */
#ifdef __FreeBSD__
	rt = rtalloc1((struct sockaddr *)&dst, 0, 0UL);
#else
	rt = rtalloc1((struct sockaddr *)&dst, 0);
#endif /* __FreeBSD__ */
	if (rt)
		rt->rt_refcnt--;
	if (rt
	    && ((rt->rt_flags & RTF_HOST) != 0)
	    && (SA6_ARE_ADDR_EQUAL(&dst, (struct sockaddr_in6 *)rt_key(rt)))) {
		error = rtrequest(RTM_DELETE, rt_key(rt),
				  (struct sockaddr *)0,
				  rt_mask(rt), 0, (struct rtentry **)0);
	}

	return (error);
}
#endif /* 0 */

#ifdef MIP6_DEBUG
void
mip6_bu_print(mbu)
	struct mip6_bu *mbu;
{
	mip6log((LOG_INFO,
		 "paddr      %s\n"
		 "haddr      %s\n"
		 "coa        %s\n"
		 "lifetime   %lu\n"
		 "remain     %lld\n"
		 "refresh    %lu\n"
		 "retrans    %lld\n"
		 "seqno      %u\n"
		 "flags      0x%x\n"
		 "state      0x%x\n"
		 "hif        0x%p\n"
		 "pri_fsm    %u\n"
		 "sec_fsm    %u\n",
		 ip6_sprintf(&mbu->mbu_paddr.sin6_addr),
		 ip6_sprintf(&mbu->mbu_haddr.sin6_addr),
		 ip6_sprintf(&mbu->mbu_coa.sin6_addr),
		 (u_long)mbu->mbu_lifetime,
		 (long long)mbu->mbu_expire,
		 (u_long)mbu->mbu_refresh,
		 (long long)mbu->mbu_retrans,
		 mbu->mbu_seqno,
		 mbu->mbu_flags,
		 mbu->mbu_state,
		 mbu->mbu_hif,
		 mbu->mbu_pri_fsm_state,
		 mbu->mbu_sec_fsm_state));

}
#endif /* MIP6_DEBUG */
