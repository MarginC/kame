/*-
 * Copyright (c) 1998 Nicolas Souchu
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
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	$Id: immio.c,v 1.5 1999/01/10 12:04:54 nsouch Exp $
 *
 */

/*
 * Iomega ZIP+ Matchmaker Parallel Port Interface driver
 *
 * Thanks to David Campbell work on the Linux driver and the Iomega specs
 * Thanks to Thiebault Moeglin for the drive
 */
#ifdef KERNEL
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/buf.h>

#include <machine/clock.h>

#endif	/* KERNEL */

#ifdef	KERNEL
#include <sys/kernel.h>
#endif /*KERNEL */

#include "opt_vpo.h"

#include <dev/ppbus/ppbconf.h>
#include <dev/ppbus/ppb_msq.h>
#include <dev/ppbus/vpoio.h>
#include <dev/ppbus/ppb_1284.h>

#define VP0_SELTMO		5000	/* select timeout */
#define VP0_FAST_SPINTMO	500000	/* wait status timeout */
#define VP0_LOW_SPINTMO		5000000	/* wait status timeout */

#define VP0_SECTOR_SIZE	512

/*
 * Microcode to execute very fast I/O sequences at the lowest bus level.
 */

#define SELECT_TARGET		MS_PARAM(6, 1, MS_TYP_CHA)

#define DECLARE_SELECT_MICROSEQUENCE					\
struct ppb_microseq select_microseq[] = {				\
	MS_CASS(0xc),							\
	/* first, check there is nothing holding onto the bus */	\
	MS_SET(VP0_SELTMO),						\
/* _loop: */								\
	MS_BRCLEAR(0x8, 3 /* _ready */),				\
	MS_DBRA(-1 /* _loop */),					\
	MS_RET(2),			/* bus busy */			\
/* _ready: */								\
	MS_CASS(0x4),							\
	MS_DASS(MS_UNKNOWN /* 0x80 | 1 << target */),			\
	MS_DELAY(1),							\
	MS_CASS(0xc),							\
	MS_CASS(0xd),							\
	/* now, wait until the drive is ready */			\
	MS_SET(VP0_SELTMO),						\
/* loop: */								\
	MS_BRSET(0x8, 4 /* ready */),					\
	MS_DBRA(-1 /* loop */),						\
/* error: */								\
	MS_CASS(0xc),							\
	MS_RET(VP0_ESELECT_TIMEOUT),					\
/* ready: */								\
	MS_CASS(0xc),							\
	MS_RET(0)							\
}

static struct ppb_microseq transfer_epilog[] = {
	MS_CASS(0x4),
	MS_CASS(0xc),
	MS_CASS(0xe),
	MS_CASS(0x4),
	MS_RET(0)
};

#define CPP_S1		MS_PARAM(10, 2, MS_TYP_PTR)
#define CPP_S2		MS_PARAM(13, 2, MS_TYP_PTR)
#define CPP_S3		MS_PARAM(16, 2, MS_TYP_PTR)
#define CPP_PARAM	MS_PARAM(17, 1, MS_TYP_CHA)

#define DECLARE_CPP_MICROSEQ \
struct ppb_microseq cpp_microseq[] = {					\
	MS_CASS(0x0c), MS_DELAY(2),					\
	MS_DASS(0xaa), MS_DELAY(10),					\
	MS_DASS(0x55), MS_DELAY(10),					\
	MS_DASS(0x00), MS_DELAY(10),					\
	MS_DASS(0xff), MS_DELAY(10),					\
	MS_RFETCH(MS_REG_STR, 0xb8, MS_UNKNOWN /* &s1 */),		\
	MS_DASS(0x87), MS_DELAY(10),					\
	MS_RFETCH(MS_REG_STR, 0xb8, MS_UNKNOWN /* &s2 */),		\
	MS_DASS(0x78), MS_DELAY(10),					\
	MS_RFETCH(MS_REG_STR, 0x38, MS_UNKNOWN /* &s3 */),		\
	MS_DASS(MS_UNKNOWN /* param */),				\
	MS_DELAY(2),							\
	MS_CASS(0x0c), MS_DELAY(10),					\
	MS_CASS(0x0d), MS_DELAY(2),					\
	MS_CASS(0x0c), MS_DELAY(10),					\
	MS_DASS(0xff), MS_DELAY(10),					\
	MS_RET(0)							\
}

