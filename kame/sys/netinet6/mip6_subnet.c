/*	$KAME: mip6_subnet.c,v 1.20 2002/02/19 03:40:39 keiichi Exp $	*/

/*
 * Copyright (C) 2001 WIDE Project.  All rights reserved.
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
#include "opt_ipsec.h"
#include "opt_inet6.h"
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
#include <sys/syslog.h>

#if defined(__NetBSD__) || (defined(__FreeBSD__) && __FreeBSD__ >= 3)
#include <sys/callout.h>
#elif defined(__OpenBSD__)
#include <sys/timeout.h>
#endif

#include <net/if.h>
#include <net/if_types.h>
#include <net/route.h>
#include <net/if_dl.h>

#include <net/if_hif.h>

#include <netinet/in.h>
#include <netinet6/in6_var.h>
#include <netinet/ip6.h>
#include <netinet6/ip6_var.h>
#include <netinet/icmp6.h>

#include <netinet6/nd6.h>
#include <netinet6/mip6_var.h>
#include <netinet6/mip6.h>

struct mip6_subnet_list mip6_subnet_list;
static int mip6_subnet_count = 0;

extern struct mip6_prefix_list mip6_prefix_list;

static void mip6_subnet_ha_timeout __P((struct mip6_subnet_ha *));
static void mip6_subnet_prefix_timeout __P((struct mip6_subnet_prefix *,
					    struct mip6_subnet_ha *));
static void mip6_subnet_timeout __P((void *));
static void mip6_subnet_starttimer __P((void));
static void mip6_subnet_stoptimer __P((void));

#ifdef __NetBSD__
struct callout mip6_subnet_ch = CALLOUT_INITIALIZER;
#elif (defined(__FreeBSD__) && __FreeBSD__ >= 3)
struct callout mip6_subnet_ch;
#elif defined(__OpenBSD__)
struct timeout mip6_subnet_ch;
#endif

void
mip6_subnet_init(void)
{
	LIST_INIT(&mip6_subnet_list);
}

struct mip6_subnet *
mip6_subnet_create(void)
{
	struct mip6_subnet *ms;

	MALLOC(ms, struct mip6_subnet *, sizeof(struct mip6_subnet),
	       M_TEMP, M_NOWAIT);
	if (ms == NULL) {
		mip6log((LOG_ERR,
			 "%s:%d: memory allocation failed.\n",
			 __FILE__, __LINE__));
		return (NULL);
	}

	bzero(ms, sizeof(*ms));
	TAILQ_INIT(&ms->ms_mspfx_list);
	TAILQ_INIT(&ms->ms_msha_list);
	
	return (ms);
}

int
mip6_subnet_delete(ms)
	struct mip6_subnet *ms;
{
	struct hif_softc *sc;
	struct hif_subnet *hs;
	struct mip6_subnet_prefix *mspfx;
	struct mip6_subnet_ha *msha;
	int error = 0;

	if (ms == NULL) {
		return (EINVAL);
	}

	while ((mspfx = TAILQ_FIRST(&ms->ms_mspfx_list)) != NULL) {
		TAILQ_REMOVE(&ms->ms_mspfx_list, mspfx, mspfx_entry);
		error = mip6_prefix_list_remove(&mip6_prefix_list,
						mspfx->mspfx_mpfx);
		if (error) {
			return (error);
		}
	}
	while ((msha = TAILQ_FIRST(&ms->ms_msha_list)) != NULL) {
		TAILQ_REMOVE(&ms->ms_msha_list, msha, msha_entry);
		error = mip6_ha_list_remove(&mip6_ha_list, msha->msha_mha);
		if (error) {
			return (error);
		}
	}

	/* remove all hif_subnet that point this mip6_subnet. */
	for (sc = TAILQ_FIRST(&hif_softc_list); sc;
	     sc = TAILQ_NEXT(sc, hif_entry)) {
		for (hs = TAILQ_FIRST(&sc->hif_hs_list_home); hs;
		     hs = TAILQ_NEXT(hs, hs_entry)) {
			if (hs->hs_ms == ms) {
				error = hif_subnet_list_remove(&sc->hif_hs_list_home,
							       hs);
				if (error) {
					mip6log((LOG_ERR,
						 "%s:%d: can't remove hif_subnet (0x%p).\n",
						 __FILE__, __LINE__, hs));
				}
			}
		}
		for (hs = TAILQ_FIRST(&sc->hif_hs_list_foreign); hs;
		     hs = TAILQ_NEXT(hs, hs_entry)) {
			if (hs->hs_ms == ms) {
				error = hif_subnet_list_remove(&sc->hif_hs_list_home,
							       hs);
				if (error) {
					mip6log((LOG_ERR,
						 "%s:%d: can't remove hif_subnet (0x%p).\n",
						 __FILE__, __LINE__, hs));
				}
			}
		}
	}

	FREE(ms, M_TEMP);

	return (0);
}

