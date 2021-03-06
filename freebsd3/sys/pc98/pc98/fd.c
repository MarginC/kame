/*
 * Copyright (c) 1990 The Regents of the University of California.
 * All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Don Ahn.
 *
 * Libretto PCMCIA floppy support by David Horwitt (dhorwitt@ucsd.edu)
 * aided by the Linux floppy driver modifications from David Bateman
 * (dbateman@eng.uts.edu.au).
 *
 * Copyright (c) 1993, 1994 by
 *  jc@irbs.UUCP (John Capo)
 *  vak@zebub.msk.su (Serge Vakulenko)
 *  ache@astral.msk.su (Andrew A. Chernov)
 *
 * Copyright (c) 1993, 1994, 1995 by
 *  joerg_wunsch@uriah.sax.de (Joerg Wunsch)
 *  dufault@hda.com (Peter Dufault)
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
 *	from:	@(#)fd.c	7.4 (Berkeley) 5/25/91
 *	$Id: fd.c,v 1.50.2.2 1999/05/12 00:08:32 kato Exp $
 *
 */

#include "fd.h"
#include "opt_devfs.h"
#include "opt_fdc.h"

#if NFDC > 0

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/conf.h>
#include <sys/fcntl.h>
#include <machine/clock.h>
#include <machine/ioctl_fd.h>
#include <sys/disklabel.h>
#include <sys/buf.h>
#include <sys/devicestat.h>
#include <sys/malloc.h>
#include <sys/proc.h>
#include <sys/syslog.h>
#ifdef PC98
#include <pc98/pc98/pc98.h>
#include <pc98/pc98/pc98_machdep.h>
#include <pc98/pc98/epsonio.h>
#include <i386/isa/isa_device.h>
#include <pc98/pc98/fdreg.h>
#else
#include <i386/isa/isa.h>
#include <i386/isa/isa_device.h>
#include <i386/isa/fdreg.h>
#include <i386/isa/rtc.h>
#endif
#include <i386/isa/fdc.h>
#include <machine/stdarg.h>
#ifdef	DEVFS
#include <sys/devfsext.h>
#endif	/* DEVFS */

/* misuse a flag to identify format operation */
#define B_FORMAT B_XXX

/* configuration flags */
#define FDC_PRETEND_D0	(1 << 0)	/* pretend drive 0 to be there */
#ifdef FDC_YE
#define FDC_IS_PCMCIA  (1 << 1)		/* if successful probe, then it's
					   a PCMCIA device */
#endif

/* internally used only, not really from CMOS: */
#define RTCFDT_144M_PRETENDED	0x1000

/*
 * this biotab field doubles as a field for the physical unit number
 * on the controller
 */
#define id_physid id_scsiid

/* error returns for fd_cmd() */
#define FD_FAILED -1
#define FD_NOT_VALID -2
#define FDC_ERRMAX	100	/* do not log more */

#ifdef PC98
#define NUMTYPES 5
#define NUMDENS  NUMTYPES
#else
#define NUMTYPES 14
#define NUMDENS  (NUMTYPES - 6)
#endif

/* These defines (-1) must match index for fd_types */
#define F_TAPE_TYPE	0x020	/* bit for fd_types to indicate tape */
#define NO_TYPE		0	/* must match NO_TYPE in ft.c */
#ifdef PC98
#define FDT_NONE	0 /* none present */
#define FDT_12M		1 /* 1M/640K FDD */
#define FDT_144M	2 /* 1.44M/1M/640K FDD */

#define FD_1200         1
#define FD_1232         2
#define FD_720          3
#define FD_640          4
#define FD_1440         5
#else
#define FD_1720         1
#define FD_1480         2
#define FD_1440         3
#define FD_1200         4
#define FD_820          5
#define FD_800          6
#define FD_720          7
#define FD_360          8

#define FD_1480in5_25   9
#define FD_1440in5_25   10
#define FD_820in5_25    11
#define FD_800in5_25    12
#define FD_720in5_25    13
#define FD_360in5_25    14
#endif


static struct fd_type fd_types[NUMTYPES] =
{
#ifdef PC98
{ 15,2,0xFF,0x1B,80,2400,1,0,2,0x54,1 }, /* 1.2 meg HD floppy    */
{  8,3,0xFF,0x35,77,1232,1,0,2,0x74,1 }, /* 1.2 meg HD floppy 1024/sec  */
{  9,2,0xFF,0x20,80,1440,1,1,2,0x50,1 }, /* 720k floppy in 1.2meg drive */
{  8,2,0xFF,0x2A,80,1280,1,1,2,0x50,1 }, /* 640k floppy in 1.2meg drive */
{ 18,2,0xFF,0x1B,80,2880,1,2,2,0x54,1 }, /* 1.44 meg HD 3.5in floppy */
#else
{ 21,2,0xFF,0x04,82,3444,1,FDC_500KBPS,2,0x0C,2 }, /* 1.72M in HD 3.5in */
{ 18,2,0xFF,0x1B,82,2952,1,FDC_500KBPS,2,0x6C,1 }, /* 1.48M in HD 3.5in */
{ 18,2,0xFF,0x1B,80,2880,1,FDC_500KBPS,2,0x6C,1 }, /* 1.44M in HD 3.5in */
{ 15,2,0xFF,0x1B,80,2400,1,FDC_500KBPS,2,0x54,1 }, /*  1.2M in HD 5.25/3.5 */
{ 10,2,0xFF,0x10,82,1640,1,FDC_250KBPS,2,0x2E,1 }, /*  820K in HD 3.5in */
{ 10,2,0xFF,0x10,80,1600,1,FDC_250KBPS,2,0x2E,1 }, /*  800K in HD 3.5in */
{  9,2,0xFF,0x20,80,1440,1,FDC_250KBPS,2,0x50,1 }, /*  720K in HD 3.5in */
{  9,2,0xFF,0x2A,40, 720,1,FDC_250KBPS,2,0x50,1 }, /*  360K in DD 5.25in */

{ 18,2,0xFF,0x02,82,2952,1,FDC_500KBPS,2,0x02,2 }, /* 1.48M in HD 5.25in */
{ 18,2,0xFF,0x02,80,2880,1,FDC_500KBPS,2,0x02,2 }, /* 1.44M in HD 5.25in */
{ 10,2,0xFF,0x10,82,1640,1,FDC_300KBPS,2,0x2E,1 }, /*  820K in HD 5.25in */
{ 10,2,0xFF,0x10,80,1600,1,FDC_300KBPS,2,0x2E,1 }, /*  800K in HD 5.25in */
{  9,2,0xFF,0x20,80,1440,1,FDC_300KBPS,2,0x50,1 }, /*  720K in HD 5.25in */
{  9,2,0xFF,0x23,40, 720,2,FDC_300KBPS,2,0x50,1 }, /*  360K in HD 5.25in */
#endif
};

#ifdef PC98
#define DRVS_PER_CTLR 4		/* 4 floppies */
#else
#define DRVS_PER_CTLR 2		/* 2 floppies */
#endif

/***********************************************************************\
* Per controller structure.						*
\***********************************************************************/
struct fdc_data fdc_data[NFDC];

/***********************************************************************\
* Per drive structure.							*
* N per controller  (DRVS_PER_CTLR)					*
\***********************************************************************/
static struct fd_data {
	struct	fdc_data *fdc;	/* pointer to controller structure */
	int	fdsu;		/* this units number on this controller */
	int	type;		/* Drive type (FD_1440...) */
	struct	fd_type *ft;	/* pointer to the type descriptor */
	int	flags;
#define	FD_OPEN		0x01	/* it's open		*/
#define	FD_ACTIVE	0x02	/* it's active		*/
#define	FD_MOTOR	0x04	/* motor should be on	*/
#define	FD_MOTOR_WAIT	0x08	/* motor coming up	*/
	int	skip;
	int	hddrv;
#define FD_NO_TRACK -2
	int	track;		/* where we think the head is */
	int	options;	/* user configurable options, see ioctl_fd.h */
	struct	callout_handle toffhandle;
	struct	callout_handle tohandle;
	struct	devstat device_stats;
#ifdef DEVFS
	void	*bdevs[1 + NUMDENS + MAXPARTITIONS];
	void	*cdevs[1 + NUMDENS + MAXPARTITIONS];
#endif
#ifdef PC98
	int	pc98_trans;
#endif
} fd_data[NFD];

#ifdef EPSON_NRDISK
typedef unsigned int	nrd_t;

#define	P_NRD_ADDRH	0xc24
#define	P_NRD_ADDRM	0xc22
#define	P_NRD_ADDRL	0xc20
#define	P_NRD_CHECK	0xc20
#define	P_NRD_DATA	0xc26
#define	P_NRD_LED	0xc36
#define	B_NRD_CHK	0x80
#define	B_NRD_LED	0x40
#define	A_NRD_INFO	0x2
#define	A_NRD_BASE	0x400
#define NRD_STATUS	0x0
#define NRD_ST0_HD	0x04

static fdu_t nrdu=-1;
static int nrdsec=0;
static nrd_t nrdblkn=0;
static nrd_t nrdaddr=0x0;

#define	nrd_check_ready()	({		\
	(epson_inb(P_NRD_CHECK) & B_NRD_CHK) ? 0 : 1;	\
	})
#define	nrd_LED_on()	epson_outb(P_NRD_LED, B_NRD_LED)
#define	nrd_LED_off()	epson_outb(P_NRD_LED, ~B_NRD_LED)
#define	nrd_trac()	((int)(nrd_info(nrdaddr) & 0xff))
#define	nrd_head()	((int)((nrd_info(nrdaddr) >> 8) & 0xff))
#define	nrd_sec()	((int)(nrd_info(nrdaddr + 2) & 0xff))
#define	nrd_secsize()	((int)((nrd_info(A_NRD_INFO) >> 8) & 0xff))
#define	nrd_addrset(p)	nrd_addr((nrd_t)((nrd_t)p+A_NRD_BASE))

static inline void
nrd_addr(addr)
	nrd_t	addr;
{
	epson_outb(P_NRD_ADDRH, (u_char)((addr >> 16) & 0x1f));
	epson_outb(P_NRD_ADDRM, (u_char)((addr >> 8) & 0xff));
	epson_outb(P_NRD_ADDRL, (u_char)(addr & 0xff));
}

static inline u_short
nrd_info(addr)
	nrd_t	addr;
{
	u_short	tmp;

	nrd_addr(addr);
	outb(0x43f, 0x42);
	tmp = (short)inw(P_NRD_DATA);
	outb(0x43f, 0x40);
	return ((u_short)tmp);
}
#endif /* EPSON_NRDISK */

/***********************************************************************\
* Throughout this file the following conventions will be used:		*
* fd is a pointer to the fd_data struct for the drive in question	*
* fdc is a pointer to the fdc_data struct for the controller		*
* fdu is the floppy drive unit number					*
* fdcu is the floppy controller unit number				*
* fdsu is the floppy drive unit number on that controller. (sub-unit)	*
\***********************************************************************/

#ifdef FDC_YE
#include "card.h"
static int yeattach(struct isa_device *);
#endif

/* autoconfig functions */
static int fdprobe(struct isa_device *);
static int fdattach(struct isa_device *);

/* needed for ft driver, thus exported */
int in_fdc(fdcu_t);
int out_fdc(fdcu_t, int);

