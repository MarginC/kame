/*	$KAME: altq_cdnr.c,v 1.4 2000/04/17 10:46:57 kjc Exp $	*/

/*
 * Copyright (C) 1999
 *	Sony Computer Science Laboratories Inc.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY SONY CSL AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL SONY CSL OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $Id: altq_cdnr.c,v 1.4 2000/04/17 10:46:57 kjc Exp $
 */

#if defined(__FreeBSD__) || defined(__NetBSD__)
#include "opt_altq.h"
#if !defined(__FreeBSD__) || (__FreeBSD__ > 2)
#include "opt_inet.h"
#if (__FreeBSD__ > 3)
#include "opt_inet6.h"
#endif
#endif
#endif /* __FreeBSD__ || __NetBSD__ */

#include <sys/param.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/socket.h>
#include <sys/sockio.h>
#include <sys/systm.h>
#include <sys/proc.h>
#include <sys/errno.h>
#include <sys/kernel.h>
#include <sys/queue.h>

#include <net/if.h>
#include <net/if_types.h>
#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/ip.h>
#ifdef INET6
#include <netinet/ip6.h>
#endif

#include <altq/altq.h>
#include <altq/altq_conf.h>
#include <altq/altq_cdnr.h>

/*
 * diffserv traffic conditioning module
 */

int altq_cdnr_enabled = 0;

/* traffic conditioner is enabled by CDNR option in opt_altq.h */
#ifdef CDNR

/* cdnr_list keeps all cdnr's allocated. */
static LIST_HEAD(, top_cdnr) tcb_list;

/* default tbrio parameter values */
static int tbrio_avg_pkt_size = 1024;	/* average packet size */
static int tbrio_holdtime_pkts = 100;	/* hold time by packets */
static int tbrio_lowat = 4096;	/* low watermark: should be larger than MTU */


int cdnropen __P((dev_t, int, int, struct proc *));
int cdnrclose __P((dev_t, int, int, struct proc *));
int cdnrioctl __P((dev_t, ioctlcmd_t, caddr_t, int, struct proc *));

static struct top_cdnr *tcb_lookup __P((char *ifname));
static struct cdnr_block *cdnr_handle2cb __P((u_long));
static u_long cdnr_cb2handle __P((struct cdnr_block *));
static void *cdnr_cballoc __P((struct top_cdnr *, int,
       struct tc_action *(*)(struct cdnr_block *, struct cdnr_pktinfo *)));
static void cdnr_cbdestroy __P((void *));
static int tca_verify_action __P((struct tc_action *));
static void tca_import_action __P((struct tc_action *, struct tc_action *));
static void tca_invalidate_action __P((struct tc_action *));

static int generic_element_destroy __P((struct cdnr_block *));
static struct top_cdnr *top_create __P((struct ifnet *));
static int top_destroy __P((struct top_cdnr *));
static struct cdnr_block *element_create __P((struct top_cdnr *,
					      struct tc_action *));
static int element_destroy __P((struct cdnr_block *));
static void tb_import_profile __P((struct tbe *, struct tb_profile *));
static struct tbmeter *tbm_create __P((struct top_cdnr *, struct tb_profile *,
			   struct tc_action *, struct tc_action *));
static int tbm_destroy __P((struct tbmeter *));
static struct tc_action *tbm_input __P((struct cdnr_block *,
					struct cdnr_pktinfo *));
static struct trtcm *trtcm_create __P((struct top_cdnr *,
		  struct tb_profile *, struct tb_profile *,
		  struct tc_action *, struct tc_action *, struct tc_action *,
		  int));
static int trtcm_destroy __P((struct trtcm *));
static struct tc_action *trtcm_input __P((struct cdnr_block *,
					  struct cdnr_pktinfo *));
static struct tbrio *tbrio_create __P((struct top_cdnr *, struct tb_profile *,
			       struct tc_action *, struct tc_action *));
static int tbrio_destroy __P((struct tbrio *));
static struct tc_action *tbrio_input __P((struct cdnr_block *,
					  struct cdnr_pktinfo *));
static struct tswtcm *tswtcm_create __P((struct top_cdnr *,
		  u_int32_t, u_int32_t, u_int32_t,
		  struct tc_action *, struct tc_action *, struct tc_action *));
static int tswtcm_destroy __P((struct tswtcm *));
static struct tc_action *tswtcm_input __P((struct cdnr_block *,
					   struct cdnr_pktinfo *));

static int cdnrcmd_if_attach __P((char *));
static int cdnrcmd_if_detach __P((char *));
static int cdnrcmd_add_element __P((struct cdnr_add_element *));
static int cdnrcmd_delete_element __P((struct cdnr_delete_element *));
static int cdnrcmd_add_filter __P((struct cdnr_add_filter *));
static int cdnrcmd_delete_filter __P((struct cdnr_delete_filter *));
static int cdnrcmd_add_tbm __P((struct cdnr_add_tbmeter *));
static int cdnrcmd_modify_tbm __P((struct cdnr_modify_tbmeter *));
static int cdnrcmd_tbm_stats __P((struct cdnr_tbmeter_stats *));
static int cdnrcmd_add_trtcm __P((struct cdnr_add_trtcm *));
static int cdnrcmd_modify_trtcm __P((struct cdnr_modify_trtcm *));
static int cdnrcmd_tcm_stats __P((struct cdnr_tcm_stats *));
static int cdnrcmd_add_tbrio __P((struct cdnr_add_tbrio *));
static int cdnrcmd_modify_tbrio __P((struct cdnr_modify_tbrio *));
static int cdnrcmd_tbrio_stats __P((struct cdnr_tbrio_stats *));
static int cdnrcmd_tbrio_getdef __P((struct cdnr_tbrio_params *));
static int cdnrcmd_tbrio_setdef __P((struct cdnr_tbrio_params *));
static int cdnrcmd_add_tswtcm __P((struct cdnr_add_tswtcm *));
static int cdnrcmd_modify_tswtcm __P((struct cdnr_modify_tswtcm *));
static int cdnrcmd_get_stats __P((struct cdnr_get_stats *));

/*
 * top level input function called from ip_input.
 * should be called before converting header fields to host-byte-order.
 */
