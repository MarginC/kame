/*	$OpenBSD: conf.c,v 1.22 1998/09/25 09:20:53 todd Exp $	*/
/*	$NetBSD: conf.c,v 1.39 1997/05/12 08:17:53 thorpej Exp $	*/

/*-
 * Copyright (c) 1991 The Regents of the University of California.
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
 *      @(#)conf.c	7.9 (Berkeley) 5/28/91
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/buf.h>
#include <sys/ioctl.h>
#include <sys/tty.h>
#include <sys/conf.h>
#include <sys/vnode.h>

#include "ct.h"
bdev_decl(ct);
#include "mt.h"
bdev_decl(mt);
#include "hd.h"
bdev_decl(hd);
bdev_decl(sw);
#include "sd.h"
bdev_decl(sd);
#include "ccd.h"
bdev_decl(ccd);
#include "vnd.h"
bdev_decl(vnd);
#include "st.h"
bdev_decl(st);
#include "rd.h"
bdev_decl(rd);

struct bdevsw	bdevsw[] =
{
	bdev_tape_init(NCT,ct),		/* 0: cs80 cartridge tape */
	bdev_tape_init(NMT,mt),		/* 1: magnetic reel tape */
	bdev_disk_init(NHD,hd),		/* 2: HPIB disk */
	bdev_swap_init(1,sw),		/* 3: swap pseudo-device */
	bdev_disk_init(NSD,sd),		/* 4: SCSI disk */
	bdev_disk_init(NCCD,ccd),	/* 5: concatenated disk driver */
	bdev_disk_init(NVND,vnd),	/* 6: vnode disk driver */
	bdev_tape_init(NST,st),		/* 7: SCSI tape */
	bdev_disk_init(NRD,rd),		/* 8: RAM disk */
	bdev_lkm_dummy(),		/* 9 */
	bdev_lkm_dummy(),		/* 10 */
	bdev_lkm_dummy(),		/* 11 */
	bdev_lkm_dummy(),		/* 12 */
	bdev_lkm_dummy(),		/* 13 */
	bdev_lkm_dummy(),		/* 14 */
};
int	nblkdev = sizeof(bdevsw) / sizeof(bdevsw[0]);

/* open, close, ioctl, select, mmap -- XXX should be a map device */
#define	cdev_grf_init(c,n) { \
	dev_init(c,n,open), dev_init(c,n,close), (dev_type_read((*))) nullop, \
	(dev_type_write((*))) nullop, dev_init(c,n,ioctl), \
	(dev_type_stop((*))) enodev, 0, dev_init(c,n,select), \
	dev_init(c,n,mmap) }

/* open, close, read, write, ioctl -- XXX should be a generic device */
#define	cdev_ppi_init(c,n) { \
	dev_init(c,n,open), dev_init(c,n,close), dev_init(c,n,read), \
	dev_init(c,n,write), dev_init(c,n,ioctl), (dev_type_stop((*))) nullop, \
	0, (dev_type_select((*))) enodev, (dev_type_mmap((*))) enodev }

/* open, close, read, ioctl, select, mmap -- XXX should be a map device */
#define	cdev_hil_init(c,n) { \
	dev_init(c,n,open), dev_init(c,n,close), dev_init(c,n,read), \
	(dev_type_write((*))) nullop, dev_init(c,n,ioctl), \
	(dev_type_stop((*))) enodev, 0, dev_init(c,n,select), \
	dev_init(c,n,mmap) }

cdev_decl(cn);
cdev_decl(ctty);
#define	mmread	mmrw
#define	mmwrite	mmrw
cdev_decl(mm);
cdev_decl(sw);
#include "pty.h"
#define	ptstty		ptytty
#define	ptsioctl	ptyioctl
cdev_decl(pts);
#define	ptctty		ptytty
#define	ptcioctl	ptyioctl
cdev_decl(ptc);
cdev_decl(log);
cdev_decl(ct);
cdev_decl(sd);
cdev_decl(hd);
#include "grf.h"
cdev_decl(grf);
#include "ppi.h"
cdev_decl(ppi);
#include "dca.h"
cdev_decl(dca);
#include "apci.h"
cdev_decl(apci);
#include "ite.h"
cdev_decl(ite);
/* XXX shouldn't this be optional? */
cdev_decl(hil);
#include "dcm.h"
cdev_decl(dcm);
cdev_decl(mt);
cdev_decl(ccd);
cdev_decl(vnd);
cdev_decl(st);
cdev_decl(fd);
dev_decl(filedesc,open);
#include "bpfilter.h"
cdev_decl(bpf);
#include "tun.h"
cdev_decl(tun);
cdev_decl(random);
cdev_decl(rd);
#include "ksyms.h"
cdev_decl(ksyms);   
#ifdef XFS
#include <xfs/nxfs.h>
cdev_decl(xfs_dev);
#endif