/* internal functions */
static void set_motor(fdcu_t, int, int);
#  define TURNON 1
#  define TURNOFF 0
static timeout_t fd_turnoff;
static timeout_t fd_motor_on;
static void fd_turnon(fdu_t);
static void fdc_reset(fdc_p);
static int fd_in(fdcu_t, int *);
static void fdstart(fdcu_t);
static timeout_t fd_iotimeout;
static timeout_t fd_pseudointr;
static ointhand2_t fdintr;
static int fdstate(fdcu_t, fdc_p);
static int retrier(fdcu_t);
static int fdformat(dev_t, struct fd_formb *, struct proc *);

static int enable_fifo(fdc_p fdc);

static int fifo_threshold = 8;	/* XXX: should be accessible via sysctl */


#define DEVIDLE		0
#define FINDWORK	1
#define	DOSEEK		2
#define SEEKCOMPLETE 	3
#define	IOCOMPLETE	4
#define RECALCOMPLETE	5
#define	STARTRECAL	6
#define	RESETCTLR	7
#define	SEEKWAIT	8
#define	RECALWAIT	9
#define	MOTORWAIT	10
#define	IOTIMEDOUT	11
#define	RESETCOMPLETE	12
#ifdef FDC_YE
#define PIOREAD		13
#endif

#ifdef	FDC_DEBUG
static char const * const fdstates[] =
{
"DEVIDLE",
"FINDWORK",
"DOSEEK",
"SEEKCOMPLETE",
"IOCOMPLETE",
"RECALCOMPLETE",
"STARTRECAL",
"RESETCTLR",
"SEEKWAIT",
"RECALWAIT",
"MOTORWAIT",
"IOTIMEDOUT",
"RESETCOMPLETE",
#ifdef FDC_YE
"PIOREAD",
#endif
};

/* CAUTION: fd_debug causes huge amounts of logging output */
static int volatile fd_debug = 0;
#define TRACE0(arg) if(fd_debug) printf(arg)
#define TRACE1(arg1, arg2) if(fd_debug) printf(arg1, arg2)
#else /* FDC_DEBUG */
#define TRACE0(arg)
#define TRACE1(arg1, arg2)
#endif /* FDC_DEBUG */

#ifdef FDC_YE
#if NCARD > 0
#include <sys/select.h>
#include <sys/module.h>
#include <pccard/cardinfo.h>
#include <pccard/driver.h>
#include <pccard/slot.h>

/*
 *	PC-Card (PCMCIA) specific code.
 */
static int yeinit(struct pccard_devinfo *);		/* init device */
static void yeunload(struct pccard_devinfo *); 		/* Disable driver */
static int yeintr(struct pccard_devinfo *); 		/* Interrupt handler */

PCCARD_MODULE(fdc, yeinit, yeunload, yeintr, 0, bio_imask);

/*
 * this is the secret PIO data port (offset from base)
 */
#define FDC_YE_DATAPORT 6

/*
 *	Initialize the device - called from Slot manager.
 */
static int yeinit(struct pccard_devinfo *devi)
{
	fdc_p fdc = &fdc_data[devi->isahd.id_unit];

	/* validate unit number. */
	if (devi->isahd.id_unit >= NFDC)
		return(ENODEV);
	fdc->baseport = devi->isahd.id_iobase;
	/*
	 * reset controller
	 */
	outb(fdc->baseport+FDOUT, 0);
	DELAY(100);
	outb(fdc->baseport+FDOUT, FDO_FRST);

	/*
	 * wire into system
	 */
	if (yeattach(&devi->isahd) == 0)
		return(ENXIO);

	return(0);
}

/*
 *	yeunload - unload the driver and clear the table.
 *	XXX TODO:
 *	This is usually called when the card is ejected, but
 *	can be caused by a modunload of a controller driver.
 *	The idea is to reset the driver's view of the device
 *	and ensure that any driver entry points such as
 *	read and write do not hang.
 */
static void yeunload(struct pccard_devinfo *devi)
{
	if (fd_data[devi->isahd.id_unit].type == NO_TYPE)
		return;

	/*
	 * this prevents Fdopen() and fdstrategy() from attempting
	 * to access unloaded controller
	 */
	fd_data[devi->isahd.id_unit].type = NO_TYPE;

	printf("fdc%d: unload\n", devi->isahd.id_unit);
}

/*
 *	yeintr - Shared interrupt called from
 *	front end of PC-Card handler.
 */
static int yeintr(struct pccard_devinfo *devi)
{
	fdintr((fdcu_t)devi->isahd.id_unit);
	return(1);
}
#endif /* NCARD > 0 */
#endif /* FDC_YE */


/* autoconfig structure */

struct	isa_driver fdcdriver = {
	fdprobe, fdattach, "fdc",
};

static	d_open_t	Fdopen;	/* NOTE, not fdopen */
static	d_read_t	fdread;
static	d_write_t	fdwrite;
static	d_close_t	fdclose;
static	d_ioctl_t	fdioctl;
static	d_strategy_t	fdstrategy;

/* even if SLICE defined, these are needed for the ft support. */
#define CDEV_MAJOR 9
#define BDEV_MAJOR 2


static struct cdevsw fd_cdevsw = {
	  Fdopen,	fdclose,	fdread,	fdwrite,
	  fdioctl,	nostop,		nullreset,	nodevtotty,
	  seltrue,	nommap,		fdstrategy,	"fd",
	  NULL,		-1,		nodump,		nopsize,
	  D_DISK,	0,		-1 };


static struct isa_device *fdcdevs[NFDC];


static int
fdc_err(fdcu_t fdcu, const char *s)
{
	fdc_data[fdcu].fdc_errs++;
	if(s) {
		if(fdc_data[fdcu].fdc_errs < FDC_ERRMAX)
			printf("fdc%d: %s", fdcu, s);
		else if(fdc_data[fdcu].fdc_errs == FDC_ERRMAX)
			printf("fdc%d: too many errors, not logging any more\n",
			    fdcu);
	}

	return FD_FAILED;
}

/*
 * fd_cmd: Send a command to the chip.  Takes a varargs with this structure:
 * Unit number,
 * # of output bytes, output bytes as ints ...,
 * # of input bytes, input bytes as ints ...
 */

static int
fd_cmd(fdcu_t fdcu, int n_out, ...)
{
	u_char cmd;
	int n_in;
	int n;
	va_list ap;

	va_start(ap, n_out);
	cmd = (u_char)(va_arg(ap, int));
	va_end(ap);
	va_start(ap, n_out);
	for (n = 0; n < n_out; n++)
	{
		if (out_fdc(fdcu, va_arg(ap, int)) < 0)
		{
			char msg[50];
			snprintf(msg, sizeof(msg),
				"cmd %x failed at out byte %d of %d\n",
				cmd, n + 1, n_out);
			return fdc_err(fdcu, msg);
		}
	}
	n_in = va_arg(ap, int);
	for (n = 0; n < n_in; n++)
	{
		int *ptr = va_arg(ap, int *);
		if (fd_in(fdcu, ptr) < 0)
		{
			char msg[50];
			snprintf(msg, sizeof(msg),
				"cmd %02x failed at in byte %d of %d\n",
				cmd, n + 1, n_in);
			return fdc_err(fdcu, msg);
		}
	}

	return 0;
}

static int 
enable_fifo(fdc_p fdc)
{
	int i, j;

	if ((fdc->flags & FDC_HAS_FIFO) == 0) {
		
		/*
		 * XXX: 
		 * Cannot use fd_cmd the normal way here, since
		 * this might be an invalid command. Thus we send the
		 * first byte, and check for an early turn of data directon.
		 */
		
		if (out_fdc(fdc->fdcu, I8207X_CONFIGURE) < 0)
			return fdc_err(fdc->fdcu, "Enable FIFO failed\n");
		
		/* If command is invalid, return */
		j = 100000;
		while ((i = inb(fdc->baseport + FDSTS) & (NE7_DIO | NE7_RQM))
		       != NE7_RQM && j-- > 0)
			if (i == (NE7_DIO | NE7_RQM)) {
				fdc_reset(fdc);
				return FD_FAILED;
			}
		if (j<0 || 
		    fd_cmd(fdc->fdcu, 3,
			   0, (fifo_threshold - 1) & 0xf, 0, 0) < 0) {
			fdc_reset(fdc);
			return fdc_err(fdc->fdcu, "Enable FIFO failed\n");
		}
		fdc->flags |= FDC_HAS_FIFO;
		return 0;
	}
	if (fd_cmd(fdc->fdcu, 4,
		   I8207X_CONFIGURE, 0, (fifo_threshold - 1) & 0xf, 0, 0) < 0)
		return fdc_err(fdc->fdcu, "Re-enable FIFO failed\n");
	return 0;
}

static int
fd_sense_drive_status(fdc_p fdc, int *st3p)
{
	int st3;

	if (fd_cmd(fdc->fdcu, 2, NE7CMD_SENSED, fdc->fdu, 1, &st3))
	{
		return fdc_err(fdc->fdcu, "Sense Drive Status failed\n");
	}
	if (st3p)
		*st3p = st3;

	return 0;
}

static int
fd_sense_int(fdc_p fdc, int *st0p, int *cylp)
{
	int st0, cyl;

#ifdef EPSON_NRDISK
	if (fdc->fdu == nrdu) {
		if (fdc->fd->track >= 0) nrdaddr = (fdc->fd->track + 1) * 8;
		else nrdaddr = 0x0;
		*st0p = nrd_head() ? NRD_ST0_HD : NRD_STATUS;
		*cylp = nrd_trac();
	}
	else {
#endif /* EPSON_NRDISK */
	int ret = fd_cmd(fdc->fdcu, 1, NE7CMD_SENSEI, 1, &st0);

	if (ret)
	{
		(void)fdc_err(fdc->fdcu,
			      "sense intr err reading stat reg 0\n");
		return ret;
	}

	if (st0p)
		*st0p = st0;

	if ((st0 & NE7_ST0_IC) == NE7_ST0_IC_IV)
	{
		/*
		 * There doesn't seem to have been an interrupt.
		 */
		return FD_NOT_VALID;
	}

	if (fd_in(fdc->fdcu, &cyl) < 0)
	{
		return fdc_err(fdc->fdcu, "can't get cyl num\n");
	}

	if (cylp)
		*cylp = cyl;

#ifdef EPSON_NRDISK
	}
#endif /* EPSON_NRDISK */
	return 0;
}


static int
fd_read_status(fdc_p fdc, int fdsu)
{
	int i, ret;

	for (i = 0; i < 7; i++)
	{
		/*
		 * XXX types are poorly chosen.  Only bytes can by read
		 * from the hardware, but fdc->status[] wants u_ints and
		 * fd_in() gives ints.
		 */
		int status;

#ifdef EPSON_NRDISK
		if (fdc->fdu == nrdu) {
			switch (i) {
				case 0:	fdc->status[i] = nrd_head()
					? NRD_ST0_HD : NRD_STATUS; break;
				case 1:	fdc->status[i] = NRD_STATUS; break;
				case 2:	fdc->status[i] = NRD_STATUS; break;
				case 3:	fdc->status[i] = nrd_trac(); break;
				case 4:	fdc->status[i] = nrd_head(); break;
				case 5:	fdc->status[i] = nrdsec; break;
				case 6:	fdc->status[i] = nrd_secsize(); break;
			}
			ret = 0;
		}
		else {
#endif /* EPSON_NRDISK */
		ret = fd_in(fdc->fdcu, &status);
		fdc->status[i] = status;
		if (ret != 0)
			break;
#ifdef EPSON_NRDISK
		}
#endif /* EPSON_NRDISK */
	}

	if (ret == 0)
		fdc->flags |= FDC_STAT_VALID;
	else
		fdc->flags &= ~FDC_STAT_VALID;

	return ret;
}