#define NEGOCIATED_MODE		MS_PARAM(2, 1, MS_TYP_CHA)

#define DECLARE_NEGOCIATE_MICROSEQ \
static struct ppb_microseq negociate_microseq[] = { 			\
	MS_CASS(0x4),							\
	MS_DELAY(5),							\
	MS_DASS(MS_UNKNOWN /* mode */),					\
	MS_DELAY(100),							\
	MS_CASS(0x6),							\
	MS_DELAY(5),							\
	MS_BRSET(0x20, 6 /* continue */),				\
	MS_DELAY(5),							\
	MS_CASS(0x7),							\
	MS_DELAY(5),							\
	MS_CASS(0x6),							\
	MS_RET(VP0_ENEGOCIATE),						\
/* continue: */								\
	MS_DELAY(5),							\
	MS_CASS(0x7),							\
	MS_DELAY(5),							\
	MS_CASS(0x6),							\
	MS_RET(0)							\
}

static struct ppb_microseq reset_microseq[] = {
	MS_CASS(0x04),
	MS_DASS(0x40),
	MS_DELAY(1),
	MS_CASS(0x0c),
	MS_CASS(0x0d),
	MS_DELAY(50),
	MS_CASS(0x0c),
	MS_CASS(0x04),
	MS_RET(0)
};

/*
 * nibble_inbyte_hook()
 *
 * Formats high and low nibble into a character
 */
static int
nibble_inbyte_hook (void *p, char *ptr)
{
	struct vpo_nibble *s = (struct vpo_nibble *)p;

	/* increment the buffer pointer */
	*ptr = ((s->l >> 4) & 0x0f) + (s->h & 0xf0);

	return (0);
}

/*
 * Macro used to initialize each vpoio_data structure during
 * low level attachment
 *
 * XXX should be converted to ppb_MS_init_msq()
 */
#define INIT_NIBBLE_INBYTE_SUBMICROSEQ(vpo) {		    	\
	(vpo)->vpo_nibble_inbyte_msq[6].arg[2].p =		\
			(void *)&(vpo)->vpo_nibble.h;		\
	(vpo)->vpo_nibble_inbyte_msq[3].arg[2].p =		\
			(void *)&(vpo)->vpo_nibble.l;		\
	(vpo)->vpo_nibble_inbyte_msq[9].arg[0].f =		\
			nibble_inbyte_hook;			\
	(vpo)->vpo_nibble_inbyte_msq[9].arg[1].p =		\
			(void *)&(vpo)->vpo_nibble;		\
}

/*
 * This is the sub-microseqence for MS_GET in NIBBLE mode
 * Retrieve the two nibbles and call the C function to generate the character
 * and store it in the buffer (see nibble_inbyte_hook())
 */
static struct ppb_microseq nibble_inbyte_submicroseq[] = {
	  MS_CASS(0x4),

/* loop: */
	  MS_CASS(0x6),
	  MS_DELAY(1),
	  MS_RFETCH(MS_REG_STR, MS_FETCH_ALL, MS_UNKNOWN /* low nibble */),
	  MS_CASS(0x5),
	  MS_DELAY(1),
	  MS_RFETCH(MS_REG_STR, MS_FETCH_ALL, MS_UNKNOWN /* high nibble */),
	  MS_CASS(0x4),
	  MS_DELAY(1),

	  /* do a C call to format the received nibbles */
	  MS_C_CALL(MS_UNKNOWN /* C hook */, MS_UNKNOWN /* param */),
	  MS_DBRA(-6 /* loop */),
	  MS_RET(0)
};

/*
 * This is the sub-microseqence for MS_GET in PS2 mode
 */
static struct ppb_microseq ps2_inbyte_submicroseq[] = {
	  MS_CASS(0x4),

/* loop: */
	  MS_CASS(PCD | 0x6),
	  MS_RFETCH_P(1, MS_REG_DTR, MS_FETCH_ALL),
	  MS_CASS(PCD | 0x5),
	  MS_DBRA(-3 /* loop */),

	  MS_RET(0)
};

/*
 * This is the sub-microsequence for MS_PUT in both NIBBLE and PS2 modes
 */