int
altq_cdnr_input(m, af)
	struct mbuf	*m;
	int		af;	/* address family */
{
	struct ifnet		*ifp;
	struct ip		*ip;
	struct top_cdnr		*top;
	struct tc_action	*tca;
	struct cdnr_block	*cb;
	struct cdnr_pktinfo	pktinfo;
	struct pr_hdr		pr_hdr;
	struct flowinfo		flow;

	ifp = m->m_pkthdr.rcvif;
	if (!ALTQ_IS_CNDTNING(ifp))
		/* traffic conditioner is not enabled on this interface */
		return (1);

	top = ifp->if_altqcdnr;

	ip = mtod(m, struct ip *);
#ifdef INET6
	if (af == AF_INET6) {
		u_int32_t flowlabel;
		
		flowlabel = ((struct ip6_hdr *)ip)->ip6_flow;
		pktinfo.pkt_dscp = (ntohl(flowlabel) >> 20) & DSCP_MASK;
	}
	else
#endif
		pktinfo.pkt_dscp = ip->ip_tos & DSCP_MASK;
	pktinfo.pkt_len = m_pktlen(m);

	tca = NULL;

	pr_hdr.ph_family = af;
	pr_hdr.ph_hdr = (caddr_t)ip;
	altq_extractflow(m, &pr_hdr, &flow,
			 top->tc_classifier.acc_fbmask);
	cb = acc_classify(&top->tc_classifier, &flow);
	if (cb != NULL)
		tca = &cb->cb_action;

	if (tca == NULL)
		tca = &top->tc_block.cb_action;

	while (1) {
		top->tc_stats[tca->tca_code].packets++;
		top->tc_stats[tca->tca_code].bytes += pktinfo.pkt_len;
		
		switch (tca->tca_code) {
		case TCACODE_PASS:
			return (1);
		case TCACODE_DROP:
			m_freem(m);
			return (0);
		case TCACODE_RETURN:
			return (0);
		case TCACODE_MARK:
#ifdef INET6
			if (af == AF_INET6) {
				struct ip6_hdr *ip6 = (struct ip6_hdr *)ip;
				u_int32_t flowlabel;

				flowlabel = ntohl(ip6->ip6_flow);
				flowlabel = (tca->tca_dscp << 20) |
					(flowlabel & ~(DSCP_MASK << 20));
				ip6->ip6_flow = htonl(flowlabel);
			}
			else
#endif
				ip->ip_tos = tca->tca_dscp |
					(ip->ip_tos & DSCP_CUMASK);
			return (1);
		case TCACODE_NEXT:
			cb = tca->tca_next;
			tca = (*cb->cb_input)(cb, &pktinfo);
			break;
		case TCACODE_NONE:
		default:
			return (1);
		}
	}
}

static struct top_cdnr *
tcb_lookup(ifname)
	char *ifname;
{
	struct top_cdnr *top;
	struct ifnet *ifp;

	if ((ifp = ifunit(ifname)) != NULL)
		LIST_FOREACH(top, &tcb_list, tc_next)
			if (top->tc_ifp == ifp)
				return (top);
	return (NULL);
}

static struct cdnr_block *
cdnr_handle2cb(handle)
	u_long handle;
{
	struct cdnr_block *cb;

	cb = (struct cdnr_block *)handle;
	if (handle != ALIGN(cb))
		return (NULL);
	
	if (cb == NULL || cb->cb_handle != handle)
		return (NULL);
	return (cb);
}

static u_long
cdnr_cb2handle(cb)
	struct cdnr_block *cb;
{
	return (cb->cb_handle);
}

static void *
cdnr_cballoc(top, type, input_func)
	struct top_cdnr *top;
	int type;
	struct tc_action *(*input_func)(struct cdnr_block *,
					struct cdnr_pktinfo *);
{
	struct cdnr_block *cb;
	int size;

	switch (type) {
	case TCETYPE_TOP:
		size = sizeof(struct top_cdnr);
		break;
	case TCETYPE_ELEMENT:
		size = sizeof(struct cdnr_block);
		break;
	case TCETYPE_TBMETER:
		size = sizeof(struct tbmeter);
		break;
	case TCETYPE_TRTCM:
		size = sizeof(struct trtcm);
		break;
	case TCETYPE_TBRIO:
		size = sizeof(struct tbrio);
		break;
	case TCETYPE_TSWTCM:
		size = sizeof(struct tswtcm);
		break;
	default:
		return (NULL);
	}

	MALLOC(cb, struct cdnr_block *, size, M_DEVBUF, M_WAITOK);
	if (cb == NULL)
		return (NULL);
	bzero(cb, size);
	
	cb->cb_len = size;
	cb->cb_type = type;
	cb->cb_ref = 0;
	cb->cb_handle = (u_long)cb;
	if (top == NULL)
		cb->cb_top = (struct top_cdnr *)cb;
	else
		cb->cb_top = top;

	if (input_func != NULL) {
		/*
		 * if this cdnr has an action function,
		 * make tc_action to call itself.
		 */
		cb->cb_action.tca_code = TCACODE_NEXT;
		cb->cb_action.tca_next = cb;
		cb->cb_input = input_func;
	}
	else
		cb->cb_action.tca_code = TCACODE_NONE;

	/* if this isn't top, register the element to the top level cdnr */
	if (top != NULL)
		LIST_INSERT_HEAD(&top->tc_elements, cb, cb_next);

	return ((void *)cb);
}

static void
cdnr_cbdestroy(cblock)
	void *cblock;
{
	struct cdnr_block *cb = cblock;

	/* delete filters belonging to this cdnr */
	acc_discard_filters(&cb->cb_top->tc_classifier, cb, 0);

	/* remove from the top level cdnr */
	if (cb->cb_top != cblock)
		LIST_REMOVE(cb, cb_next);

	FREE(cb, M_DEVBUF);
}

/*
 * conditioner common destroy routine
 */
static int
generic_element_destroy(cb)
	struct cdnr_block *cb;
{
	int error = 0;

	switch (cb->cb_type) {
	case TCETYPE_TOP:
		error = top_destroy((struct top_cdnr *)cb);
		break;
	case TCETYPE_ELEMENT:
		error = element_destroy(cb);
		break;
	case TCETYPE_TBMETER:
		error = tbm_destroy((struct tbmeter *)cb);
		break;
	case TCETYPE_TRTCM:
		error = trtcm_destroy((struct trtcm *)cb);
		break;
	case TCETYPE_TBRIO:
		error = tbrio_destroy((struct tbrio *)cb);
		break;
	case TCETYPE_TSWTCM:
		error = tswtcm_destroy((struct tswtcm *)cb);
		break;
	default:
		error = EINVAL;
	}
	return (error);
}

static int
tca_verify_action(utca)
	struct tc_action *utca;
{
	switch (utca->tca_code) {
	case TCACODE_PASS:
	case TCACODE_DROP:
	case TCACODE_MARK:
		/* these are ok */
		break;

	case TCACODE_HANDLE:
		/* verify handle value */
		if (cdnr_handle2cb(utca->tca_handle) == NULL)
			return (-1);
		break;

	case TCACODE_NONE:
	case TCACODE_RETURN:
	case TCACODE_NEXT:
	default:
		/* should not be passed from a user */
		return (-1);
	}
	return (0);
}

static void
tca_import_action(ktca, utca)
	struct tc_action *ktca, *utca;
{
	struct cdnr_block *cb;

