/*	$OpenBSD: mvme188.h,v 1.1 1999/09/27 20:46:18 smurph Exp $ */
/*
 * Copyright (c) 1999 Steve Murphree, Jr.
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
 *      This product includes software developed by Nivas Madhur.
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */
/*
 * Mach Operating System
 * Copyright (c) 1991 Carnegie Mellon University
 * Copyright (c) 1991 OMRON Corporation
 * All Rights Reserved.
 *
 * Permission to use, copy, modify and distribute this software and its
 * documentation is hereby granted, provided that both the copyright
 * notice and this permission notice appear in all copies of the
 * software, derivative works or modified versions, and any portions
 * thereof, and that both notices appear in supporting documentation.
 *
 */

#ifndef	_MACHINE_MVME188_H
#define	_MACHINE_MVME188_H

/*
 * Something to put append a 'U' to a long constant if it's C so that
 * it'll be unsigned in both ANSI and traditional.
 */
#ifndef UDEFINED
#if defined(ASSEMBLER)
#	define U(num)	num
#else
#  if defined(__STDC__)
#	define U(num)	num ## U
#  else
#	define U(num)	num/**/U
#  endif
#endif
#endif


#define MVME188_EPROM		   U(0xFFC00000)
#define MVME188_EPROM_SIZE	   U(0x00080000)
#define MVME188_SRAM		      U(0xFFE00000)
#define MVME188_SRAM_SIZE	   U(0x00020000)
#define MVME188_UTILITY		   U(0xFF000000)
#define MVME188_UTILITY_SIZE	U(0x01000000)
#define UTIL_START            U(0xFFC00000)  /* start of MVME188 utility space */
#define UTIL_SIZE             U(0x003FFFFF)  /* size of MVME188 utility space */

/*
 * MVME188 declarations for hardware level device registers and such.
 */

/* base address for the interrupt control registers */
#define INTR_CONTROL_BASE	U(0xfff84000)
#define VMEA24SPACE 	   U(0xEEC00000) 	/*  VMEA24 master addr space (4 Meg) */

/* per-processor interrupt enable registers */
#define MVME188_IEN0	   U(0xFFF84004)	/* interrupt enable CPU 0 */
#define MVME188_IEN1	   U(0xFFF84008)	/* interrupt enable CPU 1 */
#define MVME188_IEN2	   U(0xFFF84010)	/* interrupt enable CPU 2 */
#define MVME188_IEN3	   U(0xFFF84020)	/* interrupt enable CPU 3 */

/* same as above */
#define IEN0_REG        U(0xfff84004)
#define IEN1_REG        U(0xfff84008)
#define IEN2_REG        U(0xfff84010)
#define IEN3_REG        U(0xfff84020)

#define IENALL_REG      U(0xfff8403c)

#define MVME188_IST	   U(0xFFF84040) 	/* interrupt status register */
#define IST_REG         U(0xfff84040)	/* same as above */

#define MVME188_SETSWI	U(0xFFF84080) 	/* generate soft interrupt */
#define MVME188_CLRSWI	U(0xFFF84084) 	/* reset soft interrupt */
#define MVME188_ISTATE	U(0xFFF84088) 	/* HW interrupt status */
#define MVME188_CLRINT	U(0xFFF8408C) 	/* reset HW interrupt */

/* same as above */
#define SETSWI_REG   U(0xfff84080)	/* SETSWI register addr */
#define CLRSWI_REG   U(0xfff84084)	/* CLRSWI register addr */
#define ISTATE_REG   U(0xfff84088)
#define CLRINT_REG   U(0xfff8408C)

#define MVME188_GCSR 	U(0xFFF86000)	/* 188 global control and status reg */
#define MVME188_UCSR 	U(0xFFF87000)	/* 188 utility control and status reg */
#define MVME188_BASAD 	U(0xFFF87004)	/* 188 base address reg */
#define MVME188_GLBRES 	U(0xFFF8700C)	/* 188 global reset reg */

#define GCSR_BASE    U(0xfff86000)
#define GLOBAL0      GCSR_BASE + 0x01
#define GLOBAL1      GCSR_BASE + 0x03
#define GLOBAL2      GCSR_BASE + 0x05
#define GLOBAL3      GCSR_BASE + 0x07
#define GLB0         U(0xfff86001)
#define GLB1         U(0xfff86003)
#define GLB2         U(0xfff86005)
#define GLB3         U(0xfff86007)
#define M188_SYSCON  U(0x00000040)
#define UCSR_REG     U(0xfff87000)
#define GLBRES_REG   U(0xfff8700C)

