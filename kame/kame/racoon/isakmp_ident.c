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
/* YIPS @(#)$Id: isakmp_ident.c,v 1.35 2000/07/04 16:35:59 sakane Exp $ */

/* Identity Protecion Exchange (Main Mode) */

#include <sys/types.h>
#include <sys/param.h>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#if TIME_WITH_SYS_TIME
# include <sys/time.h>
# include <time.h>
#else
# if HAVE_SYS_TIME_H
#  include <sys/time.h>
# else
#  include <time.h>
# endif
#endif

#include "var.h"
#include "misc.h"
#include "vmbuf.h"
#include "plog.h"
#include "sockmisc.h"
#include "schedule.h"
#include "debug.h"

#include "localconf.h"
#include "remoteconf.h"
#include "isakmp_var.h"
#include "isakmp.h"
#include "handler.h"
#include "oakley.h"
#include "ipsec_doi.h"
#include "crypto_openssl.h"
#include "pfkey.h"
#include "isakmp_ident.h"
#include "isakmp_inf.h"
#include "vendorid.h"

static vchar_t *ident_ir2sendmx __P((struct ph1handle *));
static vchar_t *ident_ir3sendmx __P((struct ph1handle *));

/* %%%
 * begin Identity Protection Mode as initiator.
 */
/*
 * send to responder
 * 	psk: HDR, SA
 * 	sig: HDR, SA
 * 	rsa: HDR, SA
 * 	rev: HDR, SA
 */
int
ident_i1send(iph1, msg)
	struct ph1handle *iph1;
	vchar_t *msg; /* must be null */
{
	struct isakmp_gen *gen;
	caddr_t p;
	int tlen;
	int error = -1;

	YIPSDEBUG(DEBUG_STAMP, plog(logp, LOCATION, NULL, "begin.\n"));

	/* validity check */
	if (iph1->status != PHASE1ST_START) {
		plog(logp, LOCATION, NULL,
			"status mismatched %d.\n", iph1->status);
		goto end;
	}

	/* create isakmp index */
	memset(&iph1->index, 0, sizeof(iph1->index));
	isakmp_newcookie((caddr_t)&iph1->index, iph1->remote, iph1->local);

	/* create SA payload for my proposal */
	iph1->sa = ipsecdoi_setph1proposal(iph1->rmconf->proposal);
	if (iph1->sa == NULL)
		goto end;

	/* create buffer to send isakmp payload */
	tlen = sizeof(struct isakmp)
		+ sizeof(*gen) + iph1->sa->l;

	iph1->sendbuf = vmalloc(tlen);
	if (iph1->sendbuf == NULL) {
		plog(logp, LOCATION, NULL,
			"failed to get buffer to send.\n");
		goto end;
	}

	/* set isakmp header */
	p = set_isakmp_header(iph1->sendbuf, iph1, ISAKMP_NPTYPE_SA);
	if (p == NULL)
		goto end;

	/* set SA payload to propose */
	p = set_isakmp_payload(p, iph1->sa, ISAKMP_NPTYPE_NONE);

#ifdef HAVE_PRINT_ISAKMP_C
	isakmp_printpacket(iph1->sendbuf, iph1->local, iph1->remote, 0);
#endif

	/* send to responder */
	if (isakmp_send(iph1, iph1->sendbuf) < 0)
		goto end;

	iph1->status = PHASE1ST_MSG1SENT;

	iph1->retry_counter = iph1->rmconf->retry_counter;
	iph1->scr = sched_new(iph1->rmconf->retry_interval,
			isakmp_ph1resend, iph1);

	error = 0;

end:

	return error;
}

/*
 * receive from responder
 * 	psk: HDR, SA
 * 	sig: HDR, SA
 * 	rsa: HDR, SA
 * 	rev: HDR, SA
 */
int
ident_i2recv(iph1, msg)
	struct ph1handle *iph1;
	vchar_t *msg;
{
	vchar_t *pbuf = NULL;
	struct isakmp_parse_t *pa;
	vchar_t *satmp = NULL;
	int error = -1;

	YIPSDEBUG(DEBUG_STAMP, plog(logp, LOCATION, NULL, "begin.\n"));

	/* validity check */
	if (iph1->status != PHASE1ST_MSG1SENT) {
		plog(logp, LOCATION, NULL,
			"status mismatched %d.\n", iph1->status);
		goto end;
	}

	/* validate the type of next payload */
	/*
	 * NOTE: RedCreek(as responder) attaches N[responder-lifetime] here,
	 *	if proposal-lifetime > lifetime-redcreek-wants.
	 *	(see doi-08 4.5.4)
	 *	=> According to the seciton 4.6.3 in RFC 2407, This is illegal.
	 * NOTE: we do not really care about ordering of VID and N.
	 *	does it matters?
	 * NOTE: even if there's multiple VID/N, we'll ignore them.
	 */
	pbuf = isakmp_parse(msg);
	if (pbuf == NULL)
		goto end;
	pa = (struct isakmp_parse_t *)pbuf->v;

	/* SA payload is fixed postion */
	if (pa->type != ISAKMP_NPTYPE_SA) {
		plog(logp, LOCATION, iph1->remote,
			"received invalid next payload type %d, "
			"expecting %d.\n",
			pa->type, ISAKMP_NPTYPE_SA);
		goto end;
	}
	if (isakmp_p2ph(&satmp, pa->ptr) < 0)
		goto end;
	pa++;

	for (/*nothing*/;
	     pa->type != ISAKMP_NPTYPE_NONE;
	     pa++) {

		switch (pa->type) {
		case ISAKMP_NPTYPE_VID:
			YIPSDEBUG(DEBUG_NOTIFY,
				plog(logp, LOCATION, iph1->remote,
				"peer transmitted Vendor ID.\n"));
			(void)check_vendorid(pa->ptr);
			break;
		default:
			/* don't send information, see ident_r1recv() */
			plog(logp, LOCATION, iph1->remote,
				"ignore the packet, "
				"received unexpecting payload type %d.\n",
				pa->type);
			goto end;
		}
	}

	/* check SA payload and set approval SA for use */
	if (ipsecdoi_checkph1proposal(satmp, iph1) < 0) {
		plog(logp, LOCATION, iph1->remote,
			"failed to get valid proposal.\n");
		/* XXX send information */
		goto end;
	}
	if (iph1->sa_ret) {
		vfree(iph1->sa_ret);
		iph1->sa_ret = NULL;
	}

	iph1->status = PHASE1ST_MSG2RECEIVED;

	error = 0;

end:
	if (pbuf)
		vfree(pbuf);
	if (satmp)
		vfree(satmp);
	return error;
}