	*ktca = *utca;
	if (ktca->tca_code == TCACODE_HANDLE) {
		cb = cdnr_handle2cb(ktca->tca_handle);
		if (cb == NULL) {
			ktca->tca_code = TCACODE_NONE;
			return;
		}
		ktca->tca_code = TCACODE_NEXT;
		ktca->tca_next = cb;
		cb->cb_ref++;
	}
	else if (ktca->tca_code == TCACODE_MARK) {
		ktca->tca_dscp &= DSCP_MASK;
	}
	return;
}

static void
tca_invalidate_action(tca)
	struct tc_action *tca;
{
	struct cdnr_block *cb;

	if (tca->tca_code == TCACODE_NEXT) {
		cb = tca->tca_next;
		if (cb == NULL)
			return;
		cb->cb_ref--;
	}
	tca->tca_code = TCACODE_NONE;
}

/*
 * top level traffic conditioner
 */
static struct top_cdnr *
top_create(ifp)
	struct ifnet *ifp;
{
	struct top_cdnr *top;

	if ((top = cdnr_cballoc(NULL, TCETYPE_TOP, NULL)) == NULL)
		return (NULL);

	top->tc_ifp = ifp;
	/* set default action for the top level conditioner */
	top->tc_block.cb_action.tca_code = TCACODE_PASS;

	LIST_INSERT_HEAD(&tcb_list, top, tc_next);

	ifp->if_altqcdnr = top;

	return (top);
}

static int
top_destroy(top)
	struct top_cdnr *top;
{
	struct cdnr_block *cb;

	if (ALTQ_IS_CNDTNING(top->tc_ifp))
		CLEAR_CNDTNING(top->tc_ifp);
	top->tc_ifp->if_altqcdnr = NULL;

	/*
	 * destroy all the conditioner elements belonging to this interface
	 */
	while ((cb = LIST_FIRST(&top->tc_elements)) != NULL) {
		while (cb != NULL && cb->cb_ref > 0)
			cb = LIST_NEXT(cb, cb_next);
		if (cb != NULL)
			generic_element_destroy(cb);
	}
	
	LIST_REMOVE(top, tc_next);

	cdnr_cbdestroy(top);

	/* if there is no active conditioner, remove the input hook */
	if (altq_input != NULL) {
		LIST_FOREACH(top, &tcb_list, tc_next)
			if (ALTQ_IS_CNDTNING(top->tc_ifp))
				break;
		if (top == NULL)
			altq_input = NULL;
	}

	return (0);
}

/*
 * simple tc elements without input function (e.g., dropper and makers).
 */
static struct cdnr_block *
element_create(top, action)
	struct top_cdnr *top;
	struct tc_action *action;
{
	struct cdnr_block *cb;

	if (tca_verify_action(action) < 0)
		return (NULL);

	if ((cb = cdnr_cballoc(top, TCETYPE_ELEMENT, NULL)) == NULL)
		return (NULL);

	tca_import_action(&cb->cb_action, action);

	return (cb);
}

static int
element_destroy(cb)
	struct cdnr_block *cb;
{
	if (cb->cb_ref > 0)
		return (EBUSY);

	tca_invalidate_action(&cb->cb_action);

	cdnr_cbdestroy(cb);
	return (0);
}

/*
 * internal representation of token bucket parameters
 *	rate: 	byte_per_unittime << 32
 *		(((bits_per_sec) / 8) << 32) / machclk_freq
 *	depth:	byte << 32
 *
 */
#define TB_SHIFT	32
#define TB_SCALE(x)	((u_int64_t)(x) << TB_SHIFT)
#define TB_UNSCALE(x)	((x) >> TB_SHIFT)

static void
tb_import_profile(tb, profile)
	struct tbe *tb;
	struct tb_profile *profile;
{
	tb->rate = TB_SCALE(profile->rate / 8) / machclk_freq;
	tb->depth = TB_SCALE(profile->depth);
	if (tb->rate > 0)
		tb->filluptime = tb->depth / tb->rate;
	else
		tb->filluptime = 0xffffffffffffffffLL;
	tb->token = tb->depth;
	tb->last = read_machclk();
}

/*
 * simple token bucket meter
 */
static struct tbmeter *
tbm_create(top, profile, in_action, out_action)
	struct top_cdnr *top;
	struct tb_profile *profile;
	struct tc_action *in_action, *out_action;
{
	struct tbmeter *tbm = NULL;

	if (tca_verify_action(in_action) < 0
	    || tca_verify_action(out_action) < 0)
		return (NULL);

	if ((tbm = cdnr_cballoc(top, TCETYPE_TBMETER,
				tbm_input)) == NULL)
		return (NULL);

	tb_import_profile(&tbm->tb, profile);

	tca_import_action(&tbm->in_action, in_action);
	tca_import_action(&tbm->out_action, out_action);

	return (tbm);
}

static int
tbm_destroy(tbm)
	struct tbmeter *tbm;
{
	if (tbm->cdnrblk.cb_ref > 0)
		return (EBUSY);

	tca_invalidate_action(&tbm->in_action);
	tca_invalidate_action(&tbm->out_action);

	cdnr_cbdestroy(tbm);
	return (0);
}
	
static struct tc_action *
tbm_input(cb, pktinfo)
	struct cdnr_block *cb;
	struct cdnr_pktinfo *pktinfo;
{
	struct tbmeter *tbm = (struct tbmeter *)cb;
	u_int64_t	len;
	u_int64_t	interval, now;
	
	len = TB_SCALE(pktinfo->pkt_len);

	if (tbm->tb.token < len) {
		now = read_machclk();
		interval = now - tbm->tb.last;
		if (interval >= tbm->tb.filluptime)
			tbm->tb.token = tbm->tb.depth;
		else {
			tbm->tb.token += interval * tbm->tb.rate;
			if (tbm->tb.token > tbm->tb.depth)
				tbm->tb.token = tbm->tb.depth;
		}
		tbm->tb.last = now;
	}
	
	if (tbm->tb.token < len) {
		tbm->out_stats.packets++;
		tbm->out_stats.bytes += pktinfo->pkt_len;
		return (&tbm->out_action);
	}

	tbm->tb.token -= len;
	tbm->in_stats.packets++;
	tbm->in_stats.bytes += pktinfo->pkt_len;
	return (&tbm->in_action);
}

/*
 * two rate three color marker
 * as described in draft-heinanen-diffserv-trtcm-01.txt
 */