static struct ppb_microseq spp_outbyte_submicroseq[] = {
	  MS_CASS(0x4),

/* loop: */
	  MS_RASSERT_P(1, MS_REG_DTR), 
	  MS_CASS(0x5),
	  MS_DBRA(1),				/* decrement counter */
	  MS_RASSERT_P(1, MS_REG_DTR), 
	  MS_CASS(0x0),
	  MS_DBRA(-5 /* loop */),

	  /* return from the put call */
	  MS_CASS(0x4),
	  MS_RET(0)
};

/* EPP 1.7 microsequences, ptr and len set at runtime */
static struct ppb_microseq epp17_outstr[] = {
	  MS_CASS(0x4),
	  MS_RASSERT_P(MS_ACCUM, MS_REG_EPP), 
	  MS_CASS(0xc),
	  MS_RET(0),
};

static struct ppb_microseq epp17_instr[] = {
	  MS_CASS(PCD | 0x4),
	  MS_RFETCH_P(MS_ACCUM, MS_REG_EPP, MS_FETCH_ALL), 
	  MS_CASS(PCD | 0xc),
	  MS_RET(0),
};

static int
imm_disconnect(struct vpoio_data *vpo, int *connected, int release_bus)
{
	DECLARE_CPP_MICROSEQ;

	char s1, s2, s3;
	int ret;

	/* all should be ok */
	if (connected)
		*connected = 0;

	ppb_MS_init_msq(cpp_microseq, 4, CPP_S1, (void *)&s1,
			CPP_S2, (void *)&s2, CPP_S3, (void *)&s3,
			CPP_PARAM, 0x30);

	ppb_MS_microseq(&vpo->vpo_dev, cpp_microseq, &ret);

	if ((s1 != (char)0xb8 || s2 != (char)0x18 || s3 != (char)0x38)) {
		if (bootverbose)
			printf("imm%d: (disconnect) s1=0x%x s2=0x%x, s3=0x%x\n",
				vpo->vpo_unit, s1 & 0xff, s2 & 0xff, s3 & 0xff);
		if (connected)
			*connected = VP0_ECONNECT;
	}

	if (release_bus)
		return (ppb_release_bus(&vpo->vpo_dev));
	else
		return (0);
}

/*
 * how	: PPB_WAIT or PPB_DONTWAIT
 */
static int
imm_connect(struct vpoio_data *vpo, int how, int *disconnected, int request_bus)
{
	DECLARE_CPP_MICROSEQ;

	char s1, s2, s3;
	int error;
	int ret;

	/* all should be ok */
	if (disconnected)
		*disconnected = 0;

	if (request_bus)
		if ((error = ppb_request_bus(&vpo->vpo_dev, how)))
			return (error);

	ppb_MS_init_msq(cpp_microseq, 3, CPP_S1, (void *)&s1,
			CPP_S2, (void *)&s2, CPP_S3, (void *)&s3);

	/* select device 0 in compatible mode */
	ppb_MS_init_msq(cpp_microseq, 1, CPP_PARAM, 0xe0);
	ppb_MS_microseq(&vpo->vpo_dev, cpp_microseq, &ret);

	/* disconnect all devices */
	ppb_MS_init_msq(cpp_microseq, 1, CPP_PARAM, 0x30);
	ppb_MS_microseq(&vpo->vpo_dev, cpp_microseq, &ret);

	if (PPB_IN_EPP_MODE(&vpo->vpo_dev))
		ppb_MS_init_msq(cpp_microseq, 1, CPP_PARAM, 0x28);
	else
		ppb_MS_init_msq(cpp_microseq, 1, CPP_PARAM, 0xe0);

	ppb_MS_microseq(&vpo->vpo_dev, cpp_microseq, &ret);

	if ((s1 != (char)0xb8 || s2 != (char)0x18 || s3 != (char)0x30)) {
		if (bootverbose)
			printf("imm%d: (connect) s1=0x%x s2=0x%x, s3=0x%x\n",
				vpo->vpo_unit, s1 & 0xff, s2 & 0xff, s3 & 0xff);
		if (disconnected)
			*disconnected = VP0_ECONNECT;
	}

	return (0);
}

/*
 * imm_detect()
 *
 * Detect and initialise the VP0 adapter.
 */