/*
 * send to responder
 * 	psk: HDR, KE, Ni
 * 	sig: HDR, KE, Ni
 * 	rsa: HDR, KE, [ HASH(1), ] <IDi1_b>PubKey_r, <Ni_b>PubKey_r
 * 	rev: HDR, [ HASH(1), ] <Ni_b>Pubkey_r, <KE_b>Ke_i,
 * 	          <IDi1_b>Ke_i, [<<Cert-I_b>Ke_i]
 */
int
ident_i2send(iph1, msg)
	struct ph1handle *iph1;
	vchar_t *msg;
{
	int error = -1;

	YIPSDEBUG(DEBUG_STAMP, plog(logp, LOCATION, NULL, "begin.\n"));

	/* validity check */
	if (iph1->status != PHASE1ST_MSG2RECEIVED) {
		plog(logp, LOCATION, NULL,
			"status mismatched %d.\n", iph1->status);
		goto end;
	}

	/* fix isakmp index */
	memcpy(&iph1->index.r_ck, &((struct isakmp *)msg->v)->r_ck,
		sizeof(cookie_t));

	/* generate DH public value */
	if (oakley_dh_generate(iph1->approval->dhgrp,
				&iph1->dhpub, &iph1->dhpriv) < 0)
		goto end;

	/* generate NONCE value */
	iph1->nonce = eay_set_random(iph1->rmconf->nonce_size);
	if (iph1->nonce == NULL)
		goto end;

	/* create buffer to send isakmp payload */
	iph1->sendbuf = ident_ir2sendmx(iph1);
	if (iph1->sendbuf == NULL)
		goto end;

	iph1->status = PHASE1ST_MSG2SENT;

	/* add to the schedule to resend, and seve back pointer. */
	iph1->retry_counter = iph1->rmconf->retry_counter;
	iph1->scr = sched_new(iph1->rmconf->retry_interval,
			isakmp_ph1resend, iph1);

	error = 0;

end:
	return error;
}

/*
 * receive from responder
 * 	psk: HDR, KE, Nr
 * 	sig: HDR, KE, Nr [, CR ]
 * 	rsa: HDR, KE, <IDr1_b>PubKey_i, <Nr_b>PubKey_i
 * 	rev: HDR, <Nr_b>PubKey_i, <KE_b>Ke_r, <IDr1_b>Ke_r,
 */
int
ident_i3recv(iph1, msg)
	struct ph1handle *iph1;
	vchar_t *msg;
{
	vchar_t *pbuf = NULL;
	struct isakmp_parse_t *pa;
	int error = -1;

	YIPSDEBUG(DEBUG_STAMP, plog(logp, LOCATION, NULL, "begin.\n"));

	/* validity check */
	if (iph1->status != PHASE1ST_MSG2SENT) {
		plog(logp, LOCATION, NULL,
			"status mismatched %d.\n", iph1->status);
		goto end;
	}

	/* validate the type of next payload */
	pbuf = isakmp_parse(msg);
	if (pbuf == NULL)
		goto end;

	for (pa = (struct isakmp_parse_t *)pbuf->v;
	     pa->type != ISAKMP_NPTYPE_NONE;
	     pa++) {

		switch (pa->type) {
		case ISAKMP_NPTYPE_KE:
			if (isakmp_p2ph(&iph1->dhpub_p, pa->ptr) < 0)
				goto end;
			break;
		case ISAKMP_NPTYPE_NONCE:
			if (isakmp_p2ph(&iph1->nonce_p, pa->ptr) < 0)
				goto end;
			break;
		case ISAKMP_NPTYPE_VID:
			YIPSDEBUG(DEBUG_NOTIFY,
				plog(logp, LOCATION, iph1->remote,
				"peer transmitted Vendor ID.\n"));
			(void)check_vendorid(pa->ptr);
			break;
		case ISAKMP_NPTYPE_CR:
			if (isakmp_p2ph(&iph1->cr_p, pa->ptr) < 0)
				goto end;
			break;
		default:
			/* don't send information, see ident_r1recv() */
			plog(logp, LOCATION, iph1->remote,
				"ignore the packet, "
				"received unexpecting payload type %d.\n",
				pa->type);
			goto end;
		}
	}

	/* payload existency check */
	if (iph1->dhpub_p == NULL || iph1->nonce_p == NULL) {
		plog(logp, LOCATION, iph1->remote,
			"few isakmp message received.\n");
		goto end;
	}

	if (oakley_checkcr(iph1) < 0) {
		/* Ignore this error in order to be interoperability. */
		;
	}

	iph1->status = PHASE1ST_MSG3RECEIVED;

	error = 0;

end:
	if (pbuf)
		vfree(pbuf);
	if (error) {
		VPTRINIT(iph1->dhpub_p);
		VPTRINIT(iph1->nonce_p);
		VPTRINIT(iph1->id_p);
		VPTRINIT(iph1->cr_p);
	}

	return error;
}

