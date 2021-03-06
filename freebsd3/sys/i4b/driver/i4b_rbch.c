/*
 * Copyright (c) 1997, 1998 Hellmuth Michaelis. All rights reserved.
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
 *---------------------------------------------------------------------------
 *
 *	i4b_rbch.c - device driver for raw B channel data
 *	---------------------------------------------------
 *
 *	$Id: i4b_rbch.c,v 1.1 1998/12/27 21:46:42 phk Exp $
 *
 *	last edit-date: [Sun Dec 13 10:19:08 1998]
 *
 *---------------------------------------------------------------------------*/

#include "i4brbch.h"

#if NI4BRBCH > 0

#include <sys/param.h>
#include <sys/systm.h>

#if (defined(__FreeBSD_version) && __FreeBSD_version >= 300001) || !defined(__FreeBSD__)
#include <sys/ioccom.h>
#include <sys/poll.h>
#else
#include <sys/fcntl.h>
#include <sys/ioctl.h>
#endif

#if (defined(__FreeBSD_version) && __FreeBSD_version >= 300001)
#include <sys/filio.h>
#endif

#include <sys/conf.h>
#include <sys/uio.h>
#include <sys/kernel.h>
#include <sys/mbuf.h>
#include <sys/socket.h>
#include <net/if.h>
#include <sys/proc.h>
#include <sys/tty.h>

#ifdef __NetBSD__
extern cc_t ttydefchars;
#define termioschars(t) memcpy((t)->c_cc, &ttydefchars, sizeof((t)->c_cc))
#endif

#ifdef __FreeBSD__
#include "opt_devfs.h"
#endif

#ifdef DEVFS
#include <sys/devfsext.h>
#endif

#ifdef __NetBSD__
#include <sys/filio.h>
#define bootverbose 0
#endif

#ifdef __FreeBSD__
#include <machine/i4b_ioctl.h>
#include <machine/i4b_debug.h>
#else
#include <i4b/i4b_ioctl.h>
#include <i4b/i4b_debug.h>
#endif

#include <i4b/include/i4b_global.h>
#include <i4b/include/i4b_mbuf.h>
#include <i4b/include/i4b_l3l4.h>

#include <i4b/layer4/i4b_l4.h>
/* initialized by L4 */

static drvr_link_t rbch_drvr_linktab[NI4BRBCH];
static isdn_link_t *isdn_linktab[NI4BRBCH];

static struct rbch_softc {
	int sc_devstate;		/* state of driver	*/
#define ST_IDLE		0x00
#define ST_CONNECTED	0x01
#define ST_ISOPEN	0x02
#define ST_RDWAITDATA	0x04
#define ST_WRWAITEMPTY	0x08
#define ST_NOBLOCK	0x10

	int sc_bprot;			/* B-ch protocol used	*/

	call_desc_t *cd;	/* Call Descriptor */
	struct termios it_in;

	struct ifqueue sc_hdlcq;	/* hdlc read queue	*/
#define I4BRBCHMAXQLEN	10

	struct selinfo selp;		/* select / poll	*/

#ifdef DEVFS
	void *devfs_token;		/* device filesystem	*/
#endif	
} rbch_softc[NI4BRBCH];

static void rbch_rx_data_rdy(int unit);
static void rbch_tx_queue_empty(int unit);
static void rbch_connect(int unit, void *cdp);
static void rbch_disconnect(int unit, void *cdp);
static void rbch_init_linktab(int unit);
static void rbch_clrq(int unit);

#ifndef __FreeBSD__
#define PDEVSTATIC	/* - not static - */
#define IOCTL_CMD_T	u_long
void i4brbchattach __P((void));
int i4brbchopen __P((dev_t dev, int flag, int fmt, struct proc *p));
int i4brbchclose __P((dev_t dev, int flag, int fmt, struct proc *p));
int i4brbchread __P((dev_t dev, struct uio *uio, int ioflag));
int i4brbchwrite __P((dev_t dev, struct uio *uio, int ioflag));
int i4brbchioctl __P((dev_t dev, IOCTL_CMD_T cmd, caddr_t arg, int flag, struct proc* pr));
int i4brbchpoll __P((dev_t dev, int events, struct proc *p));
#endif