static int
imm_detect(struct vpoio_data *vpo)
{
	int error;

	if ((error = ppb_request_bus(&vpo->vpo_dev, PPB_DONTWAIT)))
		return (error);

	/* disconnect the drive, keep the bus */
	imm_disconnect(vpo, NULL, 0);

	/* we already have the bus, just connect */
	imm_connect(vpo, PPB_DONTWAIT, &error, 0);

	if (error) {
		if (bootverbose)
			printf("imm%d: can't connect to the drive\n",
				vpo->vpo_unit);
		goto error;
	}

	/* send SCSI reset signal */
	ppb_MS_microseq(&vpo->vpo_dev, reset_microseq, NULL);

	/* release the bus now */
	imm_disconnect(vpo, &error, 1);

	/* ensure we are disconnected or daisy chained peripheral 
	 * may cause serious problem to the disk */

	if (error) {
		if (bootverbose)
			printf("imm%d: can't disconnect from the drive\n",
				vpo->vpo_unit);
		goto error;
	}

	return (0);

error:
	ppb_release_bus(&vpo->vpo_dev);
	return (VP0_EINITFAILED);
}

/*
 * imm_outstr()
 */
static int
imm_outstr(struct vpoio_data *vpo, char *buffer, int size)
{
	int error = 0;

	if (PPB_IN_EPP_MODE(&vpo->vpo_dev))
		ppb_reset_epp_timeout(&vpo->vpo_dev);

	ppb_MS_exec(&vpo->vpo_dev, MS_OP_PUT, buffer, size, MS_UNKNOWN, &error);

	return (error);
}

/*
 * imm_instr()
 */
static int
imm_instr(struct vpoio_data *vpo, char *buffer, int size)
{
	int error = 0;

	if (PPB_IN_EPP_MODE(&vpo->vpo_dev))
		ppb_reset_epp_timeout(&vpo->vpo_dev);

	ppb_MS_exec(&vpo->vpo_dev, MS_OP_GET, buffer, size, MS_UNKNOWN, &error);

	return (error);
}

static char
imm_select(struct vpoio_data *vpo, int initiator, int target)
{
	DECLARE_SELECT_MICROSEQUENCE;
	int ret;

	/* initialize the select microsequence */
	ppb_MS_init_msq(select_microseq, 1,
			SELECT_TARGET, 1 << initiator | 1 << target);
				
	ppb_MS_microseq(&vpo->vpo_dev, select_microseq, &ret);

	return (ret);
}

/*
 * imm_wait()
 *
 * H_SELIN must be low.
 *
 * XXX should be ported to microseq
 */
static char
imm_wait(struct vpoio_data *vpo, int tmo)
{

	register int	k;
	register char	r;

	ppb_wctr(&vpo->vpo_dev, 0xc);

	/* XXX should be ported to microseq */
	k = 0;
	while (!((r = ppb_rstr(&vpo->vpo_dev)) & 0x80) && (k++ < tmo))
		DELAY(1);

	/*
	 * Return some status information.
	 * Semantics :	0x88 = ZIP+ wants more data
	 *		0x98 = ZIP+ wants to send more data
	 *		0xa8 = ZIP+ wants command
	 *		0xb8 = end of transfer, ZIP+ is sending status
	 */
	ppb_wctr(&vpo->vpo_dev, 0x4);
	if (k < tmo)
	  return (r & 0xb8);

	return (0);			   /* command timed out */	
}

static int
imm_negociate(struct vpoio_data *vpo)
{
	DECLARE_NEGOCIATE_MICROSEQ;
	int negociate_mode;
	int ret;

	if (PPB_IN_NIBBLE_MODE(&vpo->vpo_dev))
		negociate_mode = 0;
	else if (PPB_IN_PS2_MODE(&vpo->vpo_dev))
		negociate_mode = 1;
	else
		return (0);

#if 0 /* XXX use standalone code not to depend on ppb_1284 code yet */
	ret = ppb_1284_negociate(&vpo->vpo_dev, negociate_mode);

	if (ret)
		return (VP0_ENEGOCIATE);
#endif
	
	ppb_MS_init_msq(negociate_microseq, 1, NEGOCIATED_MODE, negociate_mode);

	ppb_MS_microseq(&vpo->vpo_dev, negociate_microseq, &ret);

	return (ret);
}

/*
 * imm_probe()
 *
 * Low level probe of vpo device
 *
 */
struct ppb_device *
imm_probe(struct ppb_data *ppb, struct vpoio_data *vpo)
{

	/* ppbus dependent initialisation */
	vpo->vpo_dev.id_unit = vpo->vpo_unit;
	vpo->vpo_dev.name = "vpo";
	vpo->vpo_dev.ppb = ppb;

	/* now, try to initialise the drive */
	if (imm_detect(vpo)) {
		return (NULL);
	}

	return (&vpo->vpo_dev);
}