#define MVME188_CCSR 	U(0xFFF88000)	/* 188 CPU board control status reg */
#define MVME188_ERROR 	U(0xFFF88004)	/* 188 Mbus fault reg */
#define MVME188_PCNFA 	U(0xFFF88008)	/* 188 Pbus A decoder reg */
#define MVME188_PCNFB 	U(0xFFF8800C)	/* 188 Pbus B decoder reg */
#define MVME188_EXTAD 	U(0xFFF88010)	/* 188 A24 master A24-A31 addr reg */
#define MVME188_WHOAMI 	U(0xFFF88018)	/* 188 whoami reg */
#define MVME188_WMAD 	U(0xFFF88020)	/* 188 write mbus addr decoder reg */
#define MVME188_RMAD 	U(0xFFF88024)	/* 188 read mbus addr decoder reg */
#define MVME188_WVAD 	U(0xFFF88028)	/* 188 write vmebus addr decoder reg */
#define MVME188_RVAD 	U(0xFFF8802C)	/* 188 read vmebus adds decoder reg */

/* duplicates of above */
#define CCSR_REG     U(0xfff88000)
#define ERROR_REG    U(0xfff88004)	/* ERROR register addr */
#define PCNFA_REG    U(0xfff88008)
#define PCNFB_REG    U(0xfff8800c)
#define EXTAD_REG    U(0xfff88010)
#define EXTAM_REG    U(0xfff88014)
#define WHOAMI_REG   U(0xfff88018)	/* WHOAMI register addr */
#define WMAD_REG     U(0xfff88020)
#define RMAD_REG     U(0xfff88024)
#define WVAD_REG     U(0xfff88028)
#define RVAD_REG     U(0xfff8802c)

#define MAD_MDS		   0x07	/* 188 MAD Device Select bits */

#define VMEA24 		   0x5	/* Mbus addess decode select for VMEA24 */
#define VADV		      0x1	/* vmeaddres decode enable */
#define VBDSELBIT 	   0 	   /* bit to enable vme slave response low true */
#define VBDISABLE	      0x1	/* VME BUS Disable */
#define VSDBIT    	   1 	   /* bit number to enable snooping low true */
#define VSDISABLE	      0x2	/* VME Snoop Disable */
#define VASPBIT	 	   21 	/* addr space 0 = A32, 1 = A24 bit */
#define VASP		      0x00200000 /* A24 VME address space */
#define VPN		         0x00400000 /* Page Number LSB */
#define PAGECNT		   0x400	/* number of (4 meg) pages to map */

#define UCSR_PWRUPBIT 	0x4000	/* 188 UCSR powerup indicator */
#define UCSR_DRVSFBIT 	0x2000	/* 188 UCSR Board system fail */
#define UCSR_BRIRQBIT 	0x1000	/* 188 UCSR drives VME IRQ1 broadcast int */
#define UCSR_ROBINBIT 	0x800	   /* 188 UCSR sel round robin VME arbiter mode */
#define UCSR_BRLVBITS 	0x600	   /* 188 UCSR VME bus request level 0-3 */
#define UCSR_RNEVERBIT  0x100	   /* 188 UCSR VME bus never release once req'd */
#define UCSR_RONRBIT	   0x80	   /* 188 UCSR VME bus req release on no request */
#define UCSR_RWDBIT		0x40	   /* 188 UCSR VME bus request release when done */
#define UCSR_EARBTOBIT  0x20	   /* 188 UCSR enable VME arbiter bus timeout */

/* MVME188 VMEbus data transfer timeout select */
#define VTOSELBITS 	0x18	/* 188 UCSR VMEbus timeout select bits */
#define VTO32US 	   0x00	/* VMEbus timeout length - 32 MicroSec */
#define VTO64US 	   0x01	/* VMEbus timeout length - 64 MicroSec */
#define VTO128US 	   0x10	/* VMEbus timeout length - 128 MicroSec */
#define VTODISABLE 	0x18	/* VMEbus timeout length - disabled */

/*
 * processor dependend code section
 * main goal is to concentrate HW dependencies into a few lines
 */