/*
 * send to responder
 * 	psk: HDR*, IDi1, HASH_I
 * 	sig: HDR*, IDi1, [ CR, ] [ CERT, ] SIG_I
 * 	rsa: HDR*, HASH_I
 * 	rev: HDR*, HASH_I
 */
int
ident_i3send(iph1, msg)
	struct ph1handle *iph1;
	vchar_t *msg;
{
	int error = -1;

	YIPSDEBUG(DEBUG_STAMP, plog(logp, LOCATION, NULL, "begin.\n"));

	/* validity check */
	if (iph1->status != PHASE1ST_MSG3RECEIVED) {
		plog(logp, LOCATION, NULL,
			"status mismatched %d.\n", iph1->status);
		goto end;
	}

	/* compute sharing secret of DH */
	if (oakley_dh_compute(iph1->approval->dhgrp, iph1->dhpub,
				iph1->dhpriv, iph1->dhpub_p, &iph1->dhgxy) < 0)
		goto end;

	/* generate SKEYIDs & IV & final cipher key */
	if (oakley_skeyid(iph1) < 0)
		goto end;
	if (oakley_skeyid_dae(iph1) < 0)
		goto end;
	if (oakley_compute_enckey(iph1) < 0)
		goto end;
	if (oakley_newiv(iph1) < 0)
		goto end;

	/* make ID payload into isakmp status */
	if (ipsecdoi_setid1(iph1) < 0)
		goto end;

	/* generate HASH to send */
	iph1->hash = oakley_ph1hash_common(iph1, GENERATE);
	if (iph1->hash == NULL)
		goto end;

	/* set encryption flag */
	iph1->flags |= ISAKMP_FLAG_E;

	/* create HDR;ID;HASH payload */
	iph1->sendbuf = ident_ir3sendmx(iph1);
	if (iph1->sendbuf == NULL)
		goto end;

	iph1->status = PHASE1ST_MSG3SENT;

	iph1->retry_counter = iph1->rmconf->retry_counter;
	iph1->scr = sched_new(iph1->rmconf->retry_interval,
			isakmp_ph1resend, iph1);

	error = 0;

end:
	return error;
}

/*
 * receive from responder
 * 	psk: HDR*, IDr1, HASH_R
 * 	sig: HDR*, IDr1, [ CERT, ] SIG_R
 * 	rsa: HDR*, HASH_R
 * 	rev: HDR*, HASH_R
 */
int
ident_i4recv(iph1, msg0)
	struct ph1handle *iph1;
	vchar_t *msg0;
{
	vchar_t *pbuf = NULL;
	struct isakmp_parse_t *pa;
	vchar_t *msg = NULL;
	int error = -1;

	YIPSDEBUG(DEBUG_STAMP, plog(logp, LOCATION, NULL, "begin.\n"));

	/* validity check */
	if (iph1->status != PHASE1ST_MSG3SENT) {
		plog(logp, LOCATION, NULL,
			"status mismatched %d.\n", iph1->status);
		goto end;
	}

	/* decrypting */
	if (!ISSET(((struct isakmp *)msg0->v)->flags, ISAKMP_FLAG_E)) {
		plog(logp, LOCATION, iph1->remote,
			"ignore the packet, "
			"expecting the packet encrypted.\n");
		goto end;
	}
	msg = oakley_do_decrypt(iph1, msg0, iph1->ivm->iv, iph1->ivm->ive);
	if (msg == NULL)
		goto end;

	/* validate the type of next payload */
	pbuf = isakmp_parse(msg);
	if (pbuf == NULL)
		goto end;

	iph1->pl_hash = NULL;

	for (pa = (struct isakmp_parse_t *)pbuf->v;
	     pa->type != ISAKMP_NPTYPE_NONE;
	     pa++) {

		switch (pa->type) {
		case ISAKMP_NPTYPE_ID:
			if (isakmp_p2ph(&iph1->id_p, pa->ptr) < 0)
				goto end;
			if (ipsecdoi_checkid1(iph1) < 0) {
				plog(logp, LOCATION, iph1->remote,
					"invalid ID payload.\n");
				goto end;
			}
			break;
		case ISAKMP_NPTYPE_HASH:
			iph1->pl_hash = (struct isakmp_pl_hash *)pa->ptr;
			break;
		case ISAKMP_NPTYPE_CERT:
			if (oakley_savecert(iph1, pa->ptr) < 0)
				goto end;
			break;
		case ISAKMP_NPTYPE_SIG:
			if (isakmp_p2ph(&iph1->sig_p, pa->ptr) < 0)
				goto end;
			break;
		case ISAKMP_NPTYPE_VID:
			YIPSDEBUG(DEBUG_NOTIFY,
				plog(logp, LOCATION, iph1->remote,
				"peer transmitted Vendor ID.\n"));
			(void)check_vendorid(pa->ptr);
			break;
		case ISAKMP_NPTYPE_N:
			YIPSDEBUG(DEBUG_NOTIFY,
				plog(logp, LOCATION, iph1->remote,
				"peer transmitted Notify Message.\n"));
			isakmp_check_notify(pa->ptr, iph1);
			break;
		default:
			/* don't send information, see ident_r1recv() */
			plog(logp, LOCATION, iph1->remote,
				"ignore the packet, "
				"received unexpecting payload type %d.\n",
				pa->type);
			goto end;
		}
	}

	/* payload existency check */
	/* validate authentication value */
    {
	int type;
	type = oakley_validate_auth(iph1);
	if (type != 0) {
		if (type == -1) {
			/* message printed inner oakley_validate_auth() */
			goto end;
		}
		isakmp_info_send_n1(iph1, type, NULL);
		goto end;
	}
    }

	/*
	 * XXX: Should we do compare two addresses, ph1handle's and ID
	 * payload's.
	 */

	YIPSDEBUG(DEBUG_MISC,
		plog(logp, LOCATION, iph1->remote, "ID ");
		hexdump((caddr_t)iph1->id_p, iph1->id_p->l));

	iph1->status = PHASE1ST_MSG4RECEIVED;

	error = 0;

end:
	if (pbuf)
		vfree(pbuf);
	if (msg)
		vfree(msg);

	if (error) {
		VPTRINIT(iph1->id_p);
		VPTRINIT(iph1->cert_p);
		VPTRINIT(iph1->crl_p);
		VPTRINIT(iph1->sig_p);
	}

	return error;
}