/****************************************************************************/
/*                      autoconfiguration stuff                             */
/****************************************************************************/
#ifdef PC98
static int pc98_trans = 0; /* 0 : HD , 1 : DD , 2 : 1.44 */
static int pc98_trans_prev = 0;

static void set_density(fdcu_t, fdu_t);
static int pc98_fd_check_ready(fdu_t);

static void set_density(fdcu, fdu)
	fdcu_t fdcu;
	fdu_t fdu;
{
	/* always motor on */
	outb(IO_FDPORT,
		 (pc98_trans != 1 ? FDP_FDDEXC : 0) | FDP_PORTEXC);
	DELAY(100);
	outb(fdc_data[fdcu].baseport + FDOUT, FDO_RST | FDO_DMAE);
	/* in the case of note W, always inhibit 100ms timer */
}

static int pc98_fd_check_ready(fdu)
	fdu_t fdu;
{
	fd_p fd = fd_data + fdu;
	fdcu_t fdcu = fd->fdc->fdcu;
	int retry = 0;

#ifdef EPSON_NRDISK
	if (fdu == nrdu) {
		if (nrd_check_ready()) return 0;
		else return -1;
	}
#endif
	while (retry++ < 30000) {
		set_motor(fdcu, fd->fdsu, TURNON);
		out_fdc(fdcu, NE7CMD_SENSED); /* Sense Drive Status */
		DELAY(100);
		out_fdc(fdcu, fdu); /* Drive number */
		DELAY(100);
		if ((in_fdc(fdcu) & NE7_ST3_RD)){
			outb(fdc_data[fdcu].baseport + FDOUT,
				FDO_DMAE | FDO_MTON);
			DELAY(10);
			return 0;
		}
	}
	return -1;
}
#endif


/*
 * probe for existance of controller
 */
static int
fdprobe(struct isa_device *dev)
{
	fdcu_t	fdcu = dev->id_unit;
	if(fdc_data[fdcu].flags & FDC_ATTACHED)
	{
		printf("fdc%d: unit used multiple times\n", fdcu);
		return 0;
	}

	fdcdevs[fdcu] = dev;
	fdc_data[fdcu].baseport = dev->id_iobase;

#ifndef PC98
	/* First - lets reset the floppy controller */
	outb(dev->id_iobase+FDOUT, 0);
	DELAY(100);
	outb(dev->id_iobase+FDOUT, FDO_FRST);
#endif

	/* see if it can handle a command */
#ifdef PC98
	if (fd_cmd(fdcu,
		   3, NE7CMD_SPECIFY, NE7_SPEC_1(4, 240), NE7_SPEC_2(2, 0),
		   0))
#else
	if (fd_cmd(fdcu,
		   3, NE7CMD_SPECIFY, NE7_SPEC_1(3, 240), NE7_SPEC_2(2, 0),
		   0))
#endif
	{
		return(0);
	}
#ifdef FDC_YE
	/*
	 * don't succeed on probe; wait
	 * for PCCARD subsystem to do it
	 */
	if (dev->id_flags & FDC_IS_PCMCIA)
		return(0);
#endif
	return (IO_FDCSIZE);
}

/*
 * wire controller into system, look for floppy units
 */
