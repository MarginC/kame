/*	$OpenBSD: iomod.h,v 1.5 1999/02/25 17:25:09 mickey Exp $	*/

/*
 * Copyright (c) 1990 mt Xinu, Inc.  All rights reserved.
 * Copyright (c) 1990,1991,1992,1994 University of Utah.  All rights reserved.
 *
 * Permission to use, copy, modify and distribute this software is hereby
 * granted provided that (1) source code retains these copyright, permission,
 * and disclaimer notices, and (2) redistributions including binaries
 * reproduce the notices in supporting documentation, and (3) all advertising
 * materials mentioning features or use of this software display the following
 * acknowledgement: ``This product includes software developed by the
 * Computer Systems Laboratory at the University of Utah.''
 *
 * Copyright (c) 1990 mt Xinu, Inc.
 * This file may be freely distributed in any form as long as
 * this copyright notice is included.
 * MTXINU, THE UNIVERSITY OF UTAH, AND CSL PROVIDE THIS SOFTWARE ``AS
 * IS'' AND WITHOUT ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING,
 * WITHOUT LIMITATION, THE IMPLIED WARRANTIES OF MERCHANTIBILITY AND
 * FITNESS FOR A PARTICULAR PURPOSE.
 *
 * CSL requests users of this software to return to csl-dist@cs.utah.edu any
 * improvements that they make and grant CSL redistribution rights.
 *
 *	Utah $Hdr: iomod.h 1.6 94/12/14$
 */

#ifndef	_MACHINE_IOMOD_H_
#define	_MACHINE_IOMOD_H_

#include <machine/pdc.h>

/*
 * Structures and definitions for I/O Modules on HP-PA (9000/800).
 *
 * Memory layout:
 *
 *	0x00000000	+---------------------------------+
 *			|           Page Zero             |
 *	0x00000800	+ - - - - - - - - - - - - - - - - +
 *			|                                 |
 *			|                                 |
 *			|      Memory Address Space       |
 *			|                                 |
 *			|                                 |
 *	0xEF000000	+---------------------------------+
 *			|                                 |
 *			|        PDC Address Space        |
 *			|                                 |
 *	0xF1000000	+---------------------------------+
 *			|                                 |
 *			|                                 |
 *			|        I/O Address Space        |
 *			|                                 |
 *			|                                 |
 *	0xFFF80000	+ - - - - - - - - - - - - - - - - +
 *			|  Fixed Physical Address Space   |
 *	0xFFFC0000	+ - - - - - - - - - - - - - - - - +
 *			|  Local Broadcast Address Space  |
 *	0xFFFE0000	+ - - - - - - - - - - - - - - - - +
 *			| Global Broadcast Address Space  |
 *	0xFFFFFFFF	+---------------------------------+
 *
 * "Memory Address Space" is used by memory modules,
 *   "Page Zero" is described below.
 * "PDC Address Space" is used by Processor-Dependent Code.
 * "I/O Address Space" is used by I/O modules (and is not cached),
 *   "Fixed Physical" is used by modules on the central bus,
 *   "Local Broadcast" is used to reach all modules on the same bus, and
 *   "Global Broadcast" is used to reach all modules (thru bus converters).
 *   
 * SPA space (see below) ranges from 0xF1000000 thru 0xFFFC0000.
 */

#define	HPPA_IOBEGIN	0xF0000000
#define	HPPA_IOLEN	0x10000000
#define	PDC_ADDR	0xEF000000	/* explained above */
#define	IO_ADDR		0xF1000000
#define	SGC_SLOT1	0xF4000000	/* (hp700) */
#define	SGC_SLOT2	0xF8000000	/* (hp700) */
#define	SGC_SIZE	0x02000000	/* (hp700) */
#define	FP_ADDR		0xFFF80000
#define	LBCAST_ADDR	0xFFFC0000
#define	GBCAST_ADDR	0xFFFE0000

#define	PDC_LOW		PDC_ADDR	/* define some ranges */
#define	PDC_HIGH	IO_ADDR
#define	FPA_LOW		FP_ADDR
#define	FPA_HIGH	LBCAST_ADDR
#define	SPA_LOW		IO_ADDR
#define	SPA_HIGH	LBCAST_ADDR
#define	SGC_LOW		SGC_SLOT1
#define	SGC_HIGH	(SGC_SLOT2+SGC_SIZE)

#define	FPA_IOMOD	((FPA_HIGH-FPA_LOW)/sizeof(struct iomod))
#define	MAXMODBUS	((int)(FPA_IOMOD))	/* maximum modules/bus */