/*
 * status update and establish isakmp sa.
 */
int
ident_i4send(iph1, msg)
	struct ph1handle *iph1;
	vchar_t *msg;
{
	int error = -1;

	YIPSDEBUG(DEBUG_STAMP, plog(logp, LOCATION, NULL, "begin.\n"));

	/* validity check */
	if (iph1->status != PHASE1ST_MSG4RECEIVED) {
		plog(logp, LOCATION, NULL,
			"status mismatched %d.\n", iph1->status);
		goto end;
	}

	/* synchronization IV */
	memcpy(iph1->ivm->ivd->v, iph1->ivm->ive->v, iph1->ivm->iv->l);
	memcpy(iph1->ivm->iv->v, iph1->ivm->ive->v, iph1->ivm->iv->l);

	iph1->status = PHASE1ST_ESTABLISHED;

	error = 0;

end:
	return error;
}

/*
 * receive from initiator
 * 	psk: HDR, SA
 * 	sig: HDR, SA
 * 	rsa: HDR, SA
 * 	rev: HDR, SA
 */
int
ident_r1recv(iph1, msg)
	struct ph1handle *iph1;
	vchar_t *msg;
{
	vchar_t *pbuf = NULL;
	struct isakmp_parse_t *pa;
	int error = -1;

	YIPSDEBUG(DEBUG_STAMP, plog(logp, LOCATION, NULL, "begin.\n"));

	/* validity check */
	if (iph1->status != PHASE1ST_START) {
		plog(logp, LOCATION, NULL,
			"status mismatched %d.\n", iph1->status);
		goto end;
	}

	/* validate the type of next payload */
	/*
	 * NOTE: XXX even if multiple VID, we'll silently ignore those.
	 */
	pbuf = isakmp_parse(msg);
	if (pbuf == NULL)
		goto end;
	pa = (struct isakmp_parse_t *)pbuf->v;

	/* check the position of SA payload */
	if (pa->type != ISAKMP_NPTYPE_SA) {
		plog(logp, LOCATION, iph1->remote,
			"received invalid next payload type %d, "
			"expecting %d.\n",
			pa->type, ISAKMP_NPTYPE_SA);
		goto end;
	}
	if (isakmp_p2ph(&iph1->sa, pa->ptr) < 0)
		goto end;
	pa++;

	for (/*nothing*/;
	     pa->type != ISAKMP_NPTYPE_NONE;
	     pa++) {

		switch (pa->type) {
		case ISAKMP_NPTYPE_VID:
			YIPSDEBUG(DEBUG_NOTIFY,
				plog(logp, LOCATION, iph1->remote,
				"peer transmitted Vendor ID.\n"));
			(void)check_vendorid(pa->ptr);
			break;
		default:
			/*
			 * We don't send information to the peer even
			 * if we received malformed packet.  Because we
			 * can't distinguish the malformed packet and
			 * the re-sent packet.  And we do same behavior
			 * when we expect encrypted packet.
			 */
			plog(logp, LOCATION, iph1->remote,
				"ignore the packet, "
				"received unexpecting payload type %d.\n",
				pa->type);
			goto end;
		}
	}

	/* check SA payload and set approval SA for use */
	if (ipsecdoi_checkph1proposal(iph1->sa, iph1) < 0) {
		plog(logp, LOCATION, iph1->remote,
			"failed to get valid proposal.\n");
		/* XXX send information */
		goto end;
	}

	iph1->status = PHASE1ST_MSG1RECEIVED;

	error = 0;

end:
	if (pbuf)
		vfree(pbuf);
	if (error) {
		VPTRINIT(iph1->sa);
	}

	return error;
}

/*
 * send to initiator
 * 	psk: HDR, SA
 * 	sig: HDR, SA
 * 	rsa: HDR, SA
 * 	rev: HDR, SA
 */