#if BSD > 199306 && defined(__FreeBSD__)
#define PDEVSTATIC	static
#if !defined(__FreeBSD_version) || __FreeBSD_version < 300003
#define IOCTL_CMD_T	int
#else
#define IOCTL_CMD_T	u_long
#endif

PDEVSTATIC d_open_t i4brbchopen;
PDEVSTATIC d_close_t i4brbchclose;
PDEVSTATIC d_read_t i4brbchread;
PDEVSTATIC d_read_t i4brbchwrite;
PDEVSTATIC d_ioctl_t i4brbchioctl;

#if (defined(__FreeBSD_version) && __FreeBSD_version >= 300001) || !defined(__FreeBSD__)
PDEVSTATIC d_poll_t i4brbchpoll;
#else
PDEVSTATIC d_select_t i4brbchselect;
#endif

#define CDEV_MAJOR 57
static struct cdevsw i4brbch_cdevsw = {
	i4brbchopen,	i4brbchclose,	i4brbchread,	i4brbchwrite,
  	i4brbchioctl,	nostop,		noreset,	nodevtotty,
#if defined(__FreeBSD_version) && __FreeBSD_version >= 300001
 	i4brbchpoll,	nommap, 	NULL, "i4brbch", NULL, -1
#else
	i4brbchselect,	nommap, 	NULL, "i4brbch", NULL, -1
#endif
};

static void i4brbchattach(void *);
PSEUDO_SET(i4brbchattach, i4b_rbch);

/*===========================================================================*
 *			DEVICE DRIVER ROUTINES
 *===========================================================================*/

/*---------------------------------------------------------------------------*
 *	initialization at kernel load time
 *---------------------------------------------------------------------------*/
static void
i4brbchinit(void *unused)
{
    dev_t dev;
    
    dev = makedev(CDEV_MAJOR, 0);

    cdevsw_add(&dev, &i4brbch_cdevsw, NULL);
}

SYSINIT(i4brbchdev, SI_SUB_DRIVERS,
	SI_ORDER_MIDDLE+CDEV_MAJOR, &i4brbchinit, NULL);

#endif /* BSD > 199306 && defined(__FreeBSD__) */

/*---------------------------------------------------------------------------*
 *	interface attach routine
 *---------------------------------------------------------------------------*/
PDEVSTATIC void
#ifdef __FreeBSD__
i4brbchattach(void *dummy)
#else
i4brbchattach()
#endif
{
	int i;

#ifndef HACK_NO_PSEUDO_ATTACH_MSG
	printf("i4brbch: %d raw B channel access device(s) attached\n", NI4BRBCH);
#endif
	
	for(i=0; i < NI4BRBCH; i++)
	{
#ifdef DEVFS
		rbch_softc[i].devfs_token =
			devfs_add_devswf(&i4brbch_cdevsw, i, DV_CHR,
				     UID_ROOT, GID_WHEEL, 0600,
				     "i4brbch%d", i);
#endif
		rbch_softc[i].sc_devstate = ST_IDLE;
		rbch_softc[i].sc_hdlcq.ifq_maxlen = I4BRBCHMAXQLEN;
		rbch_softc[i].it_in.c_ispeed = rbch_softc[i].it_in.c_ospeed = 64000;
		termioschars(&rbch_softc[i].it_in);
		rbch_init_linktab(i);
	}
}

/*---------------------------------------------------------------------------*
 *	open rbch device
 *---------------------------------------------------------------------------*/
int
i4brbchopen(dev_t dev, int flag, int fmt, struct proc *p)
{
	int unit = minor(dev);
	
	if(unit > NI4BRBCH)
		return(ENXIO);

	if(rbch_softc[unit].sc_devstate & ST_ISOPEN)
		return(EBUSY);

	rbch_clrq(unit);
	
	rbch_softc[unit].sc_devstate |= ST_ISOPEN;		

	DBGL4(L4_RBCHDBG, "i4brbchopen", ("unit %d, open\n", unit));	

	return(0);
}