#define	FLEX_MASK	0xFFFC0000	/* (see below) */

/* size of HPA space for any device */
#define	IOMOD_HPASIZE	0x1000

/* ASP prom offset for an lan stattion id */
#define	ASP_PROM	(0xf0810000)

/* offset to the device-specific registers,
 * basically sizeof(struct iomod) (see later)
 */
#define	IOMOD_DEVOFFSET	0x800

#if !defined(_LOCORE)

/*
 * The first 2K of Soft Physical Address space on the Initial Memory Module
 * is aptly called "page zero".  The following structure defines the format
 * of page zero.  Individual members of this structure should be accessed
 * as "PAGE0->member".
 */

#define	PAGE0	((struct pagezero *)0)	/* can't get any lower than this! */

struct pagezero {
	/* [0x000] Initialize Vectors */
	int	ivec_special;		/* must be zero */
					/* powerfail recovery software */
	int	(*ivec_mempf)__P((void));
					/* exec'd after Transfer Of Control */
	int	(*ivec_toc)__P((void));
	u_int	ivec_toclen;		/* bytes of ivec_toc code */
					/* exec'd after Rendezvous Signal */
	int	(*ivec_rendz)__P((void));
	u_int	ivec_mempflen;		/* bytes of ivec_mempf code */
	int	ivec_resv[10];		/* (reserved) must be zero */

	/* [0x040] Processor Dependent */
	union	{
		int	pd_Resv1[112];	/* (reserved) processor dependent */
		struct	{		/* Viper-specific data */
			int	v_Resv1[39];
			u_int	v_Ctrlcpy;	/* copy of Viper `vi_control' */
			int	v_Resv2[72];
		} pd_Viper;
	} pz_Pdep;

	/* [0x200] Reserved */
	int	resv1[84];		/* (reserved) */

	/* [0x350] Memory Configuration */
	int	memc_cont;		/* bytes of contiguous valid memory */
	int	memc_phsize;		/* bytes of valid physical memory */
	int	memc_adsize;		/* bytes of SPA space used by PDC */
	int	memc_resv;		/* (reserved) */

	/* [0x360] Miscellaneous */
	struct boot_err mem_be[8];	/* boot errors (see above) */
	int	mem_free;		/* first free phys. memory location */
	struct iomod *mem_hpa;		/* HPA of CPU */
	int	(*mem_pdc)__P((void));	/* PDC entry point */
	u_int	mem_10msec;		/* # of Interval Timer ticks in 10msec*/

	/* [0x390] Initial Memory Module */
	struct iomod *imm_hpa;		/* HPA of Initial Memory module */
	int	imm_soft_boot;		/* 0 == hard boot, 1 == soft boot */
	int	imm_spa_size;		/* bytes of SPA in IMM */
	int	imm_max_mem;		/* bytes of mem in IMM (<= spa_size) */

	/* [0x3A0] Boot Console/Display, Device, and Keyboard */
	struct pz_device mem_cons;	/* description of console device */
	struct pz_device mem_boot;	/* description of boot device */
	struct pz_device mem_kbd;	/* description of keyboard device */

	/* [0x430] Reserved */
	int	resv2[116];		/* (reserved) */

	/* [0x600] Processor Dependent */
	int	pd_resv2[128];		/* (reserved) processor dependent */
};
#define	v_ctrlcpy	pz_Pdep.pd_Viper.v_Ctrlcpy


/*
 * Every module has 4K-bytes of address space associated with it.
 * A Hard Physical Address (HPA) can be broken down as follows.
 *
 * Since this is an I/O space, the high 4 bits are always 1's.
 *
 * The "flex" address specifies which bus a module is on; there are
 * 256K-bytes of HPA space for each bus, however only values from
 * 64 - 1022 are valid for the "flex" field (1022 designates the
 * central bus).  The "flex" addr is set at bus configuration time.
 *
 * The "fixed" address specifies a particular module on the same
 * bus (i.e. among modules with the same "flex" address).  This
 * value can also be found in "device_path.dp_mod" in "pdc.h".
 *
 * A modules HPA space consists of 2 pages; the "up" bit specifies
 * which of these pages is being addressed.  In general, the lower
 * page is privileged and the upper page it module-type dependent.
 *
 */

struct hpa {
	u_int	hpa_ones: 4,	/* must be 1's; this is an I/O space addr */
		hpa_flex:10,	/* bus address for this module */
		hpa_fixed:6,	/* location of module on bus */
		hpa_up	: 1,	/* 1 == upper page, 0 == lower page */
		hpa_set	: 5,	/* register set */
		hpa_reg	: 4,	/* register number within a register set */
		hpa_zeros:2;	/* must be 0's; addrs are word aligned */
};