int
ident_r1send(iph1, msg)
	struct ph1handle *iph1;
	vchar_t *msg;
{
	struct isakmp_gen *gen;
	caddr_t p;
	int tlen;
	int error = -1;

	YIPSDEBUG(DEBUG_STAMP, plog(logp, LOCATION, NULL, "begin.\n"));

	/* validity check */
	if (iph1->status != PHASE1ST_MSG1RECEIVED) {
		plog(logp, LOCATION, NULL,
			"status mismatched %d.\n", iph1->status);
		goto end;
	}

	/* set responder's cookie */
	isakmp_newcookie((caddr_t)&iph1->index.r_ck, iph1->remote, iph1->local);

	/* create buffer to send isakmp payload */
	tlen = sizeof(struct isakmp)
		+ sizeof(*gen) + iph1->sa_ret->l;

	iph1->sendbuf = vmalloc(tlen);
	if (iph1->sendbuf == NULL) { 
		plog(logp, LOCATION, NULL,
			"failed to get buffer to send.\n");
		goto end;
	}

	/* set isakmp header */
	p = set_isakmp_header(iph1->sendbuf, iph1, ISAKMP_NPTYPE_SA);
	if (p == NULL)
		goto end;

	/* set SA payload to reply */
	p = set_isakmp_payload(p, iph1->sa_ret, ISAKMP_NPTYPE_NONE);

#ifdef HAVE_PRINT_ISAKMP_C
	isakmp_printpacket(iph1->sendbuf, iph1->local, iph1->remote, 0);
#endif

	/* send to responder */
	if (isakmp_send(iph1, iph1->sendbuf) < 0)
		goto end;

	iph1->status = PHASE1ST_MSG1SENT;

	iph1->retry_counter = iph1->rmconf->retry_counter;
	iph1->scr = sched_new(iph1->rmconf->retry_interval,
			isakmp_ph1resend, iph1);

	error = 0;

end:
	return error;
}

/*
 * receive from initiator
 * 	psk: HDR, KE, Ni
 * 	sig: HDR, KE, Ni
 * 	rsa: HDR, KE, [ HASH(1), ] <IDi1_b>PubKey_r, <Ni_b>PubKey_r
 * 	rev: HDR, [ HASH(1), ] <Ni_b>Pubkey_r, <KE_b>Ke_i,
 * 	          <IDi1_b>Ke_i, [<<Cert-I_b>Ke_i]
 */
int
ident_r2recv(iph1, msg)
	struct ph1handle *iph1;
	vchar_t *msg;
{
	vchar_t *pbuf = NULL;
	struct isakmp_parse_t *pa;
	int error = -1;

	YIPSDEBUG(DEBUG_STAMP, plog(logp, LOCATION, NULL, "begin.\n"));

	/* validity check */
	if (iph1->status != PHASE1ST_MSG1SENT) {
		plog(logp, LOCATION, NULL,
			"status mismatched %d.\n", iph1->status);
		goto end;
	}

	/* validate the type of next payload */
	pbuf = isakmp_parse(msg);
	if (pbuf == NULL)
		goto end;

	for (pa = (struct isakmp_parse_t *)pbuf->v;
	     pa->type != ISAKMP_NPTYPE_NONE;
	     pa++) {

		switch (pa->type) {
		case ISAKMP_NPTYPE_KE:
			if (isakmp_p2ph(&iph1->dhpub_p, pa->ptr) < 0)
				goto end;
			break;
		case ISAKMP_NPTYPE_NONCE:
			if (isakmp_p2ph(&iph1->nonce_p, pa->ptr) < 0)
				goto end;
			break;
		case ISAKMP_NPTYPE_VID:
			YIPSDEBUG(DEBUG_NOTIFY,
				plog(logp, LOCATION, iph1->remote,
				"peer transmitted Vendor ID.\n"));
			(void)check_vendorid(pa->ptr);
			break;
		case ISAKMP_NPTYPE_CR:
			plog(logp, LOCATION, iph1->remote,
				"NOTICE: CR received, ignore it."
				"It should be in other exchange.\n");
			break;
		default:
			/* don't send information, see ident_r1recv() */
			plog(logp, LOCATION, iph1->remote,
				"ignore the packet, "
				"received unexpecting payload type %d.\n",
				pa->type);
			goto end;
		}
	}

	/* payload existency check */
	if (iph1->dhpub_p == NULL || iph1->nonce_p == NULL) {
		plog(logp, LOCATION, iph1->remote,
			"few isakmp message received.\n");
		goto end;
	}

	iph1->status = PHASE1ST_MSG2RECEIVED;

	error = 0;

end:
	if (pbuf)
		vfree(pbuf);

	if (error) {
		VPTRINIT(iph1->dhpub_p);
		VPTRINIT(iph1->nonce_p);
		VPTRINIT(iph1->id_p);
	}

	return error;
}

/*
 * send to initiator
 * 	psk: HDR, KE, Nr
 * 	sig: HDR, KE, Nr [, CR ]
 * 	rsa: HDR, KE, <IDr1_b>PubKey_i, <Nr_b>PubKey_i
 * 	rev: HDR, <Nr_b>PubKey_i, <KE_b>Ke_r, <IDr1_b>Ke_r,
 */
int
ident_r2send(iph1, msg)
	struct ph1handle *iph1;
	vchar_t *msg;
{
	int error = -1;

	YIPSDEBUG(DEBUG_STAMP, plog(logp, LOCATION, NULL, "begin.\n"));

	/* validity check */
	if (iph1->status != PHASE1ST_MSG2RECEIVED) {
		plog(logp, LOCATION, NULL,
			"status mismatched %d.\n", iph1->status);
		goto end;
	}

	/* generate DH public value */
	if (oakley_dh_generate(iph1->approval->dhgrp,
				&iph1->dhpub, &iph1->dhpriv) < 0)
		goto end;

	/* generate NONCE value */
	iph1->nonce = eay_set_random(iph1->rmconf->nonce_size);
	if (iph1->nonce == NULL)
		goto end;

	/* create HDR;KE;NONCE payload */
	iph1->sendbuf = ident_ir2sendmx(iph1);
	if (iph1->sendbuf == NULL)
		goto end;

	/* compute sharing secret of DH */
	if (oakley_dh_compute(iph1->approval->dhgrp, iph1->dhpub,
				iph1->dhpriv, iph1->dhpub_p, &iph1->dhgxy) < 0)
		goto end;

	/* generate SKEYIDs & IV & final cipher key */
	if (oakley_skeyid(iph1) < 0)
		goto end;
	if (oakley_skeyid_dae(iph1) < 0)
		goto end;
	if (oakley_compute_enckey(iph1) < 0)
		goto end;
	if (oakley_newiv(iph1) < 0)
		goto end;

	iph1->status = PHASE1ST_MSG2SENT;

	iph1->retry_counter = iph1->rmconf->retry_counter;
	iph1->scr = sched_new(iph1->rmconf->retry_interval,
			isakmp_ph1resend, iph1);

	error = 0;

end:
	return error;
}