/*---------------------------------------------------------------------------*
 *	close rbch device
 *---------------------------------------------------------------------------*/
int
i4brbchclose(dev_t dev, int flag, int fmt, struct proc *p)
{
	int unit = minor(dev);

	if (rbch_softc[unit].cd) {
		i4b_l4_disconnect_ind(rbch_softc[unit].cd);
		rbch_softc[unit].cd = NULL;
	}
	rbch_softc[unit].sc_devstate &= ~ST_ISOPEN;		

	rbch_clrq(unit);
	
	DBGL4(L4_RBCHDBG, "i4brbclose", ("unit %d, close\n", unit));
	
	return(0);
}

/*---------------------------------------------------------------------------*
 *	read from rbch device
 *---------------------------------------------------------------------------*/
int
i4brbchread(dev_t dev, struct uio *uio, int ioflag)
{
	struct mbuf *m;
	int s;
	int error = 0;
	int unit = minor(dev);
	struct ifqueue *iqp;
	
	DBGL4(L4_RBCHDBG, "i4brbchread", ("unit %d, enter read\n", unit));
	
	if(!(rbch_softc[unit].sc_devstate & ST_ISOPEN))
	{
		DBGL4(L4_RBCHDBG, "i4brbchread", ("unit %d, read while not open\n", unit));
		return(EIO);
	}

	if((rbch_softc[unit].sc_devstate & ST_NOBLOCK)) {
		if(!(rbch_softc[unit].sc_devstate & ST_CONNECTED))
			return(EWOULDBLOCK);

		if(rbch_softc[unit].sc_bprot == BPROT_RHDLC)
			iqp = &rbch_softc[unit].sc_hdlcq;
		else
			iqp = isdn_linktab[unit]->rx_queue;	

		if(IF_QEMPTY(iqp) && (rbch_softc[unit].sc_devstate & ST_ISOPEN))
			return(EWOULDBLOCK);
	} else {
		while(!(rbch_softc[unit].sc_devstate & ST_CONNECTED))
		{
			DBGL4(L4_RBCHDBG, "i4brbchread", ("unit %d, wait read init\n", unit));
		
			if((error = tsleep((caddr_t) &rbch_softc[unit],
					   TTIPRI | PCATCH,
					   "rrrbch", 0 )) != 0)
			{
				DBGL4(L4_RBCHDBG, "i4brbchread", ("unit %d, error %d tsleep\n", unit, error));
				return(error);
			}
		}

		if(rbch_softc[unit].sc_bprot == BPROT_RHDLC)
			iqp = &rbch_softc[unit].sc_hdlcq;
		else
			iqp = isdn_linktab[unit]->rx_queue;	

		while(IF_QEMPTY(iqp) && (rbch_softc[unit].sc_devstate & ST_ISOPEN))
		{
			s = splimp();
			rbch_softc[unit].sc_devstate |= ST_RDWAITDATA;
			splx(s);
		
			DBGL4(L4_RBCHDBG, "i4brbchread", ("unit %d, wait read data\n", unit));
		
			if((error = tsleep((caddr_t) &isdn_linktab[unit]->rx_queue,
					   TTIPRI | PCATCH,
					   "rrbch", 0 )) != 0)
			{
				DBGL4(L4_RBCHDBG, "i4brbchread", ("unit %d, error %d tsleep read\n", unit, error));
				rbch_softc[unit].sc_devstate &= ~ST_RDWAITDATA;
				return(error);
			}
		}
	}

	s = splimp();

	IF_DEQUEUE(iqp, m);

	DBGL4(L4_RBCHDBG, "i4brbchread", ("unit %d, read %d bytes\n", unit, m->m_len));
	
	if(m && m->m_len)
	{
		error = uiomove(m->m_data, m->m_len, uio);
	}
	else
	{
		DBGL4(L4_RBCHDBG, "i4brbchread", ("unit %d, error %d uiomove\n", unit, error));
		error = EIO;
	}
		
	if(m)
		i4b_Bfreembuf(m);

	splx(s);

	return(error);
}