/*
 * Certain modules require additional memory (i.e. more than that
 * provided by the HPA space).  A Soft Physical Address (SPA) can be
 * broken down as follows, on a module-type specific basis (either
 * Memory SPA or I/O SPA).
 *
 * SPA space must be a power of 2, and aligned accordingly.  The IODC
 * provides all information needed by software to configure SPA space
 * for a particular module.
 */

struct memspa {
	u_int	spa_page:21,	/* page of memory */
		spa_off	:11;	/* offset into memory page */
};

struct iospa {
	u_int	spa_ones: 4,	/* must be 1's; this is an I/O space addr */
		spa_iopg:17,	/* page in I/O address space */
		spa_set	: 5,	/* register set */
		spa_reg	: 4,	/* register number within a register set */
		spa_mode: 2;	/* aligned according to bus transaction mode */
};


/*
 * It is possible to send a command to all modules on a particular bus
 * (local broadcast), or all modules (global broadcast).  A Broadcast
 * Physical Address (BPA) can be broken down as follows.
 *
 * Read and Clear transactions are not allowed in BPA space.  All pages
 * in BPA space are privileged.
 */

struct bpa {
	u_int	bpa_ones:14,	/* must be 1's; this is in BPA space */
		bpa_gbl	: 1,	/* 0 == local, 1 == global broadcast */
		bpa_page: 6,	/* page in local/global BPA space */
		bpa_set	: 5,	/* register set */
		bpa_reg	: 4,	/* register number within a register set */
		bpa_zeros:2;	/* must be 0's; addrs are word aligned */
};


/*
 * All I/O and Memory modules have 4K-bytes of HPA space associated with
 * it (described above), however not all modules implement every register.
 * The first 2K-bytes of registers are "priviliged".
 *
 * (WO) == Write Only, (RO) == Read Only
 */

struct iomod {
/* SRS (Supervisor Register Set) */
	u_int	io_eir;		/* (WO) interrupt CPU; set bits in EIR CR */
	u_int	io_eim;		/* (WO) External Interrupt Message address */
	u_int	io_dc_rw;	/* write address of IODC to read IODC data */
	int	io_ii_rw;	/* read/clear external intrpt msg (bit-26) */
	caddr_t	io_dma_link;	/* pointer to "next quad" in DMA chain */
	u_int	io_dma_command;	/* (RO) chain command to exec on "next quad" */
	caddr_t	io_dma_address;	/* (RO) start of DMA */
	int	io_dma_count;	/* (RO) number of bytes remaining to xfer */
	caddr_t	io_flex;	/* (WO) HPA flex addr, LSB: bus master flag */
	caddr_t	io_spa;		/* (WO) SPA space; 0-20:addr, 24-31:iodc_spa */
	int	resv1[2];	/* (reserved) */
	u_int	io_command;	/* (WO) module commands (see below) */
	u_int	io_status;	/* (RO) error returns (see below) */
	u_int	io_control;	/* memory err logging (bit-9), bc forwarding */
	u_int	io_test;	/* (RO) self-test information */
/* ARS (Auxiliary Register Set) */
	u_int	io_err_sadd;	/* (RO) slave bus error or memory error addr */
	caddr_t	chain_addr;	/* start address of chain RAM */
	u_int	sub_mask_clr;	/* ignore intrpts on sub-channel (bitmask) */
	u_int	sub_mask_set;	/* service intrpts on sub-channel (bitmask) */
	u_int	diagnostic;	/* diagnostic use (reserved) */
	int	resv2[2];	/* (reserved) */
	caddr_t	nmi_address;	/* address to send data to when NMI detected */
	caddr_t	nmi_data;	/* NMI data to be sent */
	int	resv3[3];	/* (reserved) */
	u_int	io_mem_low;	/* bottom of memory address range */
	u_int	io_mem_high;	/* top of memory address range */
	u_int	io_io_low;	/* bottom of I/O HPA address Range */
	u_int	io_io_high;	/* top of I/O HPA address Range */

	int	priv_trs[160];	/* TRSes (Type-dependent Reg Sets) */

	int	priv_hvrs[320];	/* HVRSes (HVERSION-dependent Register Sets) */

	int	hvrs[512];	/* HVRSes (HVERSION-dependent Register Sets) */
};
#endif	/* !_LOCORE */