#define ISR_LOW_SOFTINT_MASK(cpu)	(1<<cpu)
#define ISR_HIGH_SOFTINT_MASK(cpu)	(1<<(cpu + 24))
#define ISR_LOW_SOFTMASK		0xf
#define ISR_HIGH_SOFTMASK		(0xf<<24)
#define ISR_SOFTINT_EXCEPT_MASK(cpu)	(ISR_LOW_SOFTINT_MASK(cpu) | ISR_HIGH_SOFTINT_MASK(cpu) | 0xf0fffff0)
#define ISR_CLOCKINT_MASK		(1<<IEN_CIOI_LOG)

#define ISR_RESET_NMI			*(volatile int *)MVME188_CLRINT = 1<<CLRINT_CLRABRTI_LOG
#define ISR_RESET_SYSFAIL		*(volatile int *)MVME188_CLRINT = 1<<CLRINT_CLRSFI_LOG
#define ISR_RESET_ACFAIL		*(volatile int *)MVME188_CLRINT = 1<<CLRINT_CLRACFI_LOG
#define ISR_RESET_LOW_SOFTINT(cpu)	*(int *)MVME188_CLRSWI = ISR_LOW_SOFTINT_MASK(cpu)
#define ISR_RESET_HIGH_SOFTINT(cpu)	*(int *)MVME188_CLRSWI = (1<<(cpu + MAX_CPUS))
#define ISR_DETERMINE_LOW_SOFTINT(cpu)	*(volatile unsigned int *)MVME188_IST & ISR_LOW_SOFTINT_MASK(cpu)
#define ISR_DETERMINE_HIGH_SOFTINT(cpu)	*(volatile unsigned int *)MVME188_IST & ISR_HIGH_SOFTINT_MASK(cpu)
#define ISR_GENERATE_LOW_SOFTINT(cpu)	*((volatile unsigned int *)MVME188_SETSWI) = ISR_LOW_SOFTINT_MASK(cpu)
#define ISR_GENERATE_HIGH_SOFTINT(cpu)	*((volatile unsigned int *)MVME188_SETSWI) = (1<<(cpu + MAX_CPUS))
#define ISR_RESET_MACHINE		*((volatile unsigned *) MVME188_GLBRES) = 1
#define ISR_GET_CURRENT_MASK(cpu)	*int_mask_reg[cpu] & *(volatile int *)MVME188_IST

#define IST_STRING "\20\40ABRT\37ACF\36ARBTO\35DTI\34SWI7\33SWI6\32SWI5\31SWI4\30IRQ7\27res\26CIOI\25SF\24IRQ6\23res\22DI\21SIGHPI\20res\17IRQ5\16res\15IRQ4\14res\13IRQ3\12res\11LWI\10SIGLPI\7IRQ2\6res\5IRQ1\4SWI3\3SWI2\2SWI1\1SWI0"

#define INT_LEVEL	        8		/* # of interrupt level + 1 */

/*
 * masks and offsets for IST
 */
#define HW_FAILURE_MASK		   U(0x60100000)	/* HW failure bits */
#define HW_FAILURE_ACF		   U(0x40000000)  /* AC failure */
#define HW_FAILURE_ABRTO	   U(0x20000000)  /* Arbiter timeout */
#define HW_FAILURE_SYSFAIL	   U(0x00100000) 	/* SYSFAIL asserted */

#define SOFT_INTERRUPT_MASK	U(0x0F00000F)	/* software irq bits */
#define VME_INTERRUPT_MASK	   U(0x00885450)	/* vme irq bits */
#define OBIO_INTERRUPT_MASK	U(0xF0320100)  /* on board I/O */

#define IEN_ABRT_LOG		31
#define IEN_CIOI_LOG		21
#define IEN_DTI_LOG		28
#define IEN_DI_LOG		17

#define CLRINT_CLRABRTI_LOG	2		/* offset into CLRSWI */
#define CLRINT_CLRACFI_LOG	1		/* offset into CLRSWI */
#define CLRINT_CLRSFI_LOG	0		/* offset into CLRSWI */