/*---------------------------------------------------------------------------*
 *	write to rbch device
 *---------------------------------------------------------------------------*/
int
i4brbchwrite(dev_t dev, struct uio * uio, int ioflag)
{
	struct mbuf *m;
	int s;
	int error = 0;
	int unit = minor(dev);

	DBGL4(L4_RBCHDBG, "i4brbchwrite", ("unit %d, write\n", unit));	

	if(!(rbch_softc[unit].sc_devstate & ST_ISOPEN))
	{
		DBGL4(L4_RBCHDBG, "i4brbchwrite", ("unit %d, write while not open\n", unit));
		return(EIO);
	}

	if((rbch_softc[unit].sc_devstate & ST_NOBLOCK)) {
		if(!(rbch_softc[unit].sc_devstate & ST_CONNECTED))
			return(EWOULDBLOCK);
		if(IF_QFULL(isdn_linktab[unit]->tx_queue) && (rbch_softc[unit].sc_devstate & ST_ISOPEN))
			return(EWOULDBLOCK);
	} else {
		while(!(rbch_softc[unit].sc_devstate & ST_CONNECTED))
		{
			DBGL4(L4_RBCHDBG, "i4brbchwrite", ("unit %d, write wait init\n", unit));
		
			error = tsleep((caddr_t) &rbch_softc[unit],
						   TTIPRI | PCATCH,
						   "wrrbch", 0 );
			if(error == ERESTART)
				return (ERESTART);
			else if(error == EINTR) {
				printf("\n ========= i4brbchwrite, EINTR during wait init ======== \n");
				return(EINTR);
			} else if(error) {
				DBGL4(L4_RBCHDBG, "i4brbchwrite", ("unit %d, error %d tsleep init\n", unit, error));
				return(error);
			}
/*XXX*/			tsleep((caddr_t) &rbch_softc[unit], TTIPRI | PCATCH, "xrbch", (hz*1));
		}

		while(IF_QFULL(isdn_linktab[unit]->tx_queue) && (rbch_softc[unit].sc_devstate & ST_ISOPEN))
		{
			s = splimp();
			rbch_softc[unit].sc_devstate |= ST_WRWAITEMPTY;
			splx(s);

			DBGL4(L4_RBCHDBG, "i4brbchwrite", ("unit %d, write queue full\n", unit));
		
			if ((error = tsleep((caddr_t) &isdn_linktab[unit]->tx_queue,
					    TTIPRI | PCATCH,
					    "wrbch", 0)) != 0) {
				rbch_softc[unit].sc_devstate &= ~ST_WRWAITEMPTY;			
				if(error == ERESTART) {
					return(ERESTART);
				} else if(error == EINTR) {
					printf("\n ========= i4brbchwrite, EINTR during wait write ======== \n");
					return(error);
				} else if(error) {
					DBGL4(L4_RBCHDBG, "i4brbchwrite",
					      ("unit %d, error %d tsleep write\n", unit, error));
					return(error);
				}
			}
		}
	}

	s = splimp();

	if(!(rbch_softc[unit].sc_devstate & ST_ISOPEN))
	{
		DBGL4(L4_RBCHDBG, "i4brbchwrite", ("unit %d, not open anymore\n", unit));
		splx(s);
		return(EIO);
	}

	if((m = i4b_Bgetmbuf(BCH_MAX_DATALEN)) != NULL)
	{
		m->m_len = min(BCH_MAX_DATALEN, uio->uio_resid);

		DBGL4(L4_RBCHDBG, "i4brbchwrite", ("unit %d, write %d bytes\n", unit, m->m_len));
		
		error = uiomove(m->m_data, m->m_len, uio);

		IF_ENQUEUE(isdn_linktab[unit]->tx_queue, m);

		(*isdn_linktab[unit]->bch_tx_start)(isdn_linktab[unit]->unit, isdn_linktab[unit]->channel);
	}

	splx(s);
	
	return(error);
}