int
mip6_subnet_list_insert(ms_list, ms)
	struct mip6_subnet_list *ms_list;
	struct mip6_subnet *ms;
{
	if ((ms_list == NULL) || (ms == NULL)) {
		return (EINVAL);
	}

	LIST_INSERT_HEAD(ms_list, ms, ms_entry);

	if (mip6_subnet_count == 0) {
		mip6_subnet_starttimer();
		mip6log((LOG_INFO,
			 "%s:%d: subnet timer started.\n",
			 __FILE__, __LINE__));
	}
	mip6_subnet_count++;

	return (0);
}

int
mip6_subnet_list_remove(ms_list, ms)
	struct mip6_subnet_list *ms_list;
	struct mip6_subnet *ms;
{
	int error = 0;

	if ((ms_list == NULL) || (ms == NULL)) {
		return (EINVAL);
	}

	LIST_REMOVE(ms, ms_entry);
	error = mip6_subnet_delete(ms);

	mip6_subnet_count--;
	if (mip6_subnet_count == 0) {
		mip6log((LOG_INFO,
			 "%s:%d: subnet timer stopped.\n",
			 __FILE__, __LINE__));
		mip6_subnet_stoptimer();
	}

	return (error);
}

struct mip6_subnet *
mip6_subnet_list_find_withprefix(ms_list, prefix, prefixlen)
	struct mip6_subnet_list *ms_list;
	struct sockaddr_in6 *prefix;
	u_int8_t prefixlen;
{
	struct mip6_subnet *ms;
	struct mip6_subnet_prefix *mspfx;

	if ((ms_list == NULL)
	    || (prefix == NULL)
	    || (prefixlen > 128)) {
		return (NULL);
	}

	for (ms = LIST_FIRST(&mip6_subnet_list); ms;
	     ms = LIST_NEXT(ms, ms_entry)) {
		mspfx = mip6_subnet_prefix_list_find_withprefix(&ms->ms_mspfx_list,
								prefix,
								prefixlen);
		if (mspfx) {
			return (ms);
		}
	}

	/* not found. */
	return (NULL);
}

struct mip6_subnet *
mip6_subnet_list_find_withmpfx(ms_list, mpfx)
	struct mip6_subnet_list *ms_list;
	struct mip6_prefix *mpfx;
{
	struct mip6_subnet *ms;
	struct mip6_subnet_prefix *mspfx;

	if ((ms_list == NULL) || (mpfx == NULL)) {
		return (NULL);
	}

	for (ms = LIST_FIRST(&mip6_subnet_list);
	     ms;
	     ms = LIST_NEXT(ms, ms_entry)) {
		mspfx =	mip6_subnet_prefix_list_find_withmpfx(&ms->ms_mspfx_list,
							      mpfx);
		if (mspfx) {
			return (ms);
		}
	}

	/* not found. */
	return (NULL);
}

struct mip6_subnet *
mip6_subnet_list_find_withhaaddr(ms_list, haaddr)
	struct mip6_subnet_list *ms_list;
	struct sockaddr_in6 *haaddr;
{
	struct mip6_subnet *ms;
	struct mip6_subnet_ha *msha;

	if ((ms_list == NULL) || (haaddr == NULL)) {
		return (NULL);
	}

	for (ms = LIST_FIRST(&mip6_subnet_list); ms;
	     ms = LIST_NEXT(ms, ms_entry)) {
		msha = mip6_subnet_ha_list_find_withhaaddr(&ms->ms_msha_list,
							   haaddr);
		if (msha) {
			return (ms);
		}
	}

	/* not found. */
	return (NULL);
}

struct mip6_subnet_prefix *
mip6_subnet_prefix_create(mpfx)
	struct mip6_prefix *mpfx;
{
	struct mip6_subnet_prefix *mspfx;

	MALLOC(mspfx, struct mip6_subnet_prefix *,
	       sizeof(struct mip6_subnet_prefix), M_TEMP, M_NOWAIT);
	if (mspfx == NULL) {
		mip6log((LOG_ERR,
			 "%s:%d: memory allocation failed.\n",
			 __FILE__, __LINE__));
		return (NULL);
	}
	bzero(mspfx, sizeof(*mspfx));
	mspfx->mspfx_mpfx = mpfx;