static int
fdattach(struct isa_device *dev)
{
	unsigned fdt;
	fdu_t	fdu;
	fdcu_t	fdcu = dev->id_unit;
	fdc_p	fdc = fdc_data + fdcu;
	fd_p	fd;
	int	fdsu, st0, st3, i;
	struct isa_device *fdup;
	int ic_type = 0;
#ifdef DEVFS
	int	mynor;
	int	typemynor;
	int	typesize;
#endif

	dev->id_ointr = fdintr;
	fdc->fdcu = fdcu;
	fdc->flags |= FDC_ATTACHED;
#ifdef PC98
	fdc->dmachan = 2;
	if (fdc->dmachan != dev->id_drq) {
		dev->id_drq = fdc->dmachan;
		printf(" [dma is changed to #%d]", fdc->dmachan);
	}
	/* Acquire the DMA channel forever, The driver will do the rest */
	isa_dma_acquire(fdc->dmachan);
	isa_dmainit(fdc->dmachan, 128 << 3 /* XXX max secsize */);
	fdc->state = DEVIDLE;
	fdc_reset(fdc);
#else
	fdc->dmachan = dev->id_drq;
	/* Acquire the DMA channel forever, The driver will do the rest */
	isa_dma_acquire(fdc->dmachan);
	isa_dmainit(fdc->dmachan, 128 << 3 /* XXX max secsize */);
	fdc->state = DEVIDLE;
	/* reset controller, turn motor off, clear fdout mirror reg */
	outb(fdc->baseport + FDOUT, ((fdc->fdout = 0)));
#endif
	bufq_init(&fdc->head);

	/* check for each floppy drive */
	for (fdup = isa_biotab_fdc; fdup->id_driver != 0; fdup++) {
		if (fdup->id_iobase != dev->id_iobase)
			continue;
		fdu = fdup->id_unit;
		fd = &fd_data[fdu];
		if (fdu >= (NFD))
			continue;
		fdsu = fdup->id_physid;
		/* look up what bios thinks we have */
		switch (fdu) {
#ifdef PC98
		case 0: case 1: case 2: case 3:
			if ((PC98_SYSTEM_PARAMETER(0x5ae) >> fdu) & 0x01)
				fdt = FDT_144M;
#ifdef EPSON_NRDISK
			else if ((PC98_SYSTEM_PARAMETER(0x55c) >> fdu) & 0x01) {
				fdt = FDT_12M;
				switch (epson_machine_id) {
				    case 0x20: case 0x27:
					if ((PC98_SYSTEM_PARAMETER(0x488) >> fdu) & 0x01) {
						if (nrd_check_ready()) {
							nrd_LED_on();
							nrdu = fdu;
						}
						else fdt = FDT_NONE;
					}
				}
			}
#else /* !EPSON_NRDISK */
			else if ((PC98_SYSTEM_PARAMETER(0x55c) >> fdu) & 0x01) {
				fdt = FDT_12M;
				switch (epson_machine_id) {
				    case 0x20: case 0x27:
					if ((PC98_SYSTEM_PARAMETER(0x488) >> fdu) & 0x01)
						fdt = FDT_NONE;
				}
			}
#endif /* EPSON_NRDISK */
			else fdt = FDT_NONE;
			break;
		default:
			fdt = FDT_NONE;
			break;
#else
			case 0: if (dev->id_flags & FDC_PRETEND_D0)
					fdt = RTCFDT_144M | RTCFDT_144M_PRETENDED;
				else
					fdt = (rtcin(RTC_FDISKETTE) & 0xf0);
				break;
			case 1: fdt = ((rtcin(RTC_FDISKETTE) << 4) & 0xf0);
				break;
			default: fdt = RTCFDT_NONE;
				break;
#endif
		}
		/* is there a unit? */
#ifdef PC98
		if ((fdt == FDT_NONE)
#else
		if ((fdt == RTCFDT_NONE)
#endif
		) {
#ifdef PC98
			fd->fdc = fdc;
#endif
			fd->type = NO_TYPE;
			continue;
		}

#ifndef PC98
		/* select it */
		set_motor(fdcu, fdsu, TURNON);
		DELAY(1000000);	/* 1 sec */

		if (ic_type == 0 &&
		    fd_cmd(fdcu, 1, NE7CMD_VERSION, 1, &ic_type) == 0)
		{
#ifdef FDC_PRINT_BOGUS_CHIPTYPE
			printf("fdc%d: ", fdcu);
#endif
			ic_type = (u_char)ic_type;
			switch( ic_type ) {
			case 0x80:
#ifdef FDC_PRINT_BOGUS_CHIPTYPE
				printf("NEC 765\n");
#endif
				fdc->fdct = FDC_NE765;
				break;
			case 0x81:
#ifdef FDC_PRINT_BOGUS_CHIPTYPE
				printf("Intel 82077\n");
#endif
				fdc->fdct = FDC_I82077;
				break;
			case 0x90:
#ifdef FDC_PRINT_BOGUS_CHIPTYPE
				printf("NEC 72065B\n");
#endif
				fdc->fdct = FDC_NE72065;
				break;
			default:
#ifdef FDC_PRINT_BOGUS_CHIPTYPE
				printf("unknown IC type %02x\n", ic_type);
#endif
				fdc->fdct = FDC_UNKNOWN;
				break;
			}
			if (fdc->fdct != FDC_NE765 &&
			    fdc->fdct != FDC_UNKNOWN && 
			    enable_fifo(fdc) == 0) {
				printf("fdc%d: FIFO enabled", fdcu);
				printf(", %d bytes threshold\n", 
				    fifo_threshold);
			}
		}
		if ((fd_cmd(fdcu, 2, NE7CMD_SENSED, fdsu, 1, &st3) == 0) &&
		    (st3 & NE7_ST3_T0)) {
			/* if at track 0, first seek inwards */
			/* seek some steps: */
			(void)fd_cmd(fdcu, 3, NE7CMD_SEEK, fdsu, 10, 0);
			DELAY(300000); /* ...wait a moment... */
			(void)fd_sense_int(fdc, 0, 0); /* make ctrlr happy */
		}

		/* If we're at track 0 first seek inwards. */
		if ((fd_sense_drive_status(fdc, &st3) == 0) &&
		    (st3 & NE7_ST3_T0)) {
			/* Seek some steps... */
			if (fd_cmd(fdcu, 3, NE7CMD_SEEK, fdsu, 10, 0) == 0) {
				/* ...wait a moment... */
				DELAY(300000);
				/* make ctrlr happy: */
				(void)fd_sense_int(fdc, 0, 0);
			}
		}

		for(i = 0; i < 2; i++) {
			/*
			 * we must recalibrate twice, just in case the
			 * heads have been beyond cylinder 76, since most
			 * FDCs still barf when attempting to recalibrate
			 * more than 77 steps
			 */
			/* go back to 0: */
			if (fd_cmd(fdcu, 2, NE7CMD_RECAL, fdsu, 0) == 0) {
				/* a second being enough for full stroke seek*/
				DELAY(i == 0? 1000000: 300000);

				/* anything responding? */
				if (fd_sense_int(fdc, &st0, 0) == 0 &&
				(st0 & NE7_ST0_EC) == 0)
					break; /* already probed succesfully */
			}
		}

		set_motor(fdcu, fdsu, TURNOFF);

		if (st0 & NE7_ST0_EC) /* no track 0 -> no drive present */
			continue;
#endif

		fd->track = FD_NO_TRACK;
		fd->fdc = fdc;
		fd->fdsu = fdsu;
		fd->options = 0;
		callout_handle_init(&fd->toffhandle);
		callout_handle_init(&fd->tohandle);
		printf("fd%d: ", fdu);

		switch (fdt) {
#ifdef PC98
		case FDT_12M:
#ifdef EPSON_NRDISK
			if (fdu == nrdu) {
				printf("EPSON RAM DRIVE\n");
				nrd_LED_off();
			}
			else printf("1M/640M FDD\n");
#else /* !EPSON_NRDISK */
			printf("1M/640K FDD\n");
#endif /* EPSON_NRDISK */
			fd->type = FD_1200;
			fd->pc98_trans = 0;
			break;
		case FDT_144M:
			printf("1.44M FDD\n");
			fd->type = FD_1200;
			fd->pc98_trans = 0;
			outb(0x4be, (fdu << 5) | 0x10);
			break;
#else
		case RTCFDT_12M:
			printf("1.2MB 5.25in\n");
			fd->type = FD_1200;
			break;
		case RTCFDT_144M | RTCFDT_144M_PRETENDED:
			printf("config-pretended ");
			fdt = RTCFDT_144M;
			/* fallthrough */
		case RTCFDT_144M:
			printf("1.44MB 3.5in\n");
			fd->type = FD_1440;
			break;
		case RTCFDT_288M:
		case RTCFDT_288M_1:
			printf("2.88MB 3.5in - 1.44MB mode\n");
			fd->type = FD_1440;
			break;
		case RTCFDT_360K:
			printf("360KB 5.25in\n");
			fd->type = FD_360;
			break;
		case RTCFDT_720K:
			printf("720KB 3.5in\n");
			fd->type = FD_720;
			break;
#endif
		default:
			printf("unknown\n");
			fd->type = NO_TYPE;
			continue;
		}
#ifdef DEVFS
		mynor = fdu << 6;
		fd->bdevs[0] = devfs_add_devswf(&fd_cdevsw, mynor, DV_BLK,
						UID_ROOT, GID_OPERATOR, 0640,
						"fd%d", fdu);
		fd->cdevs[0] = devfs_add_devswf(&fd_cdevsw, mynor, DV_CHR,
						UID_ROOT, GID_OPERATOR, 0640,
						"rfd%d", fdu);
		for (i = 1; i < 1 + NUMDENS; i++) {
			/*
			 * XXX this and the lookup in Fdopen() should be
			 * data driven.
			 */
#ifdef PC98
			switch (fdt) {
			case FDT_12M:
				if (i != FD_1200 && i != FD_1232
				    && i != FD_720 && i != FD_640)
					continue;
				break;
			case FDT_144M:
				if (i != FD_1200 && i != FD_1232
				    && i != FD_720 && i != FD_640
				    && i != FD_1440)
					continue;
				break;
			}
#else
			switch (fd->type) {
			case FD_360:
				if (i != FD_360)
					continue;
				break;
			case FD_720:
				if (i != FD_720 && i != FD_800 && i != FD_820)
					continue;
				break;
			case FD_1200:
				if (i != FD_360 && i != FD_720 && i != FD_800
				    && i != FD_820 && i != FD_1200
				    && i != FD_1440 && i != FD_1480)
					continue;
				break;
			case FD_1440:
				if (i != FD_720 && i != FD_800 && i != FD_820
				    && i != FD_1200 && i != FD_1440
				    && i != FD_1480 && i != FD_1720)
					continue;
				break;
			}
#endif
#ifdef PC98
			if (i == FD_1232)
				typesize = fd_types[i - 1].size;
			else
				typesize = fd_types[i - 1].size / 2;
#else
			typesize = fd_types[i - 1].size / 2;
			/*
			 * XXX all these conversions give bloated code and
			 * confusing names.
			 */
			if (typesize == 1476)
				typesize = 1480;
			if (typesize == 1722)
				typesize = 1720;
#endif
			typemynor = mynor | i;
			fd->bdevs[i] =
				devfs_add_devswf(&fd_cdevsw, typemynor, DV_BLK,
						 UID_ROOT, GID_OPERATOR, 0640,
						 "fd%d.%d", fdu, typesize);
			fd->cdevs[i] =
				devfs_add_devswf(&fd_cdevsw, typemynor, DV_CHR,
						 UID_ROOT, GID_OPERATOR, 0640,
						 "rfd%d.%d", fdu, typesize);
		}

		for (i = 0; i < MAXPARTITIONS; i++) {
			fd->bdevs[1 + NUMDENS + i] = devfs_makelink(fd->bdevs[0],
					   "fd%d%c", fdu, 'a' + i);
			fd->cdevs[1 + NUMDENS + i] =
				devfs_makelink(fd->cdevs[0],
					   "rfd%d%c", fdu, 'a' + i);
		}
#endif /* DEVFS */
		/*
		 * Export the drive to the devstat interface.
		 */
		devstat_add_entry(&fd->device_stats, "fd", 
				  fdu, 512,
				  DEVSTAT_NO_ORDERED_TAGS,
				  DEVSTAT_TYPE_FLOPPY | DEVSTAT_TYPE_IF_OTHER,
				  DEVSTAT_PRIORITY_FD);
		
	}

	return (1);
}



#ifdef FDC_YE
/*
 * this is a subset of fdattach() optimized for the Y-E Data
 * PCMCIA floppy drive.
 */
static int yeattach(struct isa_device *dev)
{
	fdcu_t  fdcu = dev->id_unit;
	fdc_p   fdc = fdc_data + fdcu;
	fdsu_t  fdsu = 0;               /* assume 1 drive per YE controller */
	fdu_t   fdu;
	fd_p    fd;
	int     st0, st3, i;
#ifdef DEVFS
	int     mynor;
	int     typemynor;
	int     typesize;
#endif
	fdc->fdcu = fdcu;
	/*
	 * the FDC_PCMCIA flag is used to to indicate special PIO is used
	 * instead of DMA
	 */
	fdc->flags = FDC_ATTACHED|FDC_PCMCIA;
	fdc->state = DEVIDLE;
	/* reset controller, turn motor off, clear fdout mirror reg */
	outb(fdc->baseport + FDOUT, ((fdc->fdout = 0)));
	bufq_init(&fdc->head);
	/*
	 * assume 2 drives/ "normal" controller
	 */
	fdu = fdcu * 2;
	if (fdu >= NFD) {
		printf("fdu %d >= NFD\n",fdu);
		return(0);
	};
	fd = &fd_data[fdu];

	set_motor(fdcu, fdsu, TURNON);
	DELAY(1000000); /* 1 sec */
	fdc->fdct = FDC_NE765;

	if ((fd_cmd(fdcu, 2, NE7CMD_SENSED, fdsu, 1, &st3) == 0) &&
		(st3 & NE7_ST3_T0)) {
		/* if at track 0, first seek inwards */
		/* seek some steps: */
		(void)fd_cmd(fdcu, 3, NE7CMD_SEEK, fdsu, 10, 0);
		DELAY(300000); /* ...wait a moment... */
		(void)fd_sense_int(fdc, 0, 0); /* make ctrlr happy */
	}

	/* If we're at track 0 first seek inwards. */
	if ((fd_sense_drive_status(fdc, &st3) == 0) && (st3 & NE7_ST3_T0)) {
		/* Seek some steps... */
		if (fd_cmd(fdcu, 3, NE7CMD_SEEK, fdsu, 10, 0) == 0) {
			/* ...wait a moment... */
			DELAY(300000);
			/* make ctrlr happy: */
			(void)fd_sense_int(fdc, 0, 0);
		}
	}

	for(i = 0; i < 2; i++) {
		/*
		 * we must recalibrate twice, just in case the
		 * heads have been beyond cylinder 76, since most
		 * FDCs still barf when attempting to recalibrate
		 * more than 77 steps
		 */
		/* go back to 0: */
		if (fd_cmd(fdcu, 2, NE7CMD_RECAL, fdsu, 0) == 0) {
			/* a second being enough for full stroke seek*/
			DELAY(i == 0? 1000000: 300000);

			/* anything responding? */
			if (fd_sense_int(fdc, &st0, 0) == 0 &&
				(st0 & NE7_ST0_EC) == 0)
				break; /* already probed succesfully */
		}
	}

	set_motor(fdcu, fdsu, TURNOFF);

	if (st0 & NE7_ST0_EC) /* no track 0 -> no drive present */
		return(0);

	fd->track = FD_NO_TRACK;
	fd->fdc = fdc;
	fd->fdsu = fdsu;
	fd->options = 0;
	printf("fdc%d: 1.44MB 3.5in PCMCIA\n", fdcu);
	fd->type = FD_1440;

#ifdef DEVFS
	mynor = fdcu << 6;
	fd->bdevs[0] = devfs_add_devswf(&fd_cdevsw, mynor, DV_BLK,
		UID_ROOT, GID_OPERATOR, 0640,
		"fd%d", fdu);
	fd->cdevs[0] = devfs_add_devswf(&fd_cdevsw, mynor, DV_CHR,
		UID_ROOT, GID_OPERATOR, 0640,
		"rfd%d", fdu);
	/*
	 * XXX this and the lookup in Fdopen() should be
	 * data driven.
	 */
	typemynor = mynor | FD_1440;
	typesize = fd_types[FD_1440 - 1].size / 2;
	/*
	 * XXX all these conversions give bloated code and
	 * confusing names.
	 */
	if (typesize == 1476)
		typesize = 1480;
	if (typesize == 1722)
		typesize = 1720;
	fd->bdevs[FD_1440] = devfs_add_devswf(&fd_cdevsw, typemynor,
		DV_BLK, UID_ROOT, GID_OPERATOR,
		0640, "fd%d.%d", fdu, typesize);
	fd->cdevs[FD_1440] = devfs_add_devswf(&fd_cdevsw, typemynor,
		DV_CHR, UID_ROOT, GID_OPERATOR,
		0640,"rfd%d.%d", fdu, typesize);
	for (i = 0; i < MAXPARTITIONS; i++) {
		fd->bdevs[1 + NUMDENS + i] = devfs_makelink(fd->bdevs[0],
			"fd%d%c", fdu, 'a' + i);
		fd->cdevs[1 + NUMDENS + i] = devfs_makelink(fd->cdevs[0],
			"rfd%d%c", fdu, 'a' + i);
	}
#endif /* DEVFS */
	return (1);
}
#endif

/****************************************************************************/
/*                            motor control stuff                           */
/*		remember to not deselect the drive we're working on         */
/****************************************************************************/
static void
set_motor(fdcu_t fdcu, int fdsu, int turnon)
{
	int fdout = fdc_data[fdcu].fdout;
	int needspecify = 0;

#ifdef PC98
	outb(IO_FDPORT, (pc98_trans != 1 ? FDP_FDDEXC : 0)|FDP_PORTEXC);
	DELAY(10);
	fdout = FDO_DMAE|FDO_MTON;
#else
	if(turnon) {
		fdout &= ~FDO_FDSEL;
		fdout |= (FDO_MOEN0 << fdsu) + fdsu;
	} else
		fdout &= ~(FDO_MOEN0 << fdsu);

	if(!turnon
	   && (fdout & (FDO_MOEN0+FDO_MOEN1+FDO_MOEN2+FDO_MOEN3)) == 0)
		/* gonna turn off the last drive, put FDC to bed */
		fdout &= ~ (FDO_FRST|FDO_FDMAEN);
	else {
		/* make sure controller is selected and specified */
		if((fdout & (FDO_FRST|FDO_FDMAEN)) == 0)
			needspecify = 1;
		fdout |= (FDO_FRST|FDO_FDMAEN);
	}
#endif

	outb(fdc_data[fdcu].baseport+FDOUT, fdout);
	DELAY(10);
	fdc_data[fdcu].fdout = fdout;
	TRACE1("[0x%x->FDOUT]", fdout);

	if(needspecify) {
		/*
		 * XXX
		 * special case: since we have just woken up the FDC
		 * from its sleep, we silently assume the command will
		 * be accepted, and do not test for a timeout
		 */
#ifdef PC98
		(void)fd_cmd(fdcu, 3, NE7CMD_SPECIFY,
			     NE7_SPEC_1(4, 240), NE7_SPEC_2(2, 0),
			     0);
#else
		(void)fd_cmd(fdcu, 3, NE7CMD_SPECIFY,
			     NE7_SPEC_1(3, 240), NE7_SPEC_2(2, 0),
			     0);
#endif
		if (fdc_data[fdcu].flags & FDC_HAS_FIFO)
			(void) enable_fifo(&fdc_data[fdcu]);

	}
}

static void
fd_turnoff(void *arg1)
{
	fdu_t fdu = (fdu_t)arg1;
	int	s;
	fd_p fd = fd_data + fdu;

	TRACE1("[fd%d: turnoff]", fdu);

	/*
	 * Don't turn off the motor yet if the drive is active.
	 * XXX shouldn't even schedule turnoff until drive is inactive
	 * and nothing is queued on it.
	 */
	if (fd->fdc->state != DEVIDLE && fd->fdc->fdu == fdu) {
		fd->toffhandle = timeout(fd_turnoff, arg1, 4 * hz);
		return;
	}

	s = splbio();
	fd->flags &= ~FD_MOTOR;
	set_motor(fd->fdc->fdcu, fd->fdsu, TURNOFF);
	splx(s);
}

static void
fd_motor_on(void *arg1)
{
	fdu_t fdu = (fdu_t)arg1;
	int	s;

	fd_p fd = fd_data + fdu;
	s = splbio();
	fd->flags &= ~FD_MOTOR_WAIT;
	if((fd->fdc->fd == fd) && (fd->fdc->state == MOTORWAIT))
	{
		fdintr(fd->fdc->fdcu);
	}
	splx(s);
}

static void
fd_turnon(fdu_t fdu)
{
	fd_p fd = fd_data + fdu;
	if(!(fd->flags & FD_MOTOR))
	{
		fd->flags |= (FD_MOTOR + FD_MOTOR_WAIT);
		set_motor(fd->fdc->fdcu, fd->fdsu, TURNON);
		timeout(fd_motor_on, (caddr_t)fdu, hz); /* in 1 sec its ok */
	}
}

static void
fdc_reset(fdc_p fdc)
{
	fdcu_t fdcu = fdc->fdcu;

	/* Try a reset, keep motor on */
#ifdef PC98
	set_density(fdcu, 0);
	if (pc98_machine_type & M_EPSON_PC98)
		outb(fdc->baseport + FDOUT, 0xe8);
	else
		outb(fdc->baseport + FDOUT, 0xd8);
	DELAY(200);
	outb(fdc->baseport + FDOUT, 0x18);
	DELAY(10);
#else
	outb(fdc->baseport + FDOUT, fdc->fdout & ~(FDO_FRST|FDO_FDMAEN));
	TRACE1("[0x%x->FDOUT]", fdc->fdout & ~(FDO_FRST|FDO_FDMAEN));
	DELAY(100);
	/* enable FDC, but defer interrupts a moment */
	outb(fdc->baseport + FDOUT, fdc->fdout & ~FDO_FDMAEN);
	TRACE1("[0x%x->FDOUT]", fdc->fdout & ~FDO_FDMAEN);
	DELAY(100);
	outb(fdc->baseport + FDOUT, fdc->fdout);
	TRACE1("[0x%x->FDOUT]", fdc->fdout);
#endif

	/* XXX after a reset, silently believe the FDC will accept commands */
#ifdef PC98
	(void)fd_cmd(fdcu, 3, NE7CMD_SPECIFY,
		     NE7_SPEC_1(4, 240), NE7_SPEC_2(2, 0),
		     0);
#else
	(void)fd_cmd(fdcu, 3, NE7CMD_SPECIFY,
		     NE7_SPEC_1(3, 240), NE7_SPEC_2(2, 0),
		     0);
#endif
	if (fdc->flags & FDC_HAS_FIFO)
		(void) enable_fifo(fdc);
}

/****************************************************************************/
/*                             fdc in/out                                   */
/****************************************************************************/
int
in_fdc(fdcu_t fdcu)
{
	int baseport = fdc_data[fdcu].baseport;
	int i, j = 100000;
	while ((i = inb(baseport+FDSTS) & (NE7_DIO|NE7_RQM))
		!= (NE7_DIO|NE7_RQM) && j-- > 0)
		if (i == NE7_RQM)
			return fdc_err(fdcu, "ready for output in input\n");
	if (j <= 0)
		return fdc_err(fdcu, bootverbose? "input ready timeout\n": 0);
#ifdef	FDC_DEBUG
	i = inb(baseport+FDDATA);
	TRACE1("[FDDATA->0x%x]", (unsigned char)i);
	return(i);
#else	/* !FDC_DEBUG */
	return inb(baseport+FDDATA);
#endif	/* FDC_DEBUG */
}

/*
 * fd_in: Like in_fdc, but allows you to see if it worked.
 */
static int
fd_in(fdcu_t fdcu, int *ptr)
{
	int baseport = fdc_data[fdcu].baseport;
	int i, j = 100000;
	while ((i = inb(baseport+FDSTS) & (NE7_DIO|NE7_RQM))
		!= (NE7_DIO|NE7_RQM) && j-- > 0)
		if (i == NE7_RQM)
			return fdc_err(fdcu, "ready for output in input\n");
	if (j <= 0)
		return fdc_err(fdcu, bootverbose? "input ready timeout\n": 0);
#ifdef	FDC_DEBUG
	i = inb(baseport+FDDATA);
	TRACE1("[FDDATA->0x%x]", (unsigned char)i);
	*ptr = i;
	return 0;
#else	/* !FDC_DEBUG */
	i = inb(baseport+FDDATA);
	if (ptr)
		*ptr = i;
	return 0;
#endif	/* FDC_DEBUG */
}

int
out_fdc(fdcu_t fdcu, int x)
{
	int baseport = fdc_data[fdcu].baseport;
	int i;

	/* Check that the direction bit is set */
	i = 100000;
	while ((inb(baseport+FDSTS) & NE7_DIO) && i-- > 0);
	if (i <= 0) return fdc_err(fdcu, "direction bit not set\n");

	/* Check that the floppy controller is ready for a command */
	i = 100000;
	while ((inb(baseport+FDSTS) & NE7_RQM) == 0 && i-- > 0);
	if (i <= 0)
		return fdc_err(fdcu, bootverbose? "output ready timeout\n": 0);

	/* Send the command and return */
	outb(baseport+FDDATA, x);
	TRACE1("[0x%x->FDDATA]", x);
	return (0);
}

/****************************************************************************/
/*                           fdopen/fdclose                                 */
/****************************************************************************/
int
Fdopen(dev_t dev, int flags, int mode, struct proc *p)
{
 	fdu_t fdu = FDUNIT(minor(dev));
	int type = FDTYPE(minor(dev));
	fdc_p	fdc;

	/* check bounds */
	if (fdu >= NFD)
		return(ENXIO);
	fdc = fd_data[fdu].fdc;
	if ((fdc == NULL) || (fd_data[fdu].type == NO_TYPE))
		return(ENXIO);
	if (type > NUMDENS)
		return(ENXIO);
#ifdef PC98
	if (pc98_fd_check_ready(fdu) == -1)
		return(EIO);
#endif
	if (type == 0)
		type = fd_data[fdu].type;
#ifndef PC98
	else {
		/*
		 * For each type of basic drive, make sure we are trying
		 * to open a type it can do,
		 */
		if (type != fd_data[fdu].type) {
			switch (fd_data[fdu].type) {
			case FD_360:
				return(ENXIO);
			case FD_720:
				if (   type != FD_820
				    && type != FD_800
				   )
					return(ENXIO);
				break;
			case FD_1200:
				switch (type) {
				case FD_1480:
					type = FD_1480in5_25;
					break;
				case FD_1440:
					type = FD_1440in5_25;
					break;
				case FD_820:
					type = FD_820in5_25;
					break;
				case FD_800:
					type = FD_800in5_25;
					break;
				case FD_720:
					type = FD_720in5_25;
					break;
				case FD_360:
					type = FD_360in5_25;
					break;
				default:
					return(ENXIO);
				}
				break;
			case FD_1440:
				if (   type != FD_1720
				    && type != FD_1480
				    && type != FD_1200
				    && type != FD_820
				    && type != FD_800
				    && type != FD_720
				    )
					return(ENXIO);
				break;
			}
		}
	}
#endif
	fd_data[fdu].ft = fd_types + type - 1;
	fd_data[fdu].flags |= FD_OPEN;

	return 0;
}

int
fdclose(dev_t dev, int flags, int mode, struct proc *p)
{
 	fdu_t fdu = FDUNIT(minor(dev));

	fd_data[fdu].flags &= ~FD_OPEN;
	fd_data[fdu].options &= ~FDOPT_NORETRY;

	return(0);
}

static int
fdread(dev_t dev, struct uio *uio, int ioflag)
{
	return (physio(fdstrategy, NULL, dev, 1, minphys, uio));
}

static int
fdwrite(dev_t dev, struct uio *uio, int ioflag)
{
	return (physio(fdstrategy, NULL, dev, 0, minphys, uio));
}


/****************************************************************************/
/*                               fdstrategy                                 */
/****************************************************************************/
void
fdstrategy(struct buf *bp)
{
	unsigned nblocks, blknum, cando;
 	int	s;
 	fdcu_t	fdcu;
 	fdu_t	fdu;
 	fdc_p	fdc;
 	fd_p	fd;
	size_t	fdblk;

 	fdu = FDUNIT(minor(bp->b_dev));
	fd = &fd_data[fdu];
	fdc = fd->fdc;
	fdcu = fdc->fdcu;
#ifdef FDC_YE
	if (fd->type == NO_TYPE) {
		bp->b_error = ENXIO;
		bp->b_flags |= B_ERROR;
		/*
		 * I _refuse_ to use a goto
		 */
		biodone(bp);
		return;
	};
#endif

	fdblk = 128 << (fd->ft->secsize);
	if (!(bp->b_flags & B_FORMAT)) {
		if ((fdu >= NFD) || (bp->b_blkno < 0)) {
			printf(
		"fd%d: fdstrat: bad request blkno = %lu, bcount = %ld\n",
			       fdu, (u_long)bp->b_blkno, bp->b_bcount);
			bp->b_error = EINVAL;
			bp->b_flags |= B_ERROR;
			goto bad;
		}
		if ((bp->b_bcount % fdblk) != 0) {
			bp->b_error = EINVAL;
			bp->b_flags |= B_ERROR;
			goto bad;
		}
	}

	/*
	 * Set up block calculations.
	 */
	if (bp->b_blkno > 20000000) {
		/*
		 * Reject unreasonably high block number, prevent the
		 * multiplication below from overflowing.
		 */
		bp->b_error = EINVAL;
		bp->b_flags |= B_ERROR;
		goto bad;
	}
	blknum = (unsigned) bp->b_blkno * DEV_BSIZE/fdblk;
 	nblocks = fd->ft->size;
	bp->b_resid = 0;
#ifdef PC98
#define B_XXX2 0x8000000
		if (bp->b_flags & B_XXX2) {
			blknum *= 2;
			bp->b_blkno *= 2;
			bp->b_flags &= ~B_XXX2;
		}
#endif
	if (blknum + (bp->b_bcount / fdblk) > nblocks) {
		if (blknum <= nblocks) {
			cando = (nblocks - blknum) * fdblk;
			bp->b_resid = bp->b_bcount - cando;
			if (cando == 0)
				goto bad;	/* not actually bad but EOF */
		} else {
			bp->b_error = EINVAL;
			bp->b_flags |= B_ERROR;
			goto bad;
		}
	}
 	bp->b_pblkno = bp->b_blkno;
	s = splbio();
	bufqdisksort(&fdc->head, bp);
	untimeout(fd_turnoff, (caddr_t)fdu, fd->toffhandle); /* a good idea */

	/* Tell devstat we are starting on the transaction */
	devstat_start_transaction(&fd->device_stats);

	fdstart(fdcu);
	splx(s);
	return;

bad:
	biodone(bp);
}

/***************************************************************\
*				fdstart				*
* We have just queued something.. if the controller is not busy	*
* then simulate the case where it has just finished a command	*
* So that it (the interrupt routine) looks on the queue for more*
* work to do and picks up what we just added.			*
* If the controller is already busy, we need do nothing, as it	*
* will pick up our work when the present work completes		*
\***************************************************************/
static void
fdstart(fdcu_t fdcu)
{
	int s;

	s = splbio();
	if(fdc_data[fdcu].state == DEVIDLE)
	{
		fdintr(fdcu);
	}
	splx(s);
}

static void
fd_iotimeout(void *arg1)
{
 	fdc_p fdc;
	fdcu_t fdcu;
	int s;

	fdcu = (fdcu_t)arg1;
	fdc = fdc_data + fdcu;
	TRACE1("fd%d[fd_iotimeout()]", fdc->fdu);

	/*
	 * Due to IBM's brain-dead design, the FDC has a faked ready
	 * signal, hardwired to ready == true. Thus, any command
	 * issued if there's no diskette in the drive will _never_
	 * complete, and must be aborted by resetting the FDC.
	 * Many thanks, Big Blue!
	 * The FDC must not be reset directly, since that would
	 * interfere with the state machine.  Instead, pretend that
	 * the command completed but was invalid.  The state machine
	 * will reset the FDC and retry once.
	 */
	s = splbio();
	fdc->status[0] = NE7_ST0_IC_IV;
	fdc->flags &= ~FDC_STAT_VALID;
	fdc->state = IOTIMEDOUT;
	fdintr(fdcu);
	splx(s);
}

/* just ensure it has the right spl */
static void
fd_pseudointr(void *arg1)
{
	fdcu_t fdcu = (fdcu_t)arg1;
	int	s;

	s = splbio();
	fdintr(fdcu);
	splx(s);
}

/***********************************************************************\
*                                 fdintr				*
* keep calling the state machine until it returns a 0			*
* ALWAYS called at SPLBIO 						*
\***********************************************************************/
static void
fdintr(fdcu_t fdcu)
{
	fdc_p fdc = fdc_data + fdcu;
		while(fdstate(fdcu, fdc))
			;
}

#ifdef FDC_YE
/*
 * magic pseudo-DMA initialization for YE FDC. Sets count and
 * direction
 */
#define SET_BCDR(wr,cnt,port) outb(port,(((cnt)-1) & 0xff)); \
	outb(port+1,((wr ? 0x80 : 0) | ((((cnt)-1) >> 8) & 0x7f)))

/*
 * fdcpio(): perform programmed IO read/write for YE PCMCIA floppy
 */
static int fdcpio(fdcu_t fdcu, long flags, caddr_t addr, u_int count)
{
	u_char *cptr = (u_char *)addr;
	fdc_p fdc = &fdc_data[fdcu];
	int io = fdc->baseport;

	if (flags & B_READ) {
		if (fdc->state != PIOREAD) {
			fdc->state = PIOREAD;
			return(0);
		};
		SET_BCDR(0,count,io);
		insb(io+FDC_YE_DATAPORT,cptr,count);
	} else {
		outsb(io+FDC_YE_DATAPORT,cptr,count);
		SET_BCDR(0,count,io);
	};
	return(1);
}
#endif /* FDC_YE */

/***********************************************************************\
* The controller state machine.						*
* if it returns a non zero value, it should be called again immediatly	*
\***********************************************************************/
static int
fdstate(fdcu_t fdcu, fdc_p fdc)
{
	int read, format, head, i, sec = 0, sectrac, st0, cyl, st3;
	unsigned blknum = 0, b_cylinder = 0;
	fdu_t fdu = fdc->fdu;
	fd_p fd;
	register struct buf *bp;
	struct fd_formb *finfo = NULL;
	size_t fdblk;

	bp = fdc->bp;
	if (bp == NULL) {
		bp = bufq_first(&fdc->head);
		if (bp != NULL) {
			bufq_remove(&fdc->head, bp);
			fdc->bp = bp;
		}
	}
	if (bp == NULL) {
		/***********************************************\
		* nothing left for this controller to do	*
		* Force into the IDLE state,			*
		\***********************************************/
		fdc->state = DEVIDLE;
		if(fdc->fd)
		{
			printf("fd%d: unexpected valid fd pointer\n",
			       fdc->fdu);
			fdc->fd = (fd_p) 0;
			fdc->fdu = -1;
		}
		TRACE1("[fdc%d IDLE]", fdcu);
 		return(0);
	}
	fdu = FDUNIT(minor(bp->b_dev));
	fd = fd_data + fdu;
	fdblk = 128 << fd->ft->secsize;
	if (fdc->fd && (fd != fdc->fd))
	{
		printf("fd%d: confused fd pointers\n", fdu);
	}
	read = bp->b_flags & B_READ;
	format = bp->b_flags & B_FORMAT;
	if(format) {
		finfo = (struct fd_formb *)bp->b_data;
		fd->skip = (char *)&(finfo->fd_formb_cylno(0))
			- (char *)finfo;
	}
	if (fdc->state == DOSEEK || fdc->state == SEEKCOMPLETE) {
		blknum = (unsigned) bp->b_pblkno * DEV_BSIZE/fdblk +
			fd->skip/fdblk;
		b_cylinder = blknum / (fd->ft->sectrac * fd->ft->heads);
	}
	TRACE1("fd%d", fdu);
	TRACE1("[%s]", fdstates[fdc->state]);
	TRACE1("(0x%x)", fd->flags);
	untimeout(fd_turnoff, (caddr_t)fdu, fd->toffhandle);
	fd->toffhandle = timeout(fd_turnoff, (caddr_t)fdu, 4 * hz);
	switch (fdc->state)
	{
	case DEVIDLE:
	case FINDWORK:	/* we have found new work */
		fdc->retry = 0;
		fd->skip = 0;
		fdc->fd = fd;
		fdc->fdu = fdu;
#ifdef PC98
		pc98_trans = fd->ft->trans;
		if (pc98_trans_prev != pc98_trans) {
			int i;
			set_density(fdcu, fdu);
			for (i = 0; i < 10; i++) {
				outb(0x5f, 0);
				outb(0x5f, 0);
			}
			pc98_trans_prev = pc98_trans;
		}
		if (pc98_trans != fd->pc98_trans) {
			if (pc98_trans != 1 &&
					(PC98_SYSTEM_PARAMETER(0x5ae) >> fdu) & 0x01) {
				outb(0x4be, (fdu << 5) | 0x10 | (pc98_trans >> 1));
				outb(0x5f, 0);
				outb(0x5f, 0);
			}
			fd->pc98_trans = pc98_trans;
		}
#else
		outb(fdc->baseport+FDCTL, fd->ft->trans);
#endif
		TRACE1("[0x%x->FDCTL]", fd->ft->trans);
		/*******************************************************\
		* If the next drive has a motor startup pending, then	*
		* it will start up in its own good time		*
		\*******************************************************/
		if(fd->flags & FD_MOTOR_WAIT)
		{
			fdc->state = MOTORWAIT;
			return(0); /* come back later */
		}
		/*******************************************************\
		* Maybe if it's not starting, it SHOULD be starting	*
		\*******************************************************/
#ifdef EPSON_NRDISK
		if (fdu != nrdu) {
			if (!(fd->flags & FD_MOTOR))
			{
				fdc->state = MOTORWAIT;
				fd_turnon(fdu);
				return(0);
			}
			else	/* at least make sure we are selected */
			{
				set_motor(fdcu, fd->fdsu, TURNON);
			}
		}
#else /* !EPSON_NRDISK */
		if (!(fd->flags & FD_MOTOR))
		{
			fdc->state = MOTORWAIT;
			fd_turnon(fdu);
			return(0);
		}
		else	/* at least make sure we are selected */
		{
			set_motor(fdcu, fd->fdsu, TURNON);
		}
#endif
		if (fdc->flags & FDC_NEEDS_RESET) {
			fdc->state = RESETCTLR;
			fdc->flags &= ~FDC_NEEDS_RESET;
		} else
			fdc->state = DOSEEK;
		break;
	case DOSEEK:
		if (b_cylinder == (unsigned)fd->track)
		{
			fdc->state = SEEKCOMPLETE;
			break;
		}
#ifdef PC98
		pc98_fd_check_ready(fdu);
#endif
		if (fd_cmd(fdcu, 3, NE7CMD_SEEK,
			   fd->fdsu, b_cylinder * fd->ft->steptrac,
			   0))
		{
			/*
			 * seek command not accepted, looks like
			 * the FDC went off to the Saints...
			 */
			fdc->retry = 6;	/* try a reset */
			return(retrier(fdcu));
		}
		fd->track = FD_NO_TRACK;
		fdc->state = SEEKWAIT;
		return(0);	/* will return later */
	case SEEKWAIT:
		/* allow heads to settle */
		timeout(fd_pseudointr, (caddr_t)fdcu, hz / 16);
		fdc->state = SEEKCOMPLETE;
		return(0);	/* will return later */
	case SEEKCOMPLETE : /* SEEK DONE, START DMA */
		/* Make sure seek really happened*/
		if(fd->track == FD_NO_TRACK)
		{
			int descyl = b_cylinder * fd->ft->steptrac;
			do {
				/*
				 * This might be a "ready changed" interrupt,
				 * which cannot really happen since the
				 * RDY pin is hardwired to + 5 volts.  This
				 * generally indicates a "bouncing" intr
				 * line, so do one of the following:
				 *
				 * When running on an enhanced FDC that is
				 * known to not go stuck after responding
				 * with INVALID, fetch all interrupt states
				 * until seeing either an INVALID or a
				 * real interrupt condition.
				 *
				 * When running on a dumb old NE765, give
				 * up immediately.  The controller will
				 * provide up to four dummy RC interrupt
				 * conditions right after reset (for the
				 * corresponding four drives), so this is
				 * our only chance to get notice that it
				 * was not the FDC that caused the interrupt.
				 */
				if (fd_sense_int(fdc, &st0, &cyl)
				    == FD_NOT_VALID)
					return 0;
				if(fdc->fdct == FDC_NE765
				   && (st0 & NE7_ST0_IC) == NE7_ST0_IC_RC)
					return 0; /* hope for a real intr */
			} while ((st0 & NE7_ST0_IC) == NE7_ST0_IC_RC);

			if (0 == descyl)
			{
				int failed = 0;
				/*
				 * seek to cyl 0 requested; make sure we are
				 * really there
				 */
				if (fd_sense_drive_status(fdc, &st3))
					failed = 1;
#ifdef EPSON_NRDISK
				if (fdu == nrdu) st3 = NE7_ST3_T0;
#endif /* EPSON_NRDISK */
				if ((st3 & NE7_ST3_T0) == 0) {
					printf(
		"fd%d: Seek to cyl 0, but not really there (ST3 = %b)\n",
					       fdu, st3, NE7_ST3BITS);
					failed = 1;
				}

				if (failed)
				{
					if(fdc->retry < 3)
						fdc->retry = 3;
					return(retrier(fdcu));
				}
			}
#ifdef EPSON_NRDISK
			if (fdu == nrdu) cyl = descyl;
#endif

			if (cyl != descyl)
			{
				printf(
		"fd%d: Seek to cyl %d failed; am at cyl %d (ST0 = 0x%x)\n",
				       fdu, descyl, cyl, st0);
				if (fdc->retry < 3)
					fdc->retry = 3;
				return(retrier(fdcu));
			}
		}

		fd->track = b_cylinder;
#ifdef EPSON_NRDISK
		if (fdu != nrdu) {
#endif /* EPSON_NRDISK */
#ifdef FDC_YE
		if (!(fdc->flags & FDC_PCMCIA))
#endif
			isa_dmastart(bp->b_flags, bp->b_data+fd->skip,
				format ? bp->b_bcount : fdblk, fdc->dmachan);
		sectrac = fd->ft->sectrac;
		sec = blknum %  (sectrac * fd->ft->heads);
		head = sec / sectrac;
		sec = sec % sectrac + 1;
		fd->hddrv = ((head&1)<<2)+fdu;
		if(format || !read)
		{
			/* make sure the drive is writable */
			if(fd_sense_drive_status(fdc, &st3) != 0)
			{
				/* stuck controller? */
				isa_dmadone(bp->b_flags, bp->b_data + fd->skip,
					    format ? bp->b_bcount : fdblk,
					    fdc->dmachan);
				fdc->retry = 6;	/* reset the beast */
				return(retrier(fdcu));
			}
			if(st3 & NE7_ST3_WP)
			{
				/*
				 * XXX YES! this is ugly.
				 * in order to force the current operation
				 * to fail, we will have to fake an FDC
				 * error - all error handling is done
				 * by the retrier()
				 */
				fdc->status[0] = NE7_ST0_IC_AT;
				fdc->status[1] = NE7_ST1_NW;
				fdc->status[2] = 0;
				fdc->status[3] = fd->track;
				fdc->status[4] = head;
				fdc->status[5] = sec;
				fdc->retry = 8;	/* break out immediately */
				fdc->state = IOTIMEDOUT; /* not really... */
				return (1);
			}
		}

		if(format)
		{
#ifdef FDC_YE
			if (fdc->flags & FDC_PCMCIA)
				(void)fdcpio(fdcu,bp->b_flags,
					bp->b_data+fd->skip,
					bp->b_bcount);
#endif
			/* formatting */
			if(fd_cmd(fdcu, 6,
				  NE7CMD_FORMAT,
				  head << 2 | fdu,
				  finfo->fd_formb_secshift,
				  finfo->fd_formb_nsecs,
				  finfo->fd_formb_gaplen,
				  finfo->fd_formb_fillbyte,
				  0))
			{
				/* controller fell over */
				isa_dmadone(bp->b_flags, bp->b_data + fd->skip,
					    format ? bp->b_bcount : fdblk,
					    fdc->dmachan);
				fdc->retry = 6;
				return(retrier(fdcu));
			}
		}
		else
		{
#ifdef FDC_YE
			if (fdc->flags & FDC_PCMCIA) {
				/*
				 * this seems to be necessary even when
				 * reading data
				 */
				SET_BCDR(1,fdblk,fdc->baseport);

				/*
				 * perform the write pseudo-DMA before
				 * the WRITE command is sent
				 */
				if (!read)
					(void)fdcpio(fdcu,bp->b_flags,
					    bp->b_data+fd->skip,
					    fdblk);
			}
#endif
			if (fd_cmd(fdcu, 9,
				   (read ? NE7CMD_READ : NE7CMD_WRITE),
				   head << 2 | fdu,  /* head & unit */
				   fd->track,        /* track */
				   head,
				   sec,              /* sector + 1 */
				   fd->ft->secsize,  /* sector size */
				   sectrac,          /* sectors/track */
				   fd->ft->gap,      /* gap size */
				   fd->ft->datalen,  /* data length */
				   0))
			{
				/* the beast is sleeping again */
				isa_dmadone(bp->b_flags, bp->b_data + fd->skip,
					    format ? bp->b_bcount : fdblk,
					    fdc->dmachan);
				fdc->retry = 6;
				return(retrier(fdcu));
			}
		}
#ifdef FDC_YE
		if (fdc->flags & FDC_PCMCIA)
			/*
			 * if this is a read, then simply await interrupt
			 * before performing PIO
			 */
			if (read && !fdcpio(fdcu,bp->b_flags,
			    bp->b_data+fd->skip,fdblk)) {
				fd->tohandle = timeout(fd_iotimeout, 
					(caddr_t)fdcu, hz);
				return(0);      /* will return later */
			};

		/*
		 * write (or format) operation will fall through and
		 * await completion interrupt
		 */
#endif
		fdc->state = IOCOMPLETE;
		fd->tohandle = timeout(fd_iotimeout, (caddr_t)fdcu, hz);
		return(0);	/* will return later */
#ifdef EPSON_NRDISK
		}
		else {
			nrdblkn = (nrd_t)((unsigned long)bp->b_blkno*DEV_BSIZE/fdblk
				+ fd->skip/fdblk);
			nrd_LED_on();
			nrd_addrset(fdblk * nrdblkn);
			while (!nrd_check_ready()) DELAY(1);
			if (read) epson_insw(P_NRD_DATA,
					bp->b_data + fd->skip,
					fdblk / sizeof(short));
			else epson_outsw(P_NRD_DATA,
				bp->b_data + fd->skip,
				(format ? bp->b_bcount : fdblk)
					/ sizeof(short));

			blknum = (unsigned long)bp->b_blkno*DEV_BSIZE/fdblk
				+ fd->skip/fdblk;
			sectrac = fd->ft->sectrac;
			sec = blknum %  (sectrac * fd->ft->heads);
			head = sec / sectrac;
			sec = sec % sectrac + 1;
			fd->hddrv = ((head&1)<<2)+fdu;

			if (nrdsec++ >= nrd_sec())
				nrdaddr = (nrd_t)(fd->track * 8	+ head * 4);
			nrdsec = sec;
			fdc->state = IOCOMPLETE;
		}
#endif
#ifdef FDC_YE
	case PIOREAD:
		/* 
		 * actually perform the PIO read.  The IOCOMPLETE case
		 * removes the timeout for us.  
		 */
		(void)fdcpio(fdcu,bp->b_flags,bp->b_data+fd->skip,fdblk);
		fdc->state = IOCOMPLETE;
		/* FALLTHROUGH */
#endif
	case IOCOMPLETE: /* IO DONE, post-analyze */
#ifdef EPSON_NRDISK
		if (fdu != nrdu) 
			untimeout(fd_iotimeout, (caddr_t)fdcu, fd->tohandle);
#else
		untimeout(fd_iotimeout, (caddr_t)fdcu, fd->tohandle);
#endif

		if (fd_read_status(fdc, fd->fdsu))
		{
			isa_dmadone(bp->b_flags, bp->b_data + fd->skip,
				    format ? bp->b_bcount : fdblk,
				    fdc->dmachan);
			if (fdc->retry < 6)
				fdc->retry = 6;	/* force a reset */
			return retrier(fdcu);
  		}

		fdc->state = IOTIMEDOUT;

		/* FALLTHROUGH */

	case IOTIMEDOUT:
#ifdef EPSON_NRDISK
		if (fdu != nrdu) {
#endif /* EPSON_NRDISK */
#ifdef FDC_YE
		if (!(fdc->flags & FDC_PCMCIA))
#endif
			isa_dmadone(bp->b_flags, bp->b_data + fd->skip,
				format ? bp->b_bcount : fdblk, fdc->dmachan);
#ifdef EPSON_NRDISK
		}
		else nrd_LED_off();
#endif /* EPSON_NRDISK */
		if (fdc->status[0] & NE7_ST0_IC)
		{
                        if ((fdc->status[0] & NE7_ST0_IC) == NE7_ST0_IC_AT
			    && fdc->status[1] & NE7_ST1_OR) {
                                /*
				 * DMA overrun. Someone hogged the bus
				 * and didn't release it in time for the
				 * next FDC transfer.
				 * Just restart it, don't increment retry
				 * count. (vak)
                                 */
                                fdc->state = SEEKCOMPLETE;
                                return (1);
                        }
			else if((fdc->status[0] & NE7_ST0_IC) == NE7_ST0_IC_IV
				&& fdc->retry < 6)
				fdc->retry = 6;	/* force a reset */
			else if((fdc->status[0] & NE7_ST0_IC) == NE7_ST0_IC_AT
				&& fdc->status[2] & NE7_ST2_WC
				&& fdc->retry < 3)
				fdc->retry = 3;	/* force recalibrate */
			return(retrier(fdcu));
		}
		/* All OK */
		fd->skip += fdblk;
		if (!format && fd->skip < bp->b_bcount - bp->b_resid)
		{
			/* set up next transfer */
			fdc->state = DOSEEK;
		}
		else
		{
			/* ALL DONE */
			fd->skip = 0;
			fdc->bp = NULL;
			/* Tell devstat we have finished with the transaction */
			devstat_end_transaction(&fd->device_stats,
						bp->b_bcount - bp->b_resid,
						DEVSTAT_TAG_NONE,
						(bp->b_flags & B_READ) ?
						DEVSTAT_READ : DEVSTAT_WRITE);
			biodone(bp);
			fdc->fd = (fd_p) 0;
			fdc->fdu = -1;
			fdc->state = FINDWORK;
		}
		return(1);
	case RESETCTLR:
		fdc_reset(fdc);
		fdc->retry++;
		fdc->state = RESETCOMPLETE;
		return (0);
	case RESETCOMPLETE:
		/*
		 * Discard all the results from the reset so that they
		 * can't cause an unexpected interrupt later.
		 */
		for (i = 0; i < 4; i++)
			(void)fd_sense_int(fdc, &st0, &cyl);
		fdc->state = STARTRECAL;
		/* Fall through. */
	case STARTRECAL:
#ifdef PC98
		pc98_fd_check_ready(fdu);
#endif
		if(fd_cmd(fdcu,
			  2, NE7CMD_RECAL, fdu,
			  0)) /* Recalibrate Function */
		{
			/* arrgl */
			fdc->retry = 6;
			return(retrier(fdcu));
		}
		fdc->state = RECALWAIT;
		return(0);	/* will return later */
	case RECALWAIT:
		/* allow heads to settle */
		timeout(fd_pseudointr, (caddr_t)fdcu, hz / 8);
		fdc->state = RECALCOMPLETE;
		return(0);	/* will return later */
	case RECALCOMPLETE:
		do {
			/*
			 * See SEEKCOMPLETE for a comment on this:
			 */
			if (fd_sense_int(fdc, &st0, &cyl) == FD_NOT_VALID)
				return 0;
			if(fdc->fdct == FDC_NE765
			   && (st0 & NE7_ST0_IC) == NE7_ST0_IC_RC)
				return 0; /* hope for a real intr */
		} while ((st0 & NE7_ST0_IC) == NE7_ST0_IC_RC);
#ifdef EPSON_NRDISK
		if (fdu == nrdu) {
			st0 = NE7_ST0_IC_NT;
			cyl = 0;
		}
#endif
		if ((st0 & NE7_ST0_IC) != NE7_ST0_IC_NT || cyl != 0)
		{
			if(fdc->retry > 3)
				/*
				 * a recalibrate from beyond cylinder 77
				 * will "fail" due to the FDC limitations;
				 * since people used to complain much about
				 * the failure message, try not logging
				 * this one if it seems to be the first
				 * time in a line
				 */
				printf("fd%d: recal failed ST0 %b cyl %d\n",
				       fdu, st0, NE7_ST0BITS, cyl);
			if(fdc->retry < 3) fdc->retry = 3;
			return(retrier(fdcu));
		}
		fd->track = 0;
		/* Seek (probably) necessary */
		fdc->state = DOSEEK;
		return(1);	/* will return immediatly */
	case MOTORWAIT:
		if(fd->flags & FD_MOTOR_WAIT)
		{
			return(0); /* time's not up yet */
		}
		if (fdc->flags & FDC_NEEDS_RESET) {
			fdc->state = RESETCTLR;
			fdc->flags &= ~FDC_NEEDS_RESET;
		} else {
			/*
			 * If all motors were off, then the controller was
			 * reset, so it has lost track of the current
			 * cylinder.  Recalibrate to handle this case.
			 * But first, discard the results of the reset.
			 */
			fdc->state = RESETCOMPLETE;
		}
		return(1);	/* will return immediatly */
	default:
		printf("fdc%d: Unexpected FD int->", fdcu);
		if (fd_read_status(fdc, fd->fdsu) == 0)
			printf("FDC status :%x %x %x %x %x %x %x   ",
			       fdc->status[0],
			       fdc->status[1],
			       fdc->status[2],
			       fdc->status[3],
			       fdc->status[4],
			       fdc->status[5],
			       fdc->status[6] );
		else
			printf("No status available   ");
		if (fd_sense_int(fdc, &st0, &cyl) != 0)
		{
			printf("[controller is dead now]\n");
			return(0);
		}
		printf("ST0 = %x, PCN = %x\n", st0, cyl);
		return(0);
	}
	/*XXX confusing: some branches return immediately, others end up here*/
	return(1); /* Come back immediatly to new state */
}

static int
retrier(fdcu)
	fdcu_t fdcu;
{
	fdc_p fdc = fdc_data + fdcu;
	register struct buf *bp;

	bp = fdc->bp;

	if(fd_data[FDUNIT(minor(bp->b_dev))].options & FDOPT_NORETRY)
		goto fail;
	switch(fdc->retry)
	{
	case 0: case 1: case 2:
		fdc->state = SEEKCOMPLETE;
		break;
	case 3: case 4: case 5:
		fdc->state = STARTRECAL;
		break;
	case 6:
		fdc->state = RESETCTLR;
		break;
	case 7:
		break;
	default:
	fail:
		{
			dev_t sav_b_dev = bp->b_dev;
			/* Trick diskerr */
			bp->b_dev = makedev(major(bp->b_dev),
				    (FDUNIT(minor(bp->b_dev))<<3)|RAW_PART);
			diskerr(bp, "fd", "hard error", LOG_PRINTF,
				fdc->fd->skip / DEV_BSIZE,
				(struct disklabel *)NULL);
			bp->b_dev = sav_b_dev;
			if (fdc->flags & FDC_STAT_VALID)
			{
				printf(
			" (ST0 %b ST1 %b ST2 %b cyl %u hd %u sec %u)\n",
				       fdc->status[0], NE7_ST0BITS,
				       fdc->status[1], NE7_ST1BITS,
				       fdc->status[2], NE7_ST2BITS,
				       fdc->status[3], fdc->status[4],
				       fdc->status[5]);
			}
			else
				printf(" (No status)\n");
		}
		bp->b_flags |= B_ERROR;
		bp->b_error = EIO;
		bp->b_resid += bp->b_bcount - fdc->fd->skip;
		fdc->bp = NULL;
	
		/* Tell devstat we have finished with the transaction */
		devstat_end_transaction(&fdc->fd->device_stats,
					bp->b_bcount - bp->b_resid,
					DEVSTAT_TAG_NONE,
					(bp->b_flags & B_READ) ? DEVSTAT_READ :
								 DEVSTAT_WRITE);
		fdc->fd->skip = 0;
		biodone(bp);
		fdc->state = FINDWORK;
		fdc->flags |= FDC_NEEDS_RESET;
		fdc->fd = (fd_p) 0;
		fdc->fdu = -1;
		return(1);
	}
	fdc->retry++;
	return(1);
}

static int
fdformat(dev, finfo, p)
	dev_t dev;
	struct fd_formb *finfo;
	struct proc *p;
{
 	fdu_t	fdu;
 	fd_p	fd;

	struct buf *bp;
	int rv = 0, s;
	size_t fdblk;

 	fdu	= FDUNIT(minor(dev));
	fd	= &fd_data[fdu];
	fdblk = 128 << fd->ft->secsize;

	/* set up a buffer header for fdstrategy() */
	bp = (struct buf *)malloc(sizeof(struct buf), M_TEMP, M_NOWAIT);
	if(bp == 0)
		return ENOBUFS;
	/*
	 * keep the process from being swapped
	 */
	p->p_flag |= P_PHYSIO;
	bzero((void *)bp, sizeof(struct buf));
	bp->b_flags = B_BUSY | B_PHYS | B_FORMAT;
	bp->b_proc = p;

	/*
	 * calculate a fake blkno, so fdstrategy() would initiate a
	 * seek to the requested cylinder
	 */
	bp->b_blkno = (finfo->cyl * (fd->ft->sectrac * fd->ft->heads)
		+ finfo->head * fd->ft->sectrac) * fdblk / DEV_BSIZE;

	bp->b_bcount = sizeof(struct fd_idfield_data) * finfo->fd_formb_nsecs;
	bp->b_data = (caddr_t)finfo;

	/* now do the format */
	bp->b_dev = dev;
	fdstrategy(bp);

	/* ...and wait for it to complete */
	s = splbio();
	while(!(bp->b_flags & B_DONE))
	{
		rv = tsleep((caddr_t)bp, PRIBIO, "fdform", 20 * hz);
		if(rv == EWOULDBLOCK)
			break;
	}
	splx(s);

	if(rv == EWOULDBLOCK) {
		/* timed out */
		rv = EIO;
		biodone(bp);
	}
	if(bp->b_flags & B_ERROR)
		rv = bp->b_error;
	/*
	 * allow the process to be swapped
	 */
	p->p_flag &= ~P_PHYSIO;
	free(bp, M_TEMP);
	return rv;
}

/*
 * TODO: don't allocate buffer on stack.
 */

static int
fdioctl(dev, cmd, addr, flag, p)
	dev_t dev;
	u_long cmd;
	caddr_t addr;
	int flag;
	struct proc *p;
{
 	fdu_t	fdu = FDUNIT(minor(dev));
 	fd_p	fd = &fd_data[fdu];
	size_t fdblk;

	struct fd_type *fdt;
	struct disklabel *dl;
	char buffer[DEV_BSIZE];
	int error = 0;

	fdblk = 128 << fd->ft->secsize;

#ifdef PC98
	    pc98_fd_check_ready(fdu);
#endif	
	switch (cmd)
	{
	case DIOCGDINFO:
		bzero(buffer, sizeof (buffer));
		dl = (struct disklabel *)buffer;
		dl->d_secsize = fdblk;
		fdt = fd_data[FDUNIT(minor(dev))].ft;
		dl->d_secpercyl = fdt->size / fdt->tracks;
		dl->d_type = DTYPE_FLOPPY;

		if (readdisklabel(dkmodpart(dev, RAW_PART), fdstrategy, dl)
		    == NULL)
			error = 0;
		else
			error = EINVAL;

		*(struct disklabel *)addr = *dl;
		break;

	case DIOCSDINFO:
		if ((flag & FWRITE) == 0)
			error = EBADF;
		break;

	case DIOCWLABEL:
		if ((flag & FWRITE) == 0)
			error = EBADF;
		break;

	case DIOCWDINFO:
		if ((flag & FWRITE) == 0)
		{
			error = EBADF;
			break;
		}

		dl = (struct disklabel *)addr;

		if ((error = setdisklabel((struct disklabel *)buffer, dl,
					  (u_long)0)) != 0)
			break;

		error = writedisklabel(dev, fdstrategy,
				       (struct disklabel *)buffer);
		break;
	case FD_FORM:
		if((flag & FWRITE) == 0)
			error = EBADF;	/* must be opened for writing */
		else if(((struct fd_formb *)addr)->format_version !=
			FD_FORMAT_VERSION)
			error = EINVAL;	/* wrong version of formatting prog */
		else
			error = fdformat(dev, (struct fd_formb *)addr, p);
		break;

	case FD_GTYPE:                  /* get drive type */
		*(struct fd_type *)addr = *fd->ft;
		break;

	case FD_STYPE:                  /* set drive type */
		/* this is considered harmful; only allow for superuser */
		if(suser(p->p_ucred, &p->p_acflag) != 0)
			return EPERM;
		*fd->ft = *(struct fd_type *)addr;
		break;

	case FD_GOPTS:			/* get drive options */
		*(int *)addr = fd->options;
		break;

	case FD_SOPTS:			/* set drive options */
		fd->options = *(int *)addr;
		break;

	default:
		error = ENOTTY;
		break;
	}
	return (error);
}


static fd_devsw_installed = 0;

static void 	fd_drvinit(void *notused )
{

	if( ! fd_devsw_installed ) {
		cdevsw_add_generic(BDEV_MAJOR,CDEV_MAJOR, &fd_cdevsw);
		fd_devsw_installed = 1;
	}
}

SYSINIT(fddev,SI_SUB_DRIVERS,SI_ORDER_MIDDLE+CDEV_MAJOR,fd_drvinit,NULL)


#endif

/*
 * Hello emacs, these are the
 * Local Variables:
 *  c-indent-level:               8
 *  c-continued-statement-offset: 8
 *  c-continued-brace-offset:     0
 *  c-brace-offset:              -8
 *  c-brace-imaginary-offset:     0
 *  c-argdecl-indent:             8
 *  c-label-offset:              -8
 *  c++-hanging-braces:           1
 *  c++-access-specifier-offset: -8
 *  c++-empty-arglist-indent:     8
 *  c++-friend-offset:            0
 * End:
 */