PDEVSTATIC int
i4brbchioctl(dev_t dev, IOCTL_CMD_T cmd, caddr_t data, int flag, struct proc* p) {
	int error = 0;
	int unit = minor(dev);

	switch(cmd)
	{
#if 0
		case I4B_RBCH_DIALOUT:
if(bootverbose)printf("EE-rbch%d: attempting dialout (ioctl)\n", unit);
			i4b_l4_dialout(BDRV_RBCH, unit);
			break;
#endif
			
		case FIOASYNC:	/* Set async mode */
			if (*(int *)data) {
if(bootverbose)printf("EE-rbch%d: setting async mode\n", unit);
			} else {
if(bootverbose)printf("EE-rbch%d: clearing async mode\n", unit);
			}
			break;
		case FIONBIO:
			if (*(int *)data) {
if(bootverbose)printf("EE-rbch%d: setting non-blocking mode\n", unit);
				rbch_softc[unit].sc_devstate |= ST_NOBLOCK;
			} else {
if(bootverbose)printf("EE-rbch%d: clearing non-blocking mode\n", unit);
				rbch_softc[unit].sc_devstate &= ~ST_NOBLOCK;
			}
			break;
		case TIOCCDTR:	/* Clear DTR */
			if(rbch_softc[unit].sc_devstate & ST_CONNECTED) {
if(bootverbose)printf("EE-rbch%d: disconnecting for DTR down\n", unit);
				i4b_l4_disconnect_ind(rbch_softc[unit].cd);
			}
			break;
		case TIOCSDTR:	/* Set DTR */
if(bootverbose)printf("EE-rbch%d: attempting dialout (DTR)\n", unit);
			i4b_l4_dialout(BDRV_RBCH, unit);
			break;
		case TIOCSETA:	/* Set termios struct */
			break;
		case TIOCGETA:	/* Get termios struct */
			*(struct termios *)data = rbch_softc[unit].it_in;
			break;
		case TIOCMGET:
			*(int *)data = TIOCM_LE|TIOCM_DTR|TIOCM_RTS|TIOCM_CTS|TIOCM_DSR;
			if (rbch_softc[unit].sc_devstate & ST_CONNECTED)
				*(int *)data |= TIOCM_CD;
			break;
		default:	/* Unknown stuff */
			printf("\n ========= i4brbch%d - ioctl, unknown cmd %lx ==================== \n",
			       unit,
			       (u_long)cmd);
			error = EINVAL;
			break;
	}
	return(error);
}

/*---------------------------------------------------------------------------*
 *	device driver poll
 *---------------------------------------------------------------------------*/
#if (defined(__FreeBSD_version) && __FreeBSD_version >= 300001) || !defined(__FreeBSD__)

PDEVSTATIC int
i4brbchpoll(dev_t dev, int events, struct proc *p)
{
	int revents = 0;	/* Events we found */
	int s;
	int unit = minor(dev);

	/* We can't check for anything but IN or OUT */

	if((events & (POLLIN|POLLOUT)) == 0)
		return(POLLNVAL);
	
	s = splhigh();

	if(!(rbch_softc[unit].sc_devstate & ST_ISOPEN))
	{
		splx(s);
		return(POLLNVAL);
	}

	/*
	 * Writes are OK if we are connected and the
         * transmit queue can take them
	 */
	 
	if((events & POLLOUT) &&
	   (rbch_softc[unit].sc_devstate & ST_CONNECTED) &&
	   !IF_QFULL(isdn_linktab[unit]->tx_queue))
	{
		revents |= POLLOUT;
	}
	
	/* ... while reads are OK if we have any data */

	if((events & POLLIN) &&
	   (rbch_softc[unit].sc_devstate & ST_CONNECTED))
	{
		struct ifqueue *iqp;

		if(rbch_softc[unit].sc_bprot == BPROT_RHDLC)
			iqp = &rbch_softc[unit].sc_hdlcq;
		else
			iqp = isdn_linktab[unit]->rx_queue;	

		if(!IF_QEMPTY(iqp))
			revents |= POLLIN;
	}
		
	if(revents == 0)
		selrecord(p, &rbch_softc[unit].selp);

	splx(s);
	return(revents);
}