	return (mspfx);
}

int
mip6_subnet_prefix_list_insert(mspfx_list, mspfx)
	struct mip6_subnet_prefix_list *mspfx_list;
	struct mip6_subnet_prefix *mspfx;
{
	if ((mspfx_list == NULL) || (mspfx == NULL)) {
		return (EINVAL);
	}

	TAILQ_INSERT_HEAD(mspfx_list, mspfx, mspfx_entry);

	return (0);
}

int
mip6_subnet_prefix_list_remove(mspfx_list, mspfx)
	struct mip6_subnet_prefix_list *mspfx_list;
	struct mip6_subnet_prefix *mspfx;
{
	int error = 0;

	if ((mspfx_list == NULL) || (mspfx == NULL)) {
		return (EINVAL);
	}

	TAILQ_REMOVE(mspfx_list, mspfx, mspfx_entry);
	error = mip6_prefix_list_remove(&mip6_prefix_list, mspfx->mspfx_mpfx);
	if (error) {
		return (error);
	}

	FREE(mspfx, M_TEMP);

	return (0);
}

struct mip6_subnet_prefix *
mip6_subnet_prefix_list_find_withmpfx(mspfx_list, mpfx)
	struct mip6_subnet_prefix_list *mspfx_list;
	struct mip6_prefix *mpfx;
{
	struct mip6_subnet_prefix *mspfx;

	if ((mspfx_list == NULL) || (mpfx == NULL)) {
		return (NULL);
	}

	for (mspfx = TAILQ_FIRST(mspfx_list); mspfx;
	     mspfx = TAILQ_NEXT(mspfx, mspfx_entry)) {
		if (mspfx->mspfx_mpfx == mpfx) {
			/* found. */
			return (mspfx);
		}
	}

	/* not found. */
	return (NULL);
}

struct mip6_subnet_prefix *
mip6_subnet_prefix_list_find_withprefix(mspfx_list, prefix, prefixlen)
	struct mip6_subnet_prefix_list *mspfx_list;
	struct sockaddr_in6 *prefix;
	u_int8_t prefixlen;
{
	struct mip6_subnet_prefix *mspfx;
	struct mip6_prefix *mpfx;

	/*
	 * walk mip6_subnet_prefix_list and check each mip6_prefix
	 * (which is a member of mip6_subnet_prefix as a pointer) if
	 * it contains specified prefix or not.
	 */
	for (mspfx = TAILQ_FIRST(mspfx_list); mspfx;
	     mspfx = TAILQ_NEXT(mspfx, mspfx_entry)) {
		if ((mpfx = mspfx->mspfx_mpfx) == NULL) {
			/* must not happen. */
			mip6log((LOG_ERR,
				 "%s:%d: mspfx_mpfx is a NULL pointer.\n",
				 __FILE__, __LINE__));
			return (NULL);
		}
		if ((in6_are_prefix_equal(&mpfx->mpfx_prefix.sin6_addr,
					  &prefix->sin6_addr,
					  prefixlen))
		    && (mpfx->mpfx_prefixlen == prefixlen)) {
			/* found. */
			return (mspfx);
		}
	}

	/* not found. */
	return (NULL);
}

int32_t
mip6_subnet_prefix_list_get_minimum_lifetime(mspfx_list)
	struct mip6_subnet_prefix_list *mspfx_list;
{
	int32_t lifetime = 0xffff;
	struct mip6_subnet_prefix *mspfx;
	struct mip6_prefix *mpfx;

	for (mspfx = TAILQ_FIRST(mspfx_list);
	     mspfx;
	     mspfx = TAILQ_NEXT(mspfx, mspfx_entry)) {
		if ((mpfx = mspfx->mspfx_mpfx) == NULL) {
			/* must not happen.  try next. */
			continue;
		}

		if (lifetime > mpfx->mpfx_pltime) {
			lifetime = mpfx->mpfx_pltime;
		}
	}
	
	return (lifetime);
}

struct mip6_subnet_ha *
mip6_subnet_ha_create(mha)
	struct mip6_ha *mha;
{
	struct mip6_subnet_ha *msha;

	MALLOC(msha, struct mip6_subnet_ha *,
	       sizeof(struct mip6_subnet_ha), M_TEMP, M_NOWAIT);
	if (msha == NULL) {
		mip6log((LOG_ERR,
			 "%s:%d: memory allocation failed.\n",
			 __FILE__, __LINE__));
		return (NULL);
	}
	bzero(msha, sizeof(*msha));
	msha->msha_mha = mha;