/*
 * receive from initiator
 * 	psk: HDR*, IDi1, HASH_I
 * 	sig: HDR*, IDi1, [ CR, ] [ CERT, ] SIG_I
 * 	rsa: HDR*, HASH_I
 * 	rev: HDR*, HASH_I
 */
int
ident_r3recv(iph1, msg0)
	struct ph1handle *iph1;
	vchar_t *msg0;
{
	vchar_t *msg = NULL;
	vchar_t *pbuf = NULL;
	struct isakmp_parse_t *pa;
	int error = -1;

	YIPSDEBUG(DEBUG_STAMP, plog(logp, LOCATION, NULL, "begin.\n"));

	/* validity check */
	if (iph1->status != PHASE1ST_MSG2SENT) {
		plog(logp, LOCATION, NULL,
			"status mismatched %d.\n", iph1->status);
		goto end;
	}

	/* decrypting */
	if (!ISSET(((struct isakmp *)msg0->v)->flags, ISAKMP_FLAG_E)) {
		plog(logp, LOCATION, iph1->remote,
			"ignore the packet, "
			"expecting the packet encrypted.\n");
		goto end;
	}
	msg = oakley_do_decrypt(iph1, msg0, iph1->ivm->iv, iph1->ivm->ive);
	if (msg == NULL)
		goto end;

	/* validate the type of next payload */
	pbuf = isakmp_parse(msg);
	if (pbuf == NULL)
		goto end;

	iph1->pl_hash = NULL;

	for (pa = (struct isakmp_parse_t *)pbuf->v;
	     pa->type != ISAKMP_NPTYPE_NONE;
	     pa++) {

		switch (pa->type) {
		case ISAKMP_NPTYPE_ID:
			if (isakmp_p2ph(&iph1->id_p, pa->ptr) < 0)
				goto end;
			if (ipsecdoi_checkid1(iph1) < 0) {
				plog(logp, LOCATION, iph1->remote,
					"invalid ID payload.\n");
				goto end;
			}
			break;
		case ISAKMP_NPTYPE_HASH:
			iph1->pl_hash = (struct isakmp_pl_hash *)pa->ptr;
			break;
		case ISAKMP_NPTYPE_CR:
			if (isakmp_p2ph(&iph1->cr_p, pa->ptr) < 0)
				goto end;
			break;
		case ISAKMP_NPTYPE_CERT:
			if (oakley_savecert(iph1, pa->ptr) < 0)
				goto end;
			break;
		case ISAKMP_NPTYPE_SIG:
			if (isakmp_p2ph(&iph1->sig_p, pa->ptr) < 0)
				goto end;
			break;
		case ISAKMP_NPTYPE_VID:
			YIPSDEBUG(DEBUG_NOTIFY,
				plog(logp, LOCATION, iph1->remote,
				"peer transmitted Vendor ID.\n"));
			(void)check_vendorid(pa->ptr);
			break;
		case ISAKMP_NPTYPE_N:
			YIPSDEBUG(DEBUG_NOTIFY,
				plog(logp, LOCATION, iph1->remote,
				"peer transmitted Notify Message.\n"));
			isakmp_check_notify(pa->ptr, iph1);
			break;
		default:
			/* don't send information, see ident_r1recv() */
			plog(logp, LOCATION, iph1->remote,
				"ignore the packet, "
				"received unexpecting payload type %d.\n",
				pa->type);
			goto end;
		}
	}

	/* payload existency check */
	/* XXX same as ident_i4recv(), should be merged. */
    {
	int ng = 0;

	switch (iph1->approval->authmethod) {
	case OAKLEY_ATTR_AUTH_METHOD_PSKEY:
		if (iph1->id_p == NULL || iph1->pl_hash == NULL)
			ng++;
		break;
	case OAKLEY_ATTR_AUTH_METHOD_DSSSIG:
	case OAKLEY_ATTR_AUTH_METHOD_RSASIG:
		if (iph1->id_p == NULL || iph1->sig_p == NULL)
			ng++;
		break;
	case OAKLEY_ATTR_AUTH_METHOD_RSAENC:
	case OAKLEY_ATTR_AUTH_METHOD_RSAREV:
		if (iph1->pl_hash == NULL)
			ng++;
		break;
	default:
		plog(logp, LOCATION, iph1->remote,
			"invalid authmethod %d why ?\n",
			iph1->approval->authmethod);
		goto end;
	}
	if (ng) {
		plog(logp, LOCATION, iph1->remote,
			"few isakmp message received.\n");
		goto end;
	}
    }

	/* validate authentication value */
    {
	int type;
	type = oakley_validate_auth(iph1);
	if (type != 0) {
		if (type == -1) {
			/* message printed inner oakley_validate_auth() */
			goto end;
		}
		isakmp_info_send_n1(iph1, type, NULL);
		goto end;
	}
    }

	if (oakley_checkcr(iph1) < 0) {
		/* Ignore this error in order to be interoperability. */
		;
	}

	/*
	 * XXX: Should we do compare two addresses, ph1handle's and ID
	 * payload's.
	 */

	YIPSDEBUG(DEBUG_MISC,
		plog(logp, LOCATION, iph1->remote, "ID ");
		hexdump((caddr_t)iph1->id_p, iph1->id_p->l));

	iph1->status = PHASE1ST_MSG3RECEIVED;

	error = 0;

end:
	if (pbuf)
		vfree(pbuf);
	if (msg)
		vfree(msg);

	if (error) {
		VPTRINIT(iph1->id_p);
		VPTRINIT(iph1->cert_p);
		VPTRINIT(iph1->crl_p);
		VPTRINIT(iph1->sig_p);
		VPTRINIT(iph1->cr_p);
	}

	return error;
}