/* primarily for a "reboot" and "_rtt" routines */
#define	iomod_command	(4*12)

/* io_flex */
#define	DMA_ENABLE	0x1	/* flex register enable DMA bit */

/* io_spa */
#define	IOSPA(spa,iodc_data)	\
	((volatile caddr_t)		\
	 (spa | iodc_data.iodc_spa_shift | iodc_data.iodc_spa_enb << 5 | \
	  iodc_data.iodc_spa_pack << 6 | iodc_data.iodc_spa_io << 7))

/* io_command */
#define	CMD_STOP	0	/* halt any I/O, enable diagnostic access */
#define	CMD_FLUSH	1	/* abort DMA */
#define	CMD_CHAIN	2	/* initiate DMA */
#define	CMD_CLEAR	3	/* clear errors */
#define	CMD_RESET	5	/* reset any module */

/* io_status */
#define	IO_ERR_MEM_SL	0x10000	/* SPA space lost or corrupted */
#define	IO_ERR_MEM_SE	0x00200	/* severity: minor */
#define	IO_ERR_MEM_HE	0x00100	/* severity: affects invalid parts */
#define	IO_ERR_MEM_FE	0x00080	/* severity: bad */
#define	IO_ERR_MEM_RY	0x00040	/* IO_COMMAND register ready for command */
#define	IO_ERR_DMA_DG	0x00010	/* module in diagnostic mode */
#define	IO_ERR_DMA_PW	0x00004	/* Power Failing */
#define	IO_ERR_DMA_PL	0x00002	/* Power Lost */
#define	IO_ERR_VAL(x)	 (((x) >> 10) & 0x3f)
#define	IO_ERR_DEPEND	 0	/* unspecified error */
#define	IO_ERR_SPA	 1	/* (module-type specific) */
#define	IO_ERR_INTERNAL	 2	/* (module-type specific) */
#define	IO_ERR_MODE	 3	/* invlaid mode or address space mapping */
#define	IO_ERR_ERROR_M	 4	/* bus error (master detect) */
#define	IO_ERR_DPARITY_S 5	/* data parity (slave detect) */
#define	IO_ERR_PROTO_M	 6	/* protocol error (master detect) */
#define	IO_ERR_ADDRESS	 7	/* no slave acknowledgement in transaction */
#define	IO_ERR_MORE	 8	/* device transfered more data than expected */
#define	IO_ERR_LESS	 9	/* device transfered less data than expected */
#define	IO_ERR_SAPARITY	10	/* slave addrss phase parity */
#define	IO_ERR_MAPARITY	11	/* master address phase parity */
#define	IO_ERR_MDPARITY	12	/* mode phase parity */
#define	IO_ERR_STPARITY	13	/* status phase parity */
#define	IO_ERR_CMD	14	/* unimplemented I/O Command */
#define	IO_ERR_BUS	15	/* generic bus error */
#define	IO_ERR_CORR	24	/* correctable memory error */
#define	IO_ERR_UNCORR	25	/* uncorrectable memory error */
#define	IO_ERR_MAP	26	/* equivalent to IO_ERR_CORR */
#define	IO_ERR_LINK	28	/* Bus Converter "link" (connection) error */
#define	IO_ERR_CCMD	32	/* Illegal DMA command */
#define	IO_ERR_ERROR_S	52	/* bus error (slave detect) */
#define	IO_ERR_DPARITY_M 53	/* data parity (master detect) */
#define	IO_ERR_PROTOCOL	54	/* protocol error (slave detect) */
#define	IO_ERR_SELFTEST	58	/* (module-type specific) */
#define	IO_ERR_BUSY	59	/* slave was busy too often or too long */
#define	IO_ERR_RETRY	60	/* "busied" transaction not retried soon enuf */
#define	IO_ERR_ACCESS	61	/* illegal register access */
#define	IO_ERR_IMPROP	62	/* "improper" data written */
#define	IO_ERR_UNKNOWN	63

/* io_control (memory) */
#define	IO_CTL_MEMINIT	0x0	/* prevent some bus errors during memory init */
#define	IO_CTL_MEMOKAY	0x100	/* enable all bus error logging */

/* io_spa */
#define	SPA_ENABLE	0x20	/* io_spa register enable spa bit */

#define	EIM_GRPMASK	0x1F	/* EIM register group mask */
#define	EIEM_MASK(eim)	(0x80000000 >> (eim & EIM_GRPMASK))
#define	EIEM_BITCNT	32	/* number of bits in EIEM register */

#endif	/* _MACHINE_IOMOD_H_ */