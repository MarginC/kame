/*	$OpenBSD: intr.h,v 1.3 1998/10/09 02:06:40 rahnds Exp $ */

/*
 * Copyright (c) 1997 Per Fogelstrom, Opsycon AB and RTMX Inc, USA.
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
 *	This product includes software developed under OpenBSD by
 *	Per Fogelstrom, Opsycon AB, Sweden for RTMX Inc, North Carolina USA.
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS
 * OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */

#ifndef _MACHINE_INTR_H_
#define _MACHINE_INTR_H_

#define	IPL_BIO		0
#define	IPL_NET		1
#define	IPL_TTY		2
#define	IPL_IMP		3
#define	IPL_CLOCK	4
#define	IPL_NONE	5
#define	IPL_HIGH	6

#define	IST_NONE	0
#define	IST_PULSE	1
#define	IST_EDGE	2
#define	IST_LEVEL	3

#ifndef _LOCORE

void setsoftclock __P((void));
void clearsoftclock __P((void));
int  splsoftclock __P((void));
void setsoftnet   __P((void));
void clearsoftnet __P((void));
int  splsoftnet   __P((void));

void do_pending_int __P((void));


volatile int cpl, ipending, astpending, tickspending;
int imask[7];

/*
 *  Reorder protection in the following inline functions is
 * achived with the "eieio" instruction which the assembler
 * seems to detect and then doen't move instructions past....
 */
static __inline int
splraise(newcpl)
	int newcpl;
{
	int oldcpl;

	__asm__ volatile("sync; eieio\n");	/* don't reorder.... */
	oldcpl = cpl;
	cpl = oldcpl | newcpl;
	__asm__ volatile("sync; eieio\n");	/* reorder protect */
	return(oldcpl);
}

static __inline void
splx(newcpl)
	int newcpl;
{
	__asm__ volatile("sync; eieio\n");	/* reorder protect */
	cpl = newcpl;
	if(ipending & ~newcpl)
		do_pending_int();
	__asm__ volatile("sync; eieio\n");	/* reorder protect */
}

static __inline int
spllower(newcpl)
	int newcpl;
{
	int oldcpl;

	__asm__ volatile("sync; eieio\n");	/* reorder protect */
	oldcpl = cpl;
	cpl = newcpl;
	if(ipending & ~newcpl)
		do_pending_int();
	__asm__ volatile("sync; eieio\n");	/* reorder protect */
	return(oldcpl);
}

/* Following code should be implemented with lwarx/stwcx to avoid
 * the disable/enable. i need to read the manual once more.... */
static __inline void
set_sint(pending)
	int	pending;
{
	int	msrsave;

	__asm__ ("mfmsr %0" : "=r"(msrsave));
	__asm__ volatile ("mtmsr %0" :: "r"(msrsave & ~PSL_EE));
	ipending |= pending;
	__asm__ volatile ("mtmsr %0" :: "r"(msrsave));
}

#define	SINT_CLOCK	0x10000000
#define	SINT_NET	0x20000000
#define	SINT_TTY	0x40000000
#define	SPL_CLOCK	0x80000000
#define	SINT_MASK	(SINT_CLOCK|SINT_NET|SINT_TTY)

#define splbio()	splraise(imask[IPL_BIO])
#define splnet()	splraise(imask[IPL_NET])
#define spltty()	splraise(imask[IPL_TTY])
#define splclock()	splraise(SPL_CLOCK|SINT_MASK)
#define splimp()	splraise(imask[IPL_IMP])
#define splstatclock()	splhigh()
#define	splsoftclock()	spllower(SINT_CLOCK)
#define	splsoftnet()	splraise(SINT_NET)
#define	splsofttty()	splraise(SINT_TTY)

#define	setsoftclock()	set_sint(SINT_CLOCK);
#define	setsoftnet()	set_sint(SINT_NET);
#define	setsofttty()	set_sint(SINT_TTY);

#define	splhigh()	splraise(0xffffffff)
#define	spl0()		spllower(0)

#endif /* _LOCORE */

#endif /* _MACHINE_INTR_H_ */