static struct trtcm *
trtcm_create(top, cmtd_profile, peak_profile,
	     green_action, yellow_action, red_action, coloraware)
	struct top_cdnr *top;
	struct tb_profile *cmtd_profile, *peak_profile;
	struct tc_action *green_action, *yellow_action, *red_action;
	int	coloraware;
{
	struct trtcm *tcm = NULL;

	if (tca_verify_action(green_action) < 0
	    || tca_verify_action(yellow_action) < 0
	    || tca_verify_action(red_action) < 0)
		return (NULL);

	if ((tcm = cdnr_cballoc(top, TCETYPE_TRTCM,
				trtcm_input)) == NULL)
		return (NULL);

	tb_import_profile(&tcm->cmtd_tb, cmtd_profile);
	tb_import_profile(&tcm->peak_tb, peak_profile);

	tca_import_action(&tcm->green_action, green_action);
	tca_import_action(&tcm->yellow_action, yellow_action);
	tca_import_action(&tcm->red_action, red_action);

	/* set dscps to use */
	if (tcm->green_action.tca_code == TCACODE_MARK)
		tcm->green_dscp = tcm->green_action.tca_dscp & DSCP_MASK;
	else
		tcm->green_dscp = DSCP_AF11;
	if (tcm->yellow_action.tca_code == TCACODE_MARK)
		tcm->yellow_dscp = tcm->yellow_action.tca_dscp & DSCP_MASK;
	else
		tcm->yellow_dscp = DSCP_AF12;
	if (tcm->red_action.tca_code == TCACODE_MARK)
		tcm->red_dscp = tcm->red_action.tca_dscp & DSCP_MASK;
	else
		tcm->red_dscp = DSCP_AF13;

	tcm->coloraware = coloraware;

	return (tcm);
}

static int
trtcm_destroy(tcm)
	struct trtcm *tcm;
{
	if (tcm->cdnrblk.cb_ref > 0)
		return (EBUSY);

	tca_invalidate_action(&tcm->green_action);
	tca_invalidate_action(&tcm->yellow_action);
	tca_invalidate_action(&tcm->red_action);

	cdnr_cbdestroy(tcm);
	return (0);
}
	
static struct tc_action *
trtcm_input(cb, pktinfo)
	struct cdnr_block *cb;
	struct cdnr_pktinfo *pktinfo;
{
	struct trtcm *tcm = (struct trtcm *)cb;
	u_int64_t	len;
	u_int64_t	interval, now;
	u_int8_t	color;

	len = TB_SCALE(pktinfo->pkt_len);
	if (tcm->coloraware) {
		color = pktinfo->pkt_dscp;
		if (color != tcm->yellow_dscp && color != tcm->red_dscp)
			color = tcm->green_dscp;
	}
	else {
		/* if color-blind, precolor it as green */
		color = tcm->green_dscp;
	}

	now = read_machclk();
	if (tcm->cmtd_tb.token < len) {
		interval = now - tcm->cmtd_tb.last;
		if (interval >= tcm->cmtd_tb.filluptime)
			tcm->cmtd_tb.token = tcm->cmtd_tb.depth;
		else {
			tcm->cmtd_tb.token += interval * tcm->cmtd_tb.rate;
			if (tcm->cmtd_tb.token > tcm->cmtd_tb.depth)
				tcm->cmtd_tb.token = tcm->cmtd_tb.depth;
		}
		tcm->cmtd_tb.last = now;
	}
	if (tcm->peak_tb.token < len) {
		interval = now - tcm->peak_tb.last;
		if (interval >= tcm->peak_tb.filluptime)
			tcm->peak_tb.token = tcm->peak_tb.depth;
		else {
			tcm->peak_tb.token += interval * tcm->peak_tb.rate;
			if (tcm->peak_tb.token > tcm->peak_tb.depth)
				tcm->peak_tb.token = tcm->peak_tb.depth;
		}
		tcm->peak_tb.last = now;
	}
	
	if (color == tcm->red_dscp || tcm->peak_tb.token < len) {
		pktinfo->pkt_dscp = tcm->red_dscp;
		tcm->red_stats.packets++;
		tcm->red_stats.bytes += pktinfo->pkt_len;
		return (&tcm->red_action);
	}

	if (color == tcm->yellow_dscp || tcm->cmtd_tb.token < len) {
		pktinfo->pkt_dscp = tcm->yellow_dscp;
		tcm->peak_tb.token -= len;
		tcm->yellow_stats.packets++;
		tcm->yellow_stats.bytes += pktinfo->pkt_len;
		return (&tcm->yellow_action);
	}

	pktinfo->pkt_dscp = tcm->green_dscp;
	tcm->cmtd_tb.token -= len;
	tcm->peak_tb.token -= len;
	tcm->green_stats.packets++;
	tcm->green_stats.bytes += pktinfo->pkt_len;
	return (&tcm->green_action);
}

/*
 * token bucket rio dropper
 */
#define TBRIOPROB_MAX	(128*1024)
#define TBRIOPROB_MIN	(TBRIOPROB_MAX * 5 / 1000)	/* 0.005 */

static struct tbrio *
tbrio_create(top, profile, pass_action, drop_action)
	struct top_cdnr *top;
	struct tb_profile *profile;
	struct tc_action *pass_action, *drop_action;
{
	struct tbrio *tbrio = NULL;

	if (tca_verify_action(pass_action) < 0
	    || tca_verify_action(drop_action) < 0)
		return (NULL);

	if ((tbrio = cdnr_cballoc(top, TCETYPE_TBRIO,
				tbrio_input)) == NULL)
		return (NULL);

	tb_import_profile(&tbrio->tb, profile);

	tca_import_action(&tbrio->pass_action, pass_action);
	tca_import_action(&tbrio->drop_action, drop_action);

	/* set tbrio parameters */
	tbrio->lowat = TB_SCALE(tbrio_lowat);
	tbrio->prob = 0;
	tbrio->bumps = 0;
	tbrio->count = 0;
	tbrio->hold_time = (u_int64_t)machclk_freq * tbrio_avg_pkt_size * 8
		* tbrio_holdtime_pkts / profile->rate;
	tbrio->last_update = read_machclk();

	/* set default color values */
	tbrio->green_dscp  = DSCP_AF11;
	tbrio->yellow_dscp = DSCP_AF12;
	tbrio->red_dscp    = DSCP_AF13;

	return (tbrio);
}

static int
tbrio_destroy(tbrio)
	struct tbrio *tbrio;
{
	if (tbrio->cdnrblk.cb_ref > 0)
		return (EBUSY);

	tca_invalidate_action(&tbrio->pass_action);
	tca_invalidate_action(&tbrio->drop_action);

	cdnr_cbdestroy(tbrio);
	return (0);
}

/*
 * drop probability is calculated as follows:
 *   prob = scaled_prob / PROB_MAX
 *   prob_a = prob / (2 - count*prob)
 * here prob_a increases as successive undrop count increases.
 * (prob_a starts from prob/2, becomes prob when (count == (1 / prob)),
 * becomes 1 when (count >= (2 / prob))).
 *
 * drop probability for different colors:
 *	green:  never dropped
 *	yellow: dropped with probability prob^2
 *	(when prob_a is small, yellow's drop prob is very small)
 *	red:    dropped with probability prob_a when prob < 0.2
 *		dropped with probability prob   when prob >= 0.2
 */