	return (msha);
}

int
mip6_subnet_ha_list_insert(msha_list, msha)
	struct mip6_subnet_ha_list *msha_list;
	struct mip6_subnet_ha *msha;
{
	if ((msha_list == NULL) || (msha == NULL)) {
		return (EINVAL);
	}

	TAILQ_INSERT_HEAD(msha_list, msha, msha_entry);

	return (0);
}

/*
 * find preferable home agene.
 * XXX current code doesn't take a pref value into consideration.
 */
struct mip6_subnet_ha *
mip6_subnet_ha_list_find_preferable(msha_list)
	struct mip6_subnet_ha_list *msha_list;
{
	struct mip6_subnet_ha *msha;
	struct mip6_ha *mha;

	for (msha = TAILQ_FIRST(msha_list); msha;
	     msha = TAILQ_NEXT(msha, msha_entry)) {
		mha = msha->msha_mha;
		if (mha == NULL) {
			/* must not happen. */
			continue;
		}
		if (mha->mha_flags & ND_RA_FLAG_HOME_AGENT) {
			/* found. */
			return (msha);
		}
	}
	
	/* not found. */
	return (NULL);
}

struct mip6_subnet_ha *
mip6_subnet_ha_list_find_withmha(msha_list, mha)
	struct mip6_subnet_ha_list *msha_list;
	struct mip6_ha *mha;
{
	struct mip6_subnet_ha *msha;

	if ((msha_list == NULL) || (mha == NULL)) {
		return (NULL);
	}

	for (msha = TAILQ_FIRST(msha_list); msha;
	     msha = TAILQ_NEXT(msha, msha_entry)) {
		if (msha->msha_mha == mha) {
			/* found. */
			return (msha);
		}
	}

	/* not found. */
	return (NULL);
}

struct mip6_subnet_ha *
mip6_subnet_ha_list_find_withhaaddr(msha_list, haaddr)
	struct mip6_subnet_ha_list *msha_list;
	struct sockaddr_in6 *haaddr;
{
	struct mip6_subnet_ha *msha;
	struct mip6_ha *mha;

	/*
	 * walk mip6_subnet_ha_list and check each mip6_ha (which is a
	 * member of mip6_subnet_ha as a pointer) if it contains
	 * specified haaddr or not.
	 */
	for (msha = TAILQ_FIRST(msha_list); msha;
	     msha = TAILQ_NEXT(msha, msha_entry)) {
		if ((mha = msha->msha_mha) == NULL) {
			/* must not happen. */
			mip6log((LOG_ERR,
				 "%s:%d: msha_mha is a NULL pointer.\n",
				 __FILE__, __LINE__));
			return (NULL);
		}
		if (SA6_ARE_ADDR_EQUAL(&mha->mha_lladdr,
				       haaddr)
		    || SA6_ARE_ADDR_EQUAL(&mha->mha_gaddr,
					  haaddr)) {
			/* found. */
			return (msha);
		}
	}

	/* not found. */
	return (NULL);
}

static void
mip6_subnet_timeout(arg)
	void *arg;
{
	struct mip6_subnet *ms, *ms_next;
	struct mip6_subnet_prefix *mspfx, *mspfx_next;
	struct mip6_subnet_ha *msha, *msha_next, *msha_head;
	int s;

	mip6_subnet_starttimer();

#ifdef __NetBSD__
	s = splsoftnet();
#else
	s = splnet();
#endif

	for (ms = LIST_FIRST(&mip6_subnet_list);
	     ms;
	     ms = ms_next) {
		ms_next = LIST_NEXT(ms, ms_entry);

		/* check for each ha. */
		for (msha = TAILQ_FIRST(&ms->ms_msha_list);
		     msha;
		     msha = msha_next) {
			msha_next = TAILQ_NEXT(msha, msha_entry);

			/*
			 * XXX: TODO
			 *
			 * mip6_ha timeout routine will be here.
			 */
			mip6_subnet_ha_timeout(msha);
		}
		msha_head = TAILQ_FIRST(&ms->ms_msha_list);
		if (msha_head == NULL) {
			/* no home agent is found yet. */
			continue;
		}

		/* check for each prefix. */
		for (mspfx = TAILQ_FIRST(&ms->ms_mspfx_list);
		     mspfx;
		     mspfx = mspfx_next) {
			mspfx_next = TAILQ_NEXT(mspfx, mspfx_entry);
			/*
			 * XXX: TODO
			 * check timeout and send mp_sol if needed.
			 */
			mip6_subnet_prefix_timeout(mspfx, msha_head);
		}
	}

	splx(s);
}