/*
 * imm_attach()
 *
 * Low level attachment of vpo device
 *
 */
int
imm_attach(struct vpoio_data *vpo)
{
	int epp;

	/*
	 * Report ourselves
	 */
	printf("imm%d: <Iomega Matchmaker Parallel to SCSI interface> on ppbus %d\n",
		vpo->vpo_dev.id_unit, vpo->vpo_dev.ppb->ppb_link->adapter_unit);

	/*
	 * Initialize microsequence code
	 */
	vpo->vpo_nibble_inbyte_msq = (struct ppb_microseq *)malloc(
		sizeof(nibble_inbyte_submicroseq), M_DEVBUF, M_NOWAIT);

	if (!vpo->vpo_nibble_inbyte_msq)
		return (0);

	bcopy((void *)nibble_inbyte_submicroseq,
		(void *)vpo->vpo_nibble_inbyte_msq,
		sizeof(nibble_inbyte_submicroseq));

	INIT_NIBBLE_INBYTE_SUBMICROSEQ(vpo);

	/*
	 * Initialize mode dependent in/out microsequences
	 */
	ppb_request_bus(&vpo->vpo_dev, PPB_WAIT);

	/* enter NIBBLE mode to configure submsq */
	if (ppb_set_mode(&vpo->vpo_dev, PPB_NIBBLE) != -1) {

		ppb_MS_GET_init(&vpo->vpo_dev, vpo->vpo_nibble_inbyte_msq);
		ppb_MS_PUT_init(&vpo->vpo_dev, spp_outbyte_submicroseq);
	}

	/* enter PS2 mode to configure submsq */
	if (ppb_set_mode(&vpo->vpo_dev, PPB_PS2) != -1) {

		ppb_MS_GET_init(&vpo->vpo_dev, ps2_inbyte_submicroseq);
		ppb_MS_PUT_init(&vpo->vpo_dev, spp_outbyte_submicroseq);
	}

	epp = ppb_get_epp_protocol(&vpo->vpo_dev);

	/* enter EPP mode to configure submsq */
	if (ppb_set_mode(&vpo->vpo_dev, PPB_EPP) != -1) {

		switch (epp) {
		case EPP_1_9:
		case EPP_1_7:
			ppb_MS_GET_init(&vpo->vpo_dev, epp17_instr);
			ppb_MS_PUT_init(&vpo->vpo_dev, epp17_outstr);
			break;
		default:
			panic("%s: unknown EPP protocol (0x%x)", __FUNCTION__,
				epp);
		}
	}

	/* try to enter EPP or PS/2 mode, NIBBLE otherwise */
	if (ppb_set_mode(&vpo->vpo_dev, PPB_EPP) != -1) {
		switch (epp) {
		case EPP_1_9:
			printf("imm%d: EPP 1.9 mode\n", vpo->vpo_unit);
			break;
		case EPP_1_7:
			printf("imm%d: EPP 1.7 mode\n", vpo->vpo_unit);
			break;
		default:
			panic("%s: unknown EPP protocol (0x%x)", __FUNCTION__,
				epp);
		}
	} else if (ppb_set_mode(&vpo->vpo_dev, PPB_PS2) != -1)
		printf("imm%d: PS2 mode\n", vpo->vpo_unit);

	else if (ppb_set_mode(&vpo->vpo_dev, PPB_NIBBLE) != -1)
		printf("imm%d: NIBBLE mode\n", vpo->vpo_unit);

	else {
		printf("imm%d: can't enter NIBBLE, PS2 or EPP mode\n",
			vpo->vpo_unit);

		ppb_release_bus(&vpo->vpo_dev);

		free(vpo->vpo_nibble_inbyte_msq, M_DEVBUF);
		return (0);
	}

	ppb_release_bus(&vpo->vpo_dev);

	return (1);
}

/*
 * imm_reset_bus()
 *
 */
int
imm_reset_bus(struct vpoio_data *vpo)
{
	int disconnected;

	/* first, connect to the drive and request the bus */
	imm_connect(vpo, PPB_WAIT|PPB_INTR, &disconnected, 1);

	if (!disconnected) {

		/* reset the SCSI bus */
		ppb_MS_microseq(&vpo->vpo_dev, reset_microseq, NULL);

		/* then disconnect */
		imm_disconnect(vpo, NULL, 1);
	}

	return (0);
}