static struct tc_action *
tbrio_input(cb, pktinfo)
	struct cdnr_block *cb;
	struct cdnr_pktinfo *pktinfo;
{
	struct tbrio *tbrio = (struct tbrio *)cb;
	u_int64_t	len;
	u_int64_t	interval, now;
	u_int8_t	color;
	int		droptype, cindex;

	len = TB_SCALE(pktinfo->pkt_len);
	color = pktinfo->pkt_dscp;
	if (color == tbrio->red_dscp)
		cindex = TBRIO_CINDEX_RED;
	else if (color == tbrio->yellow_dscp)
		cindex = TBRIO_CINDEX_YELLOW;
	else
		cindex = TBRIO_CINDEX_GREEN;

	/* calculate current token */
	now = read_machclk();
	interval = now - tbrio->tb.last;
	if (interval >= tbrio->tb.filluptime)
			tbrio->tb.token = tbrio->tb.depth;
	else {
		tbrio->tb.token += interval * tbrio->tb.rate;
		if (tbrio->tb.token > tbrio->tb.depth)
			tbrio->tb.token = tbrio->tb.depth;
	}
	tbrio->tb.last = now;

	/*
	 * bumps is incremented when the remaining token is less
	 * than the low water mark, and decremented when the token is full.
	 */
	if (tbrio->tb.token < tbrio->lowat)
		tbrio->bumps++;
	else if (tbrio->tb.token == tbrio->tb.depth)
		tbrio->bumps--;
	
	/*
	 * update drop probability every hold time
	 */
	interval = now - tbrio->last_update;
	if (interval >= tbrio->hold_time) {
		interval -= tbrio->hold_time;

		if (tbrio->bumps > 0) {
			/* increase drop probability by 11% */
			if (tbrio->prob == 0)
				tbrio->prob = TBRIOPROB_MIN;
			else
				tbrio->prob += tbrio->prob / 8;
			if (tbrio->prob > TBRIOPROB_MAX)
				tbrio->prob = TBRIOPROB_MAX;
		}
		else if (tbrio->bumps < 0) {
			/* decrease drop probability by 12% */
			tbrio->prob -= tbrio->prob / 8;
			if (tbrio->prob < TBRIOPROB_MIN)
				tbrio->prob = 0;
		}

		/*
		 * if interval is more than 2 hold_time,
		 * adjust probability to reflect the idle period.
		 */
		while (tbrio->prob > 0 && interval >= tbrio->hold_time) {
			interval -= tbrio->hold_time;
			tbrio->prob -= tbrio->prob / 8;
			if (tbrio->prob < TBRIOPROB_MIN)
				tbrio->prob = 0;
		}

		tbrio->bumps = 0;
		tbrio->last_update = now;
	}

	/*
	 * drop the packet with the current drop probability.
	 * don't drop if green or drop prob is zero.
	 */
	droptype = TBRIO_DTYPE_PASS;
	if (cindex != TBRIO_CINDEX_GREEN && tbrio->prob != 0) {
		int p;

		p = tbrio->prob;
		if (cindex == TBRIO_CINDEX_RED && p < (TBRIOPROB_MAX*20/100)) {
			/*
			 * if red packet and prob is less than 20%,
			 * drop with prob_a in order to space drop interval.
			 */
			int d = p * tbrio->count;
			if (d >= 2 * TBRIOPROB_MAX) {
				/* count exceeds the hard limit */
				droptype = TBRIO_DTYPE_DROP;
			}
			else {
				d = 2 * TBRIOPROB_MAX - d;
				if ((random() % d) < p)
					droptype = TBRIO_DTYPE_DROP;
			}
		}
		else {
			/* if yellow, drop with prob (prob)^2 */
			if (cindex == TBRIO_CINDEX_YELLOW)
				p = (int64_t)p * p / TBRIOPROB_MAX;
			if ((random() % TBRIOPROB_MAX) < p)
				droptype = TBRIO_DTYPE_DROP;
		}
	}

	/* update token (regardless of droptype) */
	if (tbrio->tb.token > len)
		tbrio->tb.token -= len;
	else
		tbrio->tb.token = 0;

	tbrio->stats[cindex][droptype].packets++;
	tbrio->stats[cindex][droptype].bytes += pktinfo->pkt_len;
	
	if (droptype == TBRIO_DTYPE_PASS) {
		tbrio->count++;
		return (&tbrio->pass_action);
	}
	tbrio->count = 0;
	return (&tbrio->drop_action);
}

/*
 * time sliding window three color marker
 * as described in draft-fang-diffserv-tc-tswtcm-00.txt
 */
static struct tswtcm *
tswtcm_create(top, cmtd_rate, peak_rate, avg_interval,
	      green_action, yellow_action, red_action)
	struct top_cdnr *top;
	u_int32_t	cmtd_rate, peak_rate, avg_interval;
	struct tc_action *green_action, *yellow_action, *red_action;
{
	struct tswtcm *tsw;

	if (tca_verify_action(green_action) < 0
	    || tca_verify_action(yellow_action) < 0
	    || tca_verify_action(red_action) < 0)
		return (NULL);

	if ((tsw = cdnr_cballoc(top, TCETYPE_TSWTCM,
				tswtcm_input)) == NULL)
		return (NULL);

	tca_import_action(&tsw->green_action, green_action);
	tca_import_action(&tsw->yellow_action, yellow_action);
	tca_import_action(&tsw->red_action, red_action);

	/* set dscps to use */
	if (tsw->green_action.tca_code == TCACODE_MARK)
		tsw->green_dscp = tsw->green_action.tca_dscp & DSCP_MASK;
	else
		tsw->green_dscp = DSCP_AF11;
	if (tsw->yellow_action.tca_code == TCACODE_MARK)
		tsw->yellow_dscp = tsw->yellow_action.tca_dscp & DSCP_MASK;
	else
		tsw->yellow_dscp = DSCP_AF12;
	if (tsw->red_action.tca_code == TCACODE_MARK)
		tsw->red_dscp = tsw->red_action.tca_dscp & DSCP_MASK;
	else
		tsw->red_dscp = DSCP_AF13;

	/* convert rates from bits/sec to bytes/sec */
	tsw->cmtd_rate = cmtd_rate / 8;
	tsw->peak_rate = peak_rate / 8;
	tsw->avg_rate = 0;

	/* timewin is converted from msec to machine clock unit */
	tsw->timewin = (u_int64_t)machclk_freq * avg_interval / 1000;

	return (tsw);
}

static int
tswtcm_destroy(tsw)
	struct tswtcm *tsw;
{
	if (tsw->cdnrblk.cb_ref > 0)
		return (EBUSY);