#else

/*---------------------------------------------------------------------------*
 *	device driver select
 *---------------------------------------------------------------------------*/
static int
i4brbchselect(dev_t dev, int rw, struct proc *p)
{
	int unit = minor(dev);
        int s;

	s = splhigh();

	if(!(rbch_softc[unit].sc_devstate & ST_ISOPEN))
	{
		splx(s);
		DBGL4(L4_RBCHDBG, "i4brbchselect", ("unit %d, not open anymore\n", unit));
		return(1);
	}
	
	if(rbch_softc[unit].sc_devstate & ST_CONNECTED)
	{
		struct ifqueue *iqp;

		switch(rw)
		{
			case FREAD:
				if(rbch_softc[unit].sc_bprot == BPROT_RHDLC)
					iqp = &rbch_softc[unit].sc_hdlcq;
				else
					iqp = isdn_linktab[unit]->rx_queue;	

				if(!IF_QEMPTY(iqp))
					return(1);
				break;

			case FWRITE:
				if(!IF_QFULL(isdn_linktab[unit]->rx_queue))
					return(1);
				break;

			default:
				return 0;
		}
	}
	selrecord(p, &rbch_softc[unit].selp);
	return(0);
}

#endif /* defined(__FreeBSD_version) && __FreeBSD_version >= 300001 */

/*===========================================================================*
 *			ISDN INTERFACE ROUTINES
 *===========================================================================*/

/*---------------------------------------------------------------------------*
 *	this routine is called from L4 handler at connect time
 *---------------------------------------------------------------------------*/
static void
rbch_connect(int unit, void *cdp)
{
	call_desc_t *cd = (call_desc_t *)cdp;

	rbch_softc[unit].sc_bprot = cd->bprot;
		
	if(!(rbch_softc[unit].sc_devstate & ST_CONNECTED))
	{
		DBGL4(L4_RBCHDBG, "rbch_connect", ("unit %d, wakeup\n", unit));
		rbch_softc[unit].sc_devstate |= ST_CONNECTED;
		rbch_softc[unit].cd = cdp;
		wakeup((caddr_t) &rbch_softc[unit]);
	}
}

/*---------------------------------------------------------------------------*
 *	this routine is called from L4 handler at disconnect time
 *---------------------------------------------------------------------------*/
static void
rbch_disconnect(int unit, void *cdp)
{
	/* call_desc_t *cd = (call_desc_t *)cdp; */

	DBGL4(L4_RBCHDBG, "rbch_disconnect", ("unit %d, deinit\n", unit));
	rbch_softc[unit].sc_devstate &= ~ST_CONNECTED;
	rbch_softc[unit].cd = NULL;
}
	
/*---------------------------------------------------------------------------*
 *	feedback from daemon in case of dial problems
 *---------------------------------------------------------------------------*/
static void
rbch_dialresponse(int unit, int status)
{
}
	
/*---------------------------------------------------------------------------*
 *	interface up/down
 *---------------------------------------------------------------------------*/
static void
rbch_updown(int unit, int updown)
{
}
	
/*---------------------------------------------------------------------------*
 *	this routine is called from the HSCX interrupt handler
 *	when a new frame (mbuf) has been received and is to be put on
 *	the rx queue.
 *---------------------------------------------------------------------------*/