/*
 * send to initiator
 * 	psk: HDR*, IDr1, HASH_R
 * 	sig: HDR*, IDr1, [ CERT, ] SIG_R
 * 	rsa: HDR*, HASH_R
 * 	rev: HDR*, HASH_R
 */
int
ident_r3send(iph1, msg0)
	struct ph1handle *iph1;
	vchar_t *msg0;
{
	vchar_t *msg = NULL;
	int error = -1;

	YIPSDEBUG(DEBUG_STAMP, plog(logp, LOCATION, NULL, "begin.\n"));

	/* validity check */
	if (iph1->status != PHASE1ST_MSG3RECEIVED) {
		plog(logp, LOCATION, NULL,
			"status mismatched %d.\n", iph1->status);
		goto end;
	}

	/* make ID payload into isakmp status */
	if (ipsecdoi_setid1(iph1) < 0)
		goto end;

	/* generate HASH to send */
	YIPSDEBUG(DEBUG_KEY, plog(logp, LOCATION, NULL, "generate HASH_R\n"));
	iph1->hash = oakley_ph1hash_common(iph1, GENERATE);
	if (iph1->hash == NULL)
		goto end;

	/* set encryption flag */
	iph1->flags |= ISAKMP_FLAG_E;

	/* create HDR;ID;HASH payload */
	iph1->sendbuf = ident_ir3sendmx(iph1);
	if (iph1->sendbuf == NULL)
		goto end;

	iph1->status = PHASE1ST_ESTABLISHED;

	error = 0;

end:
	if (msg != NULL)
		vfree(msg);

	return error;
}

/*
 * This is used in main mode for:
 * initiator's 3rd exchange send to responder
 * 	psk: HDR, KE, Ni
 * 	sig: HDR, KE, Ni
 * 	rsa: HDR, KE, [ HASH(1), ] <IDi1_b>PubKey_r, <Ni_b>PubKey_r
 * 	rev: HDR, [ HASH(1), ] <Ni_b>Pubkey_r, <KE_b>Ke_i,
 * 	          <IDi1_b>Ke_i, [<<Cert-I_b>Ke_i]
 * responders 2nd exchnage send to initiator
 * 	psk: HDR, KE, Nr
 * 	sig: HDR, KE, Nr [, CR ]
 * 	rsa: HDR, KE, <IDr1_b>PubKey_i, <Nr_b>PubKey_i
 * 	rev: HDR, <Nr_b>PubKey_i, <KE_b>Ke_r, <IDr1_b>Ke_r,
 */
static vchar_t *
ident_ir2sendmx(iph1)
	struct ph1handle *iph1;
{
	vchar_t *buf = 0;
	struct isakmp_gen *gen;
	char *p;
	int tlen;
	int need_cr = 0;
	vchar_t *cr = NULL;
	int error = -1;

	/* create CR if need */
	if (iph1->side == INITIATOR
	 && oakley_needcr(iph1->approval->authmethod)
	 && iph1->rmconf->peerscertfile == NULL) {
		need_cr = 1;
		cr = oakley_getcr(iph1);
		if (cr == NULL) {
			plog(logp, LOCATION, NULL,
				"failed to get cr buffer.\n");
			goto end;
		}
	}

	/* create buffer */
	tlen = sizeof(struct isakmp)
	     + sizeof(*gen) + iph1->dhpub->l
	     + sizeof(*gen) + iph1->nonce->l;
	if (lcconf->vendorid)
		tlen += sizeof(*gen) + lcconf->vendorid->l;
	if (need_cr)
		tlen += sizeof(*gen) + cr->l;

	buf = vmalloc(tlen);
	if (buf == NULL) {
		plog(logp, LOCATION, NULL,
			"failed to get buffer to send.\n");
		goto end;
	}

	/* set isakmp header */
	p = set_isakmp_header(buf, iph1, ISAKMP_NPTYPE_KE);
	if (p == NULL)
		goto end;

	/* create isakmp KE payload */
	p = set_isakmp_payload(p, iph1->dhpub, ISAKMP_NPTYPE_NONCE);

	/* create isakmp NONCE payload */
	p = set_isakmp_payload(p, iph1->nonce,
			lcconf->vendorid ? ISAKMP_NPTYPE_VID
					 : (need_cr ? ISAKMP_NPTYPE_CR
						    : ISAKMP_NPTYPE_NONE));

	/* append vendor id, if needed */
	if (lcconf->vendorid)
		p = set_isakmp_payload(p, lcconf->vendorid,
				need_cr ? ISAKMP_NPTYPE_CR
					: ISAKMP_NPTYPE_NONE);

	/* create isakmp CR payload if needed */
	if (need_cr)
		p = set_isakmp_payload(p, cr, ISAKMP_NPTYPE_NONE);

#ifdef HAVE_PRINT_ISAKMP_C
	isakmp_printpacket(buf, iph1->local, iph1->remote, 0);
#endif