static void
mip6_subnet_ha_timeout(msha)
	struct mip6_subnet_ha *msha;
{
	struct mip6_ha *mha = msha->msha_mha;
	struct hif_softc *sc;
	struct mip6_bu *mbu;
	int error;
#if !(defined(__FreeBSD__) && __FreeBSD__ >= 3)
#if 0 /* stop temporally */
	long time_second = time.tv_sec;
#endif
#endif

	if (mha == NULL) {
		/* must not happen. */
		return;
	}

	if (!(mha->mha_flags & ND_RA_FLAG_HOME_AGENT)) {
		/* this is not a home agent. */
		return;
	}

#if 0 /* stop temporally */
	if (mha->mha_expire < time_second)
#else
	if (0)
#endif
	{
		/* lifetime expired. */
		for (sc = TAILQ_FIRST(&hif_softc_list);
		     sc;
		     sc = TAILQ_NEXT(sc, hif_entry)) {
			/*
			 * set in6addr_any as a home agent address for
			 * each binding update entry that are using
			 * the expired home agent address.
			 */
			for (mbu = LIST_FIRST(&sc->hif_bu_list);
			     mbu;
			     mbu = LIST_NEXT(mbu, mbu_entry)) {
				if ((mbu->mbu_flags & IP6_BUF_HOME) == 0) {
					/*
					 * this is not a home
					 * registration entry.
					 */
					continue;
				}
				if (SA6_ARE_ADDR_EQUAL(&mbu->mbu_paddr,
						       &mha->mha_gaddr)) {
					/* this home agent has expired. */
					bzero(&mbu->mbu_paddr,
					      sizeof(mbu->mbu_paddr));
					mbu->mbu_paddr.sin6_len
						= sizeof(mbu->mbu_paddr);
					mbu->mbu_paddr.sin6_family = AF_INET6;
					mbu->mbu_paddr.sin6_addr
						= in6addr_any;
				}
			}
		}
		error = mip6_ha_list_remove(&mip6_ha_list, mha);
		if (error) {
			mip6log((LOG_ERR,
				 "%s:%d: mha deletion failed (code %d).\n",
				 __FILE__, __LINE__, error));
		}
	}
}

static void
mip6_subnet_prefix_timeout(mspfx, msha)
	struct mip6_subnet_prefix *mspfx;
	struct mip6_subnet_ha *msha;
{
	struct mip6_prefix *mpfx = mspfx->mspfx_mpfx;
	struct mip6_ha *mha = msha->msha_mha;
#if !(defined(__FreeBSD__) && __FreeBSD__ >= 3)
#if 0 /* stop temporally */
	long time_second = time.tv_sec;
#endif
#endif

	if ((mpfx == NULL) || (mha == NULL)) {
		/* must not happen. */
		return;
	}

	/*
	 * XXX: TODO
	 *
	 * expiration check and home address autoconfiguration on hif0.
	 */

#if 0  /* stop temporally.  until mobile prefix adv is implemented. */
	if ((mpfx->mpfx_plexpire - time_second) < (mpfx->mpfx_pltime / 2))
#else
	if (0)
#endif
	{
		mip6_icmp6_mp_sol_output(mpfx, mha);
	}
}

static void
mip6_subnet_starttimer()
{
#if defined(__NetBSD__) || (defined(__FreeBSD__) && __FreeBSD__ >= 3)
	callout_reset(&mip6_subnet_ch,
		      MIP6_SUBNET_TIMEOUT_INTERVAL * hz,
		      mip6_subnet_timeout, NULL);
#elif defined(__OpenBSD__)
	timeout_set(&mip6_subnet_ch, mip6_subnet_timeout, NULL);
	timeout_add(&mip6_subnet_ch,
		    MIP6_SUBNET_TIMEOUT_INTERVAL * hz);
#else
	timeout(mip6_subnet_timeout, (void *)0,
		MIP6_SUBNET_TIMEOUT_INTERVAL * hz);
#endif
}

static void
mip6_subnet_stoptimer()
{
#if defined(__NetBSD__) || (defined(__FreeBSD__) && __FreeBSD__ >= 3)
	callout_stop(&mip6_subnet_ch);
#elif defined(__OpenBSD__)
	timeout_del(&mip6_subnet_ch);
#else
	untimeout(mip6_subnet_timeout, (void *)0);
#endif
}