/* the following codes are the INT exception enable and status bits. */
/* Refer to MVME188 RISC Microcomputer User's Manual, 4-10. */
#define ABRT_BIT     U(0x80000000)
#define ACF_BIT      U(0x40000000)
#define ARBTO_BIT    U(0x20000000)
#define DTI_BIT      U(0x10000000)
#define SWI7_BIT     U(0x08000000)
#define SWI6_BIT     U(0x04000000)
#define SWI5_BIT     U(0x02000000)
#define SWI4_BIT     U(0x01000000)
#define IRQ7_BIT     U(0x00800000)
#define CIOI_BIT     U(0x00200000)
#define SF_BIT       U(0x00100000)
#define IRQ6_BIT     U(0x00080000)
#define DI_BIT       U(0x00020000)
#define SIGHPI_BIT   U(0x00010000)
#define IRQ5_BIT     U(0x00004000)
#define IRQ4_BIT     U(0x00001000)
#define IRQ3_BIT     U(0x00000400)
#define LMI_BIT      U(0x00000100)
#define SIGLPI_BIT   U(0x00000080)
#define IRQ2_BIT     U(0x00000040)
#define IRQ1_BIT     U(0x00000010)
#define SWI3_BIT     U(0x00000008)
#define SWI2_BIT     U(0x00000004)
#define SWI1_BIT     U(0x00000002)
#define SWI0_BIT     U(0x00000001)

#define ABRT_BIT     U(0x80000000)
#define ACF_BIT      U(0x40000000)
#define ARBTO_BIT    U(0x20000000)
#define DTI_BIT      U(0x10000000)
#define CIOI_BIT     U(0x00200000)
#define SF_BIT       U(0x00100000)
#define DI_BIT       U(0x00020000)
#define LMI_BIT      U(0x00000100)

#define LVL7 (ABRT_BIT | ACF_BIT | IRQ7_BIT | SF_BIT)
#define LVL6 (IRQ6_BIT)
#define LVL5 (IRQ5_BIT | DTI_BIT | CIOI_BIT)
#define LVL4 (IRQ4_BIT)
#define LVL3 (IRQ3_BIT | DI_BIT)
#define LVL2 (IRQ2_BIT)
#define LVL1 (IRQ1_BIT)
#define LVL0 (0x0)
#define SLAVE_MASK (LVL6 | LVL1)

#define MASK_LVL_0 (LVL7 | LVL6 | LVL5 | LVL4 | LVL3 | LVL2 | LVL1)
#define MASK_LVL_1 (LVL7 | LVL6 | LVL5 | LVL4 | LVL3 | LVL2) 
#define MASK_LVL_2 (LVL7 | LVL6 | LVL5 | LVL4 | LVL3)  
#define MASK_LVL_3 (LVL7 | LVL6 | LVL5 | LVL4)    
#define MASK_LVL_4 (LVL7 | LVL6 | LVL5)    
#define MASK_LVL_5 (LVL7 | LVL6)
#define MASK_LVL_6 (LVL7)
#define MASK_LVL_7 U(0x00000000) /* all ints disabled */

/* these are the various Z8536 CIO counter/timer registers */
#define CIO_BASE     U(0xfff83000)
#define CIO_PORTC    U(0xfff83000)
#define CIO_PORTB    U(0xfff83004)
#define CIO_PORTA    U(0xfff83008)
#define CIO_CTRL     U(0xfff8300c)

#define CIO_MICR        0x00	/* Master interrupt control register */
#define CIO_MICR_MIE    0x80
#define CIO_MICR_DLC    0x40
#define CIO_MICR_NV     0x20
#define CIO_MICR_PAVIS  0x10
#define CIO_MICR_PBVIS  0x08
#define CIO_MICR_CTVIS  0x04
#define CIO_MICR_RJA    0x02
#define CIO_MICR_RESET  0x01

#define CIO_MCCR        0x01	/* Master configuration control register */
#define CIO_MCCR_PBE    0x80
#define CIO_MCCR_CT1E   0x40
#define CIO_MCCR_CT2E   0x20
#define CIO_MCCR_CT3E   0x10
#define CIO_MCCR_PLC    0x08
#define CIO_MCCR_PAE    0x04

#define CIO_CTMS1       0x1c	/* Counter/timer mode specification #1 */
#define CIO_CTMS2       0x1d	/* Counter/timer mode specification #2 */
#define CIO_CTMS3       0x1e	/* Counter/timer mode specification #3 */
#define CIO_CTMS_CSC    0x80  /* Continuous Single Cycle */
#define CIO_CTMS_EOE    0x40  /* External Output Enable  */
#define CIO_CTMS_ECE    0x20  /* External Count Enable   */
#define CIO_CTMS_ETE    0x10  /* External Trigger Enable */
#define CIO_CTMS_EGE    0x08  /* External Gate Enable    */
#define CIO_CTMS_REB    0x04  /* Retrigger Enable Bit    */
#define CIO_CTMS_PO     0x00  /* Pulse Output            */
#define CIO_CTMS_OSO    0x01  /* One Shot Output         */
#define CIO_CTMS_SWO    0x02  /* Square Wave Output      */