	/* send HDR;KE;NONCE to responder */
	if (isakmp_send(iph1, buf) < 0)
		goto end;

	error = 0;

end:
	if (error && buf != NULL) {
		vfree(buf);
		buf = NULL;
	}

	return buf;
}

/*
 * This is used in main mode for:
 * initiator's 4th exchange send to responder
 * 	psk: HDR*, IDi1, HASH_I
 * 	sig: HDR*, IDi1, [ CR, ] [ CERT, ] SIG_I
 * 	rsa: HDR*, HASH_I
 * 	rev: HDR*, HASH_I
 * responders 3rd exchnage send to initiator
 * 	psk: HDR*, IDr1, HASH_R
 * 	sig: HDR*, IDr1, [ CERT, ] SIG_R
 * 	rsa: HDR*, HASH_R
 * 	rev: HDR*, HASH_R
 */
static vchar_t *
ident_ir3sendmx(iph1)
	struct ph1handle *iph1;
{
	vchar_t *buf = NULL, *new = NULL;
	char *p;
	int tlen;
	struct isakmp_gen *gen;
	int need_cr = 0;
	vchar_t *cr = NULL;
	int error = -1;

	tlen = sizeof(struct isakmp);

	switch (iph1->approval->authmethod) {
	case OAKLEY_ATTR_AUTH_METHOD_PSKEY:
		tlen += sizeof(*gen) + iph1->id->l
			+ sizeof(*gen) + iph1->hash->l;

		buf = vmalloc(tlen);
		if (buf == NULL) {
			plog(logp, LOCATION, NULL,
				"failed to get buffer to send.\n");
			goto end;
		}

		/* set isakmp header */
		p = set_isakmp_header(buf, iph1, ISAKMP_NPTYPE_ID);
		if (p == NULL)
			goto end;

		/* create isakmp ID payload */
		p = set_isakmp_payload(p, iph1->id, ISAKMP_NPTYPE_HASH);

		/* create isakmp HASH payload */
		p = set_isakmp_payload(p, iph1->hash, ISAKMP_NPTYPE_NONE);
		break;
#ifdef HAVE_SIGNING_C
	case OAKLEY_ATTR_AUTH_METHOD_DSSSIG:
	case OAKLEY_ATTR_AUTH_METHOD_RSASIG:
		if (oakley_getmycert(iph1) < 0)
			goto end;

		if (oakley_getsign(iph1) < 0)
			goto end;

		/* create CR if need */
		if (iph1->side == INITIATOR
	 	 && oakley_needcr(iph1->approval->authmethod)
		 && iph1->rmconf->peerscertfile == NULL) {
			need_cr = 1;
			cr = oakley_getcr(iph1);
			if (cr == NULL) {
				plog(logp, LOCATION, NULL,
					"failed to get cr buffer.\n");
				goto end;
			}
		}

		tlen += sizeof(*gen) + iph1->id->l
			+ sizeof(*gen) + iph1->sig->l;
		if (iph1->cert != NULL)
			tlen += sizeof(*gen) + iph1->cert->l;
		if (need_cr)
			tlen += sizeof(*gen) + cr->l;

		buf = vmalloc(tlen);
		if (buf == NULL) {
			plog(logp, LOCATION, NULL,
				"failed to get buffer to send.\n");
			goto end;
		}

		/* set isakmp header */
		p = set_isakmp_header(buf, iph1, ISAKMP_NPTYPE_ID);
		if (p == NULL)
			goto end;

		/* add ID payload */
		p = set_isakmp_payload(p, iph1->id, iph1->cert != NULL
							? ISAKMP_NPTYPE_CERT
							: ISAKMP_NPTYPE_SIG);

		/* add CERT payload if there */
		if (iph1->cert != NULL)
			p = set_isakmp_payload(p, iph1->cert, ISAKMP_NPTYPE_SIG);
		/* add SIG payload */
		p = set_isakmp_payload(p, iph1->sig,
			need_cr ? ISAKMP_NPTYPE_CR : ISAKMP_NPTYPE_NONE);

		/* create isakmp CR payload */
		if (need_cr)
			p = set_isakmp_payload(p, cr, ISAKMP_NPTYPE_NONE);
		break;
#endif
	case OAKLEY_ATTR_AUTH_METHOD_RSAENC:
	case OAKLEY_ATTR_AUTH_METHOD_RSAREV:
		plog(logp, LOCATION, NULL,
			"not supported authentication type %d\n",
			iph1->approval->authmethod);
		goto end;
	default:
		plog(logp, LOCATION, NULL,
			"invalid authentication type %d\n",
			iph1->approval->authmethod);
		goto end;
	}

#ifdef HAVE_PRINT_ISAKMP_C
	isakmp_printpacket(buf, iph1->local, iph1->remote, 1);
#endif

	/* encoding */
	new = oakley_do_encrypt(iph1, buf, iph1->ivm->ive, iph1->ivm->iv);
	if (new == NULL)
		goto end;

	vfree(buf);

	buf = new;

	/* send HDR;ID;HASH to responder */
	if (isakmp_send(iph1, buf) < 0)
		goto end;

	/* synchronization IV */
	memcpy(iph1->ivm->ive->v, iph1->ivm->iv->v, iph1->ivm->iv->l);
	memcpy(iph1->ivm->ivd->v, iph1->ivm->iv->v, iph1->ivm->iv->l);

	error = 0;

end:
	if (cr)
		vfree(cr);
	if (error && buf != NULL) {
		vfree(buf);
		buf = NULL;
	}

	return buf;
}