/*
 * imm_do_scsi()
 *
 * Send an SCSI command
 *
 */
int 
imm_do_scsi(struct vpoio_data *vpo, int host, int target, char *command,
		int clen, char *buffer, int blen, int *result, int *count,
		int *ret)
{

	register char r;
	char l, h = 0;
	int len, error = 0, not_connected = 0;
	register int k;
	int negociated = 0;

	/*
	 * enter disk state, allocate the ppbus
	 *
	 * XXX
	 * Should we allow this call to be interruptible?
	 * The only way to report the interruption is to return
	 * EIO do upper SCSI code :^(
	 */
	if ((error = imm_connect(vpo, PPB_WAIT|PPB_INTR, &not_connected, 1)))
		return (error);

	if (not_connected) {
		*ret = VP0_ECONNECT; goto error;
	}

	/*
	 * Select the drive ...
	 */
	if ((*ret = imm_select(vpo,host,target)))
		goto error;

	/*
	 * Send the command ...
	 */
	for (k = 0; k < clen; k+=2) {
		if (imm_wait(vpo, VP0_FAST_SPINTMO) != (char)0xa8) {
			*ret = VP0_ECMD_TIMEOUT;
			goto error;
		}
		if (imm_outstr(vpo, &command[k], 2)) {
			*ret = VP0_EPPDATA_TIMEOUT;
			goto error;
		}
	}

	if (!(r = imm_wait(vpo, VP0_LOW_SPINTMO))) {
		*ret = VP0_ESTATUS_TIMEOUT; goto error;
	}

	if ((r & 0x30) == 0x10) {
		if (imm_negociate(vpo)) {
			*ret = VP0_ENEGOCIATE;
			goto error;
		} else
			negociated = 1;
	}

	/* 
	 * Complete transfer ... 
	 */
	*count = 0;
	for (;;) {

		if (!(r = imm_wait(vpo, VP0_LOW_SPINTMO))) {
			*ret = VP0_ESTATUS_TIMEOUT; goto error;
		}

		/* stop when the ZIP+ wants to send status */
		if (r == (char)0xb8)
			break;

		if (*count >= blen) {
			*ret = VP0_EDATA_OVERFLOW;
			goto error;
		}

		/* ZIP+ wants to send data? */
		if (r == (char)0x88) {
			len = (((blen - *count) >= VP0_SECTOR_SIZE)) ?
				VP0_SECTOR_SIZE : 2;

			error = imm_outstr(vpo, &buffer[*count], len);
		} else {
			if (!PPB_IN_EPP_MODE(&vpo->vpo_dev))
				len = 1;
			else
				len = (((blen - *count) >= VP0_SECTOR_SIZE)) ?
					VP0_SECTOR_SIZE : 1;

			error = imm_instr(vpo, &buffer[*count], len);
		}

		if (error) {
			*ret = error;
			goto error;
		}

		*count += len;
	}

	if ((PPB_IN_NIBBLE_MODE(&vpo->vpo_dev) ||
			PPB_IN_PS2_MODE(&vpo->vpo_dev)) && negociated)
		ppb_MS_microseq(&vpo->vpo_dev, transfer_epilog, NULL);

	/*
	 * Retrieve status ...
	 */
	if (imm_negociate(vpo)) {
		*ret = VP0_ENEGOCIATE;
		goto error;
	} else
		negociated = 1;

	if (imm_instr(vpo, &l, 1)) {
		*ret = VP0_EOTHER; goto error;
	}

	/* check if the ZIP+ wants to send more status */
	if (imm_wait(vpo, VP0_FAST_SPINTMO) == (char)0xb8)
		if (imm_instr(vpo, &h, 1)) {
			*ret = VP0_EOTHER+2; goto error;
		}

	*result = ((int) h << 8) | ((int) l & 0xff);

error:
	if ((PPB_IN_NIBBLE_MODE(&vpo->vpo_dev) ||
			PPB_IN_PS2_MODE(&vpo->vpo_dev)) && negociated)
		ppb_MS_microseq(&vpo->vpo_dev, transfer_epilog, NULL);

	/* return to printer state, release the ppbus */
	imm_disconnect(vpo, NULL, 1);

	return (0);
}