static void
rbch_rx_data_rdy(int unit)
{
	if(rbch_softc[unit].sc_bprot == BPROT_RHDLC)
	{
		register struct mbuf *m;
		
		if((m = *isdn_linktab[unit]->rx_mbuf) == NULL)
			return;

		m->m_pkthdr.len = m->m_len;

		if(IF_QFULL(&(rbch_softc[unit].sc_hdlcq)))
		{
			DBGL4(L4_RBCHDBG, "rbch_rx_data_rdy", ("unit %d: hdlc rx queue full!\n", unit));
			m_freem(m);
		}
		else
		{
			IF_ENQUEUE(&(rbch_softc[unit].sc_hdlcq), m);
		}
	}				

	if(rbch_softc[unit].sc_devstate & ST_RDWAITDATA)
	{
		DBGL4(L4_RBCHDBG, "rbch_rx_data_rdy", ("unit %d, wakeup\n", unit));
		rbch_softc[unit].sc_devstate &= ~ST_RDWAITDATA;
		wakeup((caddr_t) &isdn_linktab[unit]->rx_queue);
	}
	else
	{
		DBGL4(L4_RBCHDBG, "rbch_rx_data_rdy", ("unit %d, NO wakeup\n", unit));
	}
	selwakeup(&rbch_softc[unit].selp);
}

/*---------------------------------------------------------------------------*
 *	this routine is called from the HSCX interrupt handler
 *	when the last frame has been sent out and there is no
 *	further frame (mbuf) in the tx queue.
 *---------------------------------------------------------------------------*/
static void
rbch_tx_queue_empty(int unit)
{
	if(rbch_softc[unit].sc_devstate & ST_WRWAITEMPTY)
	{
		DBGL4(L4_RBCHDBG, "rbch_tx_queue_empty", ("unit %d, wakeup\n", unit));
		rbch_softc[unit].sc_devstate &= ~ST_WRWAITEMPTY;
		wakeup((caddr_t) &isdn_linktab[unit]->tx_queue);
	}
	else
	{
		DBGL4(L4_RBCHDBG, "rbch_tx_queue_empty", ("unit %d, NO wakeup\n", unit));
	}
	selwakeup(&rbch_softc[unit].selp);
}

/*---------------------------------------------------------------------------*
 *	this routine is called from the HSCX interrupt handler
 *	each time a packet is received or transmitted
 *---------------------------------------------------------------------------*/
static void
rbch_activity(int unit, int rxtx)
{
	if (rbch_softc[unit].cd)
		rbch_softc[unit].cd->last_active_time = SECOND;
	selwakeup(&rbch_softc[unit].selp);
}

/*---------------------------------------------------------------------------*
 *	clear an hdlc rx queue for a rbch unit
 *---------------------------------------------------------------------------*/
static void
rbch_clrq(int unit)
{
	int x;
	struct mbuf *m;

	for(;;)
	{
		x = splimp();
		IF_DEQUEUE(&rbch_softc[unit].sc_hdlcq, m);
		splx(x);
		if(m)
			m_freem(m);
		else
			break;
	}
}
				
/*---------------------------------------------------------------------------*
 *	return this drivers linktab address
 *---------------------------------------------------------------------------*/
drvr_link_t *
rbch_ret_linktab(int unit)
{
	rbch_init_linktab(unit);
	return(&rbch_drvr_linktab[unit]);
}

/*---------------------------------------------------------------------------*
 *	setup the isdn_linktab for this driver
 *---------------------------------------------------------------------------*/
void
rbch_set_linktab(int unit, isdn_link_t *ilt)
{
	isdn_linktab[unit] = ilt;
}

/*---------------------------------------------------------------------------*
 *	initialize this drivers linktab
 *---------------------------------------------------------------------------*/
static void
rbch_init_linktab(int unit)
{
	rbch_drvr_linktab[unit].unit = unit;
	rbch_drvr_linktab[unit].bch_rx_data_ready = rbch_rx_data_rdy;
	rbch_drvr_linktab[unit].bch_tx_queue_empty = rbch_tx_queue_empty;
	rbch_drvr_linktab[unit].bch_activity = rbch_activity;	
	rbch_drvr_linktab[unit].line_connected = rbch_connect;
	rbch_drvr_linktab[unit].line_disconnected = rbch_disconnect;
	rbch_drvr_linktab[unit].dial_response = rbch_dialresponse;
	rbch_drvr_linktab[unit].updown_ind = rbch_updown;	
}

/*===========================================================================*/

#endif /* NI4BRBCH > 0 */