	tca_invalidate_action(&tsw->green_action);
	tca_invalidate_action(&tsw->yellow_action);
	tca_invalidate_action(&tsw->red_action);

	cdnr_cbdestroy(tsw);
	return (0);
}

static struct tc_action *
tswtcm_input(cb, pktinfo)
	struct cdnr_block *cb;
	struct cdnr_pktinfo *pktinfo;
{
	struct tswtcm	*tsw = (struct tswtcm *)cb;
	int		len;
	u_int32_t	avg_rate;
	u_int64_t	interval, now, tmp;

	/*
	 * rate estimator
	 */
	len = pktinfo->pkt_len;
	now = read_machclk();

	interval = now - tsw->t_front;
	/*
	 * calculate average rate:
	 *	avg = (avg * timewin + pkt_len)/(timewin + interval)
	 * pkt_len needs to be multiplied by machclk_freq in order to
	 * get (bytes/sec).
	 * note: when avg_rate (bytes/sec) and timewin (machclk unit) are
	 * less than 32 bits, the following 64-bit operation has enough
	 * precision.
	 */
	tmp = ((u_int64_t)tsw->avg_rate * tsw->timewin
	       + (u_int64_t)len * machclk_freq) / (tsw->timewin + interval);
	tsw->avg_rate = avg_rate = (u_int32_t)tmp;
	tsw->t_front = now;

	/*
	 * marker
	 */
	if (avg_rate > tsw->cmtd_rate) {
		u_int32_t randval = random() % avg_rate;
		
		if (avg_rate > tsw->peak_rate) {
			if (randval < avg_rate - tsw->peak_rate) {
				/* mark red */
				pktinfo->pkt_dscp = tsw->red_dscp;
				tsw->red_stats.packets++;
				tsw->red_stats.bytes += len;
				return (&tsw->red_action);
			}
			else if (randval < avg_rate - tsw->cmtd_rate)
				goto mark_yellow;
		}
		else {
			/* peak_rate >= avg_rate > cmtd_rate */
			if (randval < avg_rate - tsw->cmtd_rate) {
			mark_yellow:
				pktinfo->pkt_dscp = tsw->yellow_dscp;
				tsw->yellow_stats.packets++;
				tsw->yellow_stats.bytes += len;
				return (&tsw->yellow_action);
			}
		}
	}

	/* mark green */
	pktinfo->pkt_dscp = tsw->green_dscp;
	tsw->green_stats.packets++;
	tsw->green_stats.bytes += len;
	return (&tsw->green_action);
}

/*
 * ioctl requests
 */
static int
cdnrcmd_if_attach(ifname)
	char *ifname;
{
	struct ifnet *ifp;
	struct top_cdnr *top;

	if ((ifp = ifunit(ifname)) == NULL)
		return (EBADF);

	if (ifp->if_altqcdnr != NULL)
		return (EBUSY);

	if ((top = top_create(ifp)) == NULL)
		return (ENOMEM);
	return (0);
}

static int
cdnrcmd_if_detach(ifname)
	char *ifname;
{
	struct top_cdnr *top;

	if ((top = tcb_lookup(ifname)) == NULL)
		return (EBADF);

	return top_destroy(top);
}

static int
cdnrcmd_add_element(ap)
	struct cdnr_add_element *ap;
{
	struct top_cdnr *top;
	struct cdnr_block *cb;

	if ((top = tcb_lookup(ap->iface.cdnr_ifname)) == NULL)
		return (EBADF);

	cb = element_create(top, &ap->action);
	if (cb == NULL)
		return (EINVAL);
	/* return a class handle to the user */
	ap->cdnr_handle = cdnr_cb2handle(cb);
	return (0);
}

static int
cdnrcmd_delete_element(ap)
	struct cdnr_delete_element *ap;
{
	struct top_cdnr *top;
	struct cdnr_block *cb;

	if ((top = tcb_lookup(ap->iface.cdnr_ifname)) == NULL)
		return (EBADF);

	if ((cb = cdnr_handle2cb(ap->cdnr_handle)) == NULL)
		return (EINVAL);

	if (cb->cb_type != TCETYPE_ELEMENT)
		return generic_element_destroy(cb);

	return element_destroy(cb);
}

static int
cdnrcmd_add_filter(ap)
	struct cdnr_add_filter *ap;
{
	struct top_cdnr *top;
	struct cdnr_block *cb;

	if ((top = tcb_lookup(ap->iface.cdnr_ifname)) == NULL)
		return (EBADF);

	if ((cb = cdnr_handle2cb(ap->cdnr_handle)) == NULL)
		return (EINVAL);

	return acc_add_filter(&top->tc_classifier, &ap->filter,
			      cb, &ap->filter_handle);
}

static int
cdnrcmd_delete_filter(ap)
	struct cdnr_delete_filter *ap;
{
	struct top_cdnr *top;

	if ((top = tcb_lookup(ap->iface.cdnr_ifname)) == NULL)
		return (EBADF);

	return acc_delete_filter(&top->tc_classifier, ap->filter_handle);
}

static int
cdnrcmd_add_tbm(ap)
	struct cdnr_add_tbmeter *ap;
{
	struct top_cdnr *top;
	struct tbmeter *tbm;

	if ((top = tcb_lookup(ap->iface.cdnr_ifname)) == NULL)
		return (EBADF);

	tbm = tbm_create(top, &ap->profile, &ap->in_action, &ap->out_action);
	if (tbm == NULL)
		return (EINVAL);
	/* return a class handle to the user */
	ap->cdnr_handle = cdnr_cb2handle(&tbm->cdnrblk);
	return (0);
}

static int
cdnrcmd_modify_tbm(ap)
	struct cdnr_modify_tbmeter *ap;
{
	struct tbmeter *tbm;

	if ((tbm = (struct tbmeter *)cdnr_handle2cb(ap->cdnr_handle)) == NULL)
		return (EINVAL);

	tb_import_profile(&tbm->tb, &ap->profile);

	return (0);
}

static int
cdnrcmd_tbm_stats(ap)
	struct cdnr_tbmeter_stats *ap;
{
	struct tbmeter *tbm;

	if ((tbm = (struct tbmeter *)cdnr_handle2cb(ap->cdnr_handle)) == NULL)
		return (EINVAL);

	ap->in_stats = tbm->in_stats;
	ap->out_stats = tbm->out_stats;

	return (0);
}

static int
cdnrcmd_add_trtcm(ap)
	struct cdnr_add_trtcm *ap;
{
	struct top_cdnr *top;
	struct trtcm *tcm;

	if ((top = tcb_lookup(ap->iface.cdnr_ifname)) == NULL)
		return (EBADF);

	tcm = trtcm_create(top, &ap->cmtd_profile, &ap->peak_profile,
			   &ap->green_action, &ap->yellow_action,
			   &ap->red_action, ap->coloraware);
	if (tcm == NULL)
		return (EINVAL);