#define CIO_IVR         0x04	/* Interrupt vector register */

#define CIO_CSR1        0x0a	/* Command and status register CTC #1 */
#define CIO_CSR2        0x0b	/* Command and status register CTC #2 */
#define CIO_CSR3        0x0c	/* Command and status register CTC #3 */

#define CIO_CT1MSB      0x16	/* CTC #1 Timer constant - MSB */
#define CIO_CT1LSB      0x17	/* CTC #1 Timer constant - LSB */
#define CIO_CT2MSB      0x18	/* CTC #2 Timer constant - MSB */
#define CIO_CT2LSB      0x19	/* CTC #2 Timer constant - LSB */
#define CIO_CT3MSB      0x1a	/* CTC #3 Timer constant - MSB */
#define CIO_CT3LSB      0x1b	/* CTC #3 Timer constant - LSB */
#define CIO_PDCA        0x23	/* Port A data direction control */
#define CIO_PDCB        0x2b	/* Port B data direction control */

#define CIO_GCB         0x04	/* CTC Gate command bit */
#define CIO_TCB         0x02	/* CTC Trigger command bit */
#define CIO_IE          0xc0	/* CTC Interrupt enable (set) */
#define CIO_CIP         0x20	/* CTC Clear interrupt pending */
#define CIO_IP          0x20	/* CTC Interrupt pending */

/* these are the DART read registers */
#define DART_BASE    U(0xfff82000)
#define DART_MRA     U(0xfff82000)	/* mode A */
#define DART_SRA     U(0xfff82004)	/* status A */
#define DART_RBA     U(0xfff8200c)	/* receive buffer A */
#define DART_IPCR    U(0xfff82010)	/* input port change */
#define DART_ISR     U(0xfff82014)	/* interrupt status */
#define DART_CUR     U(0xfff82018)	/* count upper */
#define DART_CLR     U(0xfff8201c)	/* count lower */
#define DART_MR1B    U(0xfff82020)	/* mode B */
#define DART_SRB     U(0xfff82024)	/* status B */
#define DART_RBB     U(0xfff8202c)	/* receive buffer B */
#define DART_IVR     U(0xfff82030)	/* interrupt vector */
#define DART_INP     U(0xfff82034)	/* input port */
#define DART_STARTC  U(0xfff82038)	/* start counter cmd */
#define DART_STOPC   U(0xfff8203c)	/* stop counter cmd */

/* these are the DART write registers */
#define DART_CSRA    U(0xfff82004)	/* clock select A */
#define DART_CRA     U(0xfff82008)	/* command A */
#define DART_TBA     U(0xfff8200c)	/* transmit buffer A */
#define DART_ACR     U(0xfff82010)	/* auxiliary control */
#define DART_IMR     U(0xfff82014)	/* interrupt mask reg*/
#define DART_CTUR    U(0xfff82018)	/* counter/timer MSB */
#define DART_CTLR    U(0xfff8201c)	/* counter/timer LSB */
#define DART_MRB     U(0xfff82020)	/* mode B */
#define DART_CSRB    U(0xfff82024)	/* clock select B */
#define DART_CRB     U(0xfff82028)	/* command B */
#define DART_TBB     U(0xfff8202c)	/* transmit buffer B */
#define DART_OPCR    U(0xfff82034)	/* output port config*/
#define DART_OPRS    U(0xfff82038)	/* output port set */
#define DART_OPRR    U(0xfff8203c)	/* output port reset */

#ifndef ASSEMBLER

/*
 * Externals
 */

extern unsigned int m188_curspl[MAX_CPUS];
extern unsigned int int_mask_val[INT_LEVEL];
extern unsigned int int_mask_shadow[MAX_CPUS];
extern volatile unsigned int *int_mask_reg[MAX_CPUS];
/*
 * Prototypes
 */
void block_obio_interrupt(unsigned mask);
void unblock_obio_interrupt(unsigned mask);
#endif 
#define M188_IACK        U(0xFFF85000)

#endif	/* _MACHINE_MVME188_H */