#ifdef IPFILTER
#define NIPF 1
#else
#define NIPF 0
#endif

struct cdevsw	cdevsw[] =
{
	cdev_cn_init(1,cn),		/* 0: virtual console */
	cdev_ctty_init(1,ctty),		/* 1: controlling terminal */
	cdev_mm_init(1,mm),		/* 2: /dev/{null,mem,kmem,...} */
	cdev_swap_init(1,sw),		/* 3: /dev/drum (swap pseudo-device) */
	cdev_tty_init(NPTY,pts),	/* 4: pseudo-tty slave */
	cdev_ptc_init(NPTY,ptc),	/* 5: pseudo-tty master */
	cdev_log_init(1,log),		/* 6: /dev/klog */
	cdev_tape_init(NCT,ct),		/* 7: cs80 cartridge tape */
	cdev_disk_init(NSD,sd),		/* 8: SCSI disk */
	cdev_disk_init(NHD,hd),		/* 9: HPIB disk */
	cdev_grf_init(NGRF,grf),	/* 10: frame buffer */
	cdev_ppi_init(NPPI,ppi),	/* 11: printer/plotter interface */
	cdev_tty_init(NDCA,dca),	/* 12: built-in single-port serial */
	cdev_tty_init(NITE,ite),	/* 13: console terminal emulator */
	cdev_hil_init(1,hil),		/* 14: human interface loop */
	cdev_tty_init(NDCM,dcm),	/* 15: 4-port serial */
	cdev_tape_init(NMT,mt),		/* 16: magnetic reel tape */
	cdev_disk_init(NCCD,ccd),	/* 17: concatenated disk */
	cdev_notdef(),			/* 18 */
	cdev_disk_init(NVND,vnd),	/* 19: vnode disk driver */
	cdev_tape_init(NST,st),		/* 20: SCSI tape */
	cdev_fd_init(1,filedesc),	/* 21: file descriptor pseudo-device */
	cdev_bpftun_init(NBPFILTER,bpf),/* 22: Berkeley packet filter */
	cdev_bpftun_init(NTUN,tun),	/* 23: network tunnel */
	cdev_lkm_init(NLKM,lkm),	/* 24: loadable module driver */
	cdev_lkm_dummy(),		/* 25 */
	cdev_lkm_dummy(),		/* 26 */
	cdev_lkm_dummy(),		/* 27 */
	cdev_lkm_dummy(),		/* 28 */
	cdev_lkm_dummy(),		/* 29 */
	cdev_lkm_dummy(),		/* 30 */
	cdev_lkm_dummy(),		/* 31 */
	cdev_random_init(1,random),	/* 32: random generator */
	cdev_gen_ipf(NIPF,ipl),		/* 33: ip filtering */
	cdev_disk_init(NRD,rd),		/* 34: RAM disk */
	cdev_tty_init(NAPCI,apci),	/* 35: Apollo APCI UARTs */
	cdev_ksyms_init(NKSYMS,ksyms),	/* 36: Kernel symbols device */
	cdev_notdef(),			/* 37 */
	cdev_notdef(),			/* 38 */
	cdev_notdef(),			/* 39 */
	cdev_notdef(),			/* 40 */
	cdev_notdef(),			/* 41 */
	cdev_notdef(),			/* 42 */
	cdev_notdef(),			/* 43 */
	cdev_notdef(),			/* 44 */
	cdev_notdef(),			/* 45 */
	cdev_notdef(),			/* 46 */
	cdev_notdef(),			/* 47 */
	cdev_notdef(),			/* 48 */
	cdev_notdef(),			/* 49 */
	cdev_notdef(),			/* 50 */
#ifdef XFS
	cdev_xfs_init(NXFS,xfs_dev),	/* 51: xfs communication device */
#else
	cdev_notdef(),			/* 51 */
#endif
};
int	nchrdev = sizeof(cdevsw) / sizeof(cdevsw[0]);

int	mem_no = 2; 	/* major device number of memory special file */

/*
 * Swapdev is a fake device implemented
 * in sw.c used only internally to get to swstrategy.
 * It cannot be provided to the users, because the
 * swstrategy routine munches the b_dev and b_blkno entries
 * before calling the appropriate driver.  This would horribly
 * confuse, e.g. the hashing routines. Instead, /dev/drum is
 * provided as a character (raw) device.
 */
dev_t	swapdev = makedev(3, 0);

/*
 * Returns true if dev is /dev/mem or /dev/kmem.
 */