	/* return a class handle to the user */
	ap->cdnr_handle = cdnr_cb2handle(&tcm->cdnrblk);
	return (0);
}

static int
cdnrcmd_modify_trtcm(ap)
	struct cdnr_modify_trtcm *ap;
{
	struct trtcm *tcm;

	if ((tcm = (struct trtcm *)cdnr_handle2cb(ap->cdnr_handle)) == NULL)
		return (EINVAL);

	tb_import_profile(&tcm->cmtd_tb, &ap->cmtd_profile);
	tb_import_profile(&tcm->peak_tb, &ap->peak_profile);

	return (0);
}

static int
cdnrcmd_tcm_stats(ap)
	struct cdnr_tcm_stats *ap;
{
	struct cdnr_block *cb;

	if ((cb = cdnr_handle2cb(ap->cdnr_handle)) == NULL)
		return (EINVAL);

	if (cb->cb_type == TCETYPE_TRTCM) {
	    struct trtcm *tcm = (struct trtcm *)cb;
	    
	    ap->green_stats = tcm->green_stats;
	    ap->yellow_stats = tcm->yellow_stats;
	    ap->red_stats = tcm->red_stats;
	}
	else if (cb->cb_type == TCETYPE_TSWTCM) {
	    struct tswtcm *tsw = (struct tswtcm *)cb;
	    
	    ap->green_stats = tsw->green_stats;
	    ap->yellow_stats = tsw->yellow_stats;
	    ap->red_stats = tsw->red_stats;
	}
	else
	    return (EINVAL);

	return (0);
}

static int
cdnrcmd_add_tbrio(ap)
	struct cdnr_add_tbrio *ap;
{
	struct top_cdnr *top;
	struct tbrio *tbrio;

	if ((top = tcb_lookup(ap->iface.cdnr_ifname)) == NULL)
		return (EBADF);

	tbrio = tbrio_create(top, &ap->profile,
			     &ap->in_action, &ap->out_action);
	if (tbrio == NULL)
		return (EINVAL);

	/* return a class handle to the user */
	ap->cdnr_handle = cdnr_cb2handle(&tbrio->cdnrblk);
	return (0);
}

static int
cdnrcmd_modify_tbrio(ap)
	struct cdnr_modify_tbrio *ap;
{
	struct tbrio *tbrio;

	if ((tbrio = (struct tbrio *)cdnr_handle2cb(ap->cdnr_handle)) == NULL)
		return (EINVAL);

	tb_import_profile(&tbrio->tb, &ap->profile);

	return (0);
}

static int
cdnrcmd_tbrio_stats(ap)
	struct cdnr_tbrio_stats *ap;
{
	struct tbrio *tbrio;

	if ((tbrio = (struct tbrio *)cdnr_handle2cb(ap->cdnr_handle)) == NULL)
		return (EINVAL);

	bcopy(tbrio->stats, ap->stats, sizeof(tbrio->stats));
	return (0);
}

static int
cdnrcmd_tbrio_getdef(ap)
	struct cdnr_tbrio_params *ap;
{
	struct tbrio *tbrio;

	if ((tbrio = (struct tbrio *)cdnr_handle2cb(ap->cdnr_handle)) == NULL)
		return (EINVAL);

	ap->avg_pkt_size = tbrio_avg_pkt_size;
	ap->holdtime_pkts = tbrio_holdtime_pkts;
	ap->lowat = tbrio_lowat;
	return (0);
}

static int
cdnrcmd_tbrio_setdef(ap)
	struct cdnr_tbrio_params *ap;
{
	struct tbrio *tbrio;

	if ((tbrio = (struct tbrio *)cdnr_handle2cb(ap->cdnr_handle)) == NULL)
		return (EINVAL);

	tbrio_avg_pkt_size = ap->avg_pkt_size;
	tbrio_holdtime_pkts = ap->holdtime_pkts;
	tbrio_lowat = ap->lowat;
	return (0);
}

static int
cdnrcmd_add_tswtcm(ap)
	struct cdnr_add_tswtcm *ap;
{
	struct top_cdnr *top;
	struct tswtcm *tsw;

	if ((top = tcb_lookup(ap->iface.cdnr_ifname)) == NULL)
		return (EBADF);

	if (ap->cmtd_rate > ap->peak_rate)
		return (EINVAL);

	tsw = tswtcm_create(top, ap->cmtd_rate, ap->peak_rate,
			    ap->avg_interval, &ap->green_action,
			    &ap->yellow_action, &ap->red_action);
	if (tsw == NULL)
	    return (EINVAL);

	/* return a class handle to the user */
	ap->cdnr_handle = cdnr_cb2handle(&tsw->cdnrblk);
	return (0);
}

static int
cdnrcmd_modify_tswtcm(ap)
	struct cdnr_modify_tswtcm *ap;
{
	struct tswtcm *tsw;

	if ((tsw = (struct tswtcm *)cdnr_handle2cb(ap->cdnr_handle)) == NULL)
		return (EINVAL);

	if (ap->cmtd_rate > ap->peak_rate)
		return (EINVAL);

	/* convert rates from bits/sec to bytes/sec */
	tsw->cmtd_rate = ap->cmtd_rate / 8;
	tsw->peak_rate = ap->peak_rate / 8;
	tsw->avg_rate = 0;

	/* timewin is converted from msec to machine clock unit */
	tsw->timewin = (u_int64_t)machclk_freq * ap->avg_interval / 1000;

	return (0);
}


static int
cdnrcmd_get_stats(ap)
	struct cdnr_get_stats *ap;
{
	struct top_cdnr *top;
	struct cdnr_block *cb;
	struct tbmeter *tbm;
	struct trtcm *tcm;
	struct tbrio *tbrio;
	struct tswtcm *tsw;
	struct tce_stats tce, *usp;
	int error, n, nskip, nelements;

	if ((top = tcb_lookup(ap->iface.cdnr_ifname)) == NULL)
		return (EBADF);

	/* copy action stats */
	bcopy(top->tc_stats, ap->stats, sizeof(ap->stats));

	/* stats for each element */
	nelements = ap->nelements;
	usp = ap->tce_stats;
	if (nelements <= 0 || usp == NULL)
		return (0);

	nskip = ap->nskip;
	n = 0;
	LIST_FOREACH(cb, &top->tc_elements, cb_next) {
		if (nskip > 0) {
			nskip--;
			continue;
		}

		bzero(&tce, sizeof(tce));
		tce.tce_handle = cb->cb_handle;
		tce.tce_type = cb->cb_type;
		switch (cb->cb_type) {
		case TCETYPE_TBMETER:
			tbm = (struct tbmeter *)cb;
			tce.tce_stats[0] = tbm->in_stats;
			tce.tce_stats[1] = tbm->out_stats;
			break;
		case TCETYPE_TRTCM:
			tcm = (struct trtcm *)cb;
			tce.tce_stats[0] = tcm->green_stats;
			tce.tce_stats[1] = tcm->yellow_stats;
			tce.tce_stats[2] = tcm->red_stats;
			break;
		case TCETYPE_TBRIO:
			tbrio = (struct tbrio *)cb;
			bcopy(tbrio->stats, tce.tce_stats, sizeof(tbrio->stats));
#if 1
			/* XXX return prob using drop count for green */
			tce.tce_stats[1].packets = tbrio->prob;
#endif
			break;
		case TCETYPE_TSWTCM:
			tsw = (struct tswtcm *)cb;
			tce.tce_stats[0] = tsw->green_stats;
			tce.tce_stats[1] = tsw->yellow_stats;
			tce.tce_stats[2] = tsw->red_stats;
			break;
		default:
			continue;
		}

		if ((error = copyout((caddr_t)&tce, (caddr_t)usp++,
				     sizeof(tce))) != 0)
			return (error);

		if (++n == nelements)
			break;
	}
	ap->nelements = n;

	return (0);
}

/*
 * conditioner device interface
 */
int
cdnropen(dev, flag, fmt, p)
	dev_t dev;
	int flag, fmt;
	struct proc *p;
{
	if (machclk_freq == 0)
		init_machclk();

	if (machclk_freq == 0) {
		printf("cdnr: no cpu clock available!\n");
		return (ENXIO);
	}

	/* everything will be done when the queueing scheme is attached. */
	return 0;
}

int
cdnrclose(dev, flag, fmt, p)
	dev_t dev;
	int flag, fmt;
	struct proc *p;
{
	struct top_cdnr *top;
	int err, error = 0;

	while ((top = LIST_FIRST(&tcb_list)) != NULL) {
		/* destroy all */
		err = top_destroy(top);
		if (err != 0 && error == 0)
			error = err;
	}
	altq_input = NULL;

	return (error);
}

int
cdnrioctl(dev, cmd, addr, flag, p)
	dev_t dev;
	ioctlcmd_t cmd;
	caddr_t addr;
	int flag;
	struct proc *p;
{
	struct top_cdnr *top;
	struct cdnr_interface *ifacep;
	int	s, error = 0;

	/* check super-user privilege */
	switch (cmd) {
	case CDNR_GETSTATS:
		break;
	default:
#if (__FreeBSD_version > 400000)
		if ((error = suser(p)) != 0)
#else
		if ((error = suser(p->p_ucred, &p->p_acflag)) != 0)
#endif
			return (error);
		break;
	}
    
	s = splimp();
	switch (cmd) {
		
	case CDNR_IF_ATTACH:
		ifacep = (struct cdnr_interface *)addr;
		error = cdnrcmd_if_attach(ifacep->cdnr_ifname);
		break;

	case CDNR_IF_DETACH:
		ifacep = (struct cdnr_interface *)addr;
		error = cdnrcmd_if_detach(ifacep->cdnr_ifname);
		break;

	case CDNR_ENABLE:
	case CDNR_DISABLE:
		ifacep = (struct cdnr_interface *)addr;
		if ((top = tcb_lookup(ifacep->cdnr_ifname)) == NULL) {
			error = EBADF;
			break;
		}

		switch (cmd) {

		case CDNR_ENABLE:
			SET_CNDTNING(top->tc_ifp);
			if (altq_input == NULL)
				altq_input = altq_cdnr_input;
			break;

		case CDNR_DISABLE:
			CLEAR_CNDTNING(top->tc_ifp);
			LIST_FOREACH(top, &tcb_list, tc_next)
				if (ALTQ_IS_CNDTNING(top->tc_ifp))
					break;
			if (top == NULL)
				altq_input = NULL;
			break;
		}
		break;

	case CDNR_ADD_ELEM:
		error = cdnrcmd_add_element((struct cdnr_add_element *)addr);
		break;

	case CDNR_DEL_ELEM:
		error = cdnrcmd_delete_element((struct cdnr_delete_element *)addr);
		break;

	case CDNR_ADD_TBM:
		error = cdnrcmd_add_tbm((struct cdnr_add_tbmeter *)addr);
		break;

	case CDNR_MOD_TBM:
		error = cdnrcmd_modify_tbm((struct cdnr_modify_tbmeter *)addr);
		break;

	case CDNR_TBM_STATS:
		error = cdnrcmd_tbm_stats((struct cdnr_tbmeter_stats *)addr);
		break;

	case CDNR_ADD_TCM:
		error = cdnrcmd_add_trtcm((struct cdnr_add_trtcm *)addr);
		break;

	case CDNR_MOD_TCM:
		error = cdnrcmd_modify_trtcm((struct cdnr_modify_trtcm *)addr);
		break;

	case CDNR_TCM_STATS:
		error = cdnrcmd_tcm_stats((struct cdnr_tcm_stats *)addr);
		break;

	case CDNR_ADD_FILTER:
		error = cdnrcmd_add_filter((struct cdnr_add_filter *)addr);
		break;

	case CDNR_DEL_FILTER:
		error = cdnrcmd_delete_filter((struct cdnr_delete_filter *)addr);
		break;

	case CDNR_GETSTATS:
		error = cdnrcmd_get_stats((struct cdnr_get_stats *)addr);
		break;

	case CDNR_ADD_TBRIO:
		error = cdnrcmd_add_tbrio((struct cdnr_add_tbrio *)addr);
		break;

	case CDNR_MOD_TBRIO:
		error = cdnrcmd_modify_tbrio((struct cdnr_modify_tbrio *)addr);
		break;

	case CDNR_TBRIO_STATS:
		error = cdnrcmd_tbrio_stats((struct cdnr_tbrio_stats *)addr);
		break;

	case CDNR_TBRIO_GETDEFAULTS:
		error = cdnrcmd_tbrio_getdef((struct cdnr_tbrio_params *)addr);
		break;

	case CDNR_TBRIO_SETDEFAULTS:
		error = cdnrcmd_tbrio_setdef((struct cdnr_tbrio_params *)addr);
		break;

	case CDNR_ADD_TSW:
		error = cdnrcmd_add_tswtcm((struct cdnr_add_tswtcm *)addr);
		break;

	case CDNR_MOD_TSW:
		error = cdnrcmd_modify_tswtcm((struct cdnr_modify_tswtcm *)addr);
		break;

	default:
		error = EINVAL;
		break;
	}
	splx(s);

	return error;
}

#ifdef KLD_MODULE

static struct altqsw cdnr_sw =
	{"cdnr", cdnropen, cdnrclose, cdnrioctl};

ALTQ_MODULE(altq_cdnr, ALTQT_CDNR, &cdnr_sw);

#endif /* KLD_MODULE */

#endif /* CDNR */