int
iskmemdev(dev)
	dev_t dev;
{

	return (major(dev) == mem_no && minor(dev) < 2);
}

/*
 * Returns true if dev is /dev/zero.
 */
int
iszerodev(dev)
	dev_t dev;
{

	return (major(dev) == mem_no && minor(dev) == 12);
}

static int chrtoblktbl[] = {
	/* XXXX This needs to be dynamic for LKMs. */
	/*VCHR*/	/*VBLK*/
	/*  0 */	NODEV,
	/*  1 */	NODEV,
	/*  2 */	NODEV,
	/*  3 */	NODEV,
	/*  4 */	NODEV,
	/*  5 */	NODEV,
	/*  6 */	NODEV,
	/*  7 */	0,
	/*  8 */	4,
	/*  9 */	2,
	/* 10 */	NODEV,
	/* 11 */	NODEV,
	/* 12 */	NODEV,
	/* 13 */	NODEV,
	/* 14 */	NODEV,
	/* 15 */	NODEV,
	/* 16 */	NODEV,
	/* 17 */	5,
	/* 18 */	NODEV,
	/* 19 */	6,
	/* 20 */	7,
	/* 21 */	NODEV,
	/* 22 */	NODEV,
	/* 23 */	NODEV,
	/* 24 */	NODEV,
	/* 25 */	NODEV,
	/* 26 */	NODEV,
	/* 27 */	NODEV,
	/* 28 */	NODEV,
	/* 29 */	NODEV,
	/* 30 */	NODEV,
	/* 31 */	NODEV,
	/* 32 */	NODEV,
	/* 33 */	NODEV,
	/* 34 */	8,
};

/*
 * Convert a character device number to a block device number.
 */
dev_t
chrtoblk(dev)
	dev_t dev;
{
	int blkmaj;

	if (major(dev) >= nchrdev ||
	    major(dev) > sizeof(chrtoblktbl)/sizeof(chrtoblktbl[0]))
		return (NODEV);
	blkmaj = chrtoblktbl[major(dev)];
	if (blkmaj == NODEV)
		return (NODEV);
	return (makedev(blkmaj, minor(dev)));
}

/*
 * Convert a block device number to a character device number.
 */
dev_t
blktochr(dev)
	dev_t dev;
{
	int blkmaj = major(dev);
	int i;

	if (blkmaj >= nblkdev)
		return (NODEV);
	for (i = 0; i < sizeof(chrtoblktbl)/sizeof(chrtoblktbl[0]); i++)
		if (blkmaj == chrtoblktbl[i])
			return (makedev(i, minor(dev)));
	return (NODEV);
}

/*
 * This entire table could be autoconfig()ed but that would mean that
 * the kernel's idea of the console would be out of sync with that of
 * the standalone boot.  I think it best that they both use the same
 * known algorithm unless we see a pressing need otherwise.
 */
#include <dev/cons.h>

#define dvboxcngetc		itecngetc
#define dvboxcnputc		itecnputc
#define dvboxcnpollc		nullcnpollc
cons_decl(dvbox);

#define gboxcngetc		itecngetc
#define gboxcnputc		itecnputc
#define gboxcnpollc		nullcnpollc
cons_decl(gbox);

#define hypercngetc		itecngetc
#define hypercnputc		itecnputc
#define hypercnpollc		nullcnpollc
cons_decl(hyper);

#define rboxcngetc		itecngetc
#define rboxcnputc		itecnputc
#define rboxcnpollc		nullcnpollc
cons_decl(rbox);

#define topcatcngetc		itecngetc
#define topcatcnputc		itecnputc
#define topcatcnpollc		nullcnpollc
cons_decl(topcat);

#define dcacnpollc		nullcnpollc
cons_decl(dca);

#define	apcicnpollc		nullcnpollc
cons_decl(apci);

#define dcmcnpollc		nullcnpollc
cons_decl(dcm);

#include "dvbox.h"
#include "gbox.h"
#include "hyper.h"
#include "rbox.h"
#include "topcat.h"

struct	consdev constab[] = {
#if NITE > 0
#if NDVBOX > 0
	cons_init(dvbox),
#endif
#if NGBOX > 0
	cons_init(gbox),
#endif
#if NHYPER > 0
	cons_init(hyper),
#endif
#if NRBOX > 0
	cons_init(rbox),
#endif
#if NTOPCAT > 0
	cons_init(topcat),
#endif
#endif /* NITE > 0 */
#if NDCA > 0
	cons_init(dca),
#endif
#if NAPCI > 0
	cons_init(apci),
#endif
#if NDCM > 0
	cons_init(dcm),
#endif
	{ 0 },
};
