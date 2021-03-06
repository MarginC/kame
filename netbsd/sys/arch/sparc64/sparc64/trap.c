/*	$NetBSD: trap.c,v 1.29 1999/03/30 02:12:37 eeh Exp $ */

/*
 * Copyright (c) 1996
 *	The President and Fellows of Harvard College. All rights reserved.
 * Copyright (c) 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This software was developed by the Computer Systems Engineering group
 * at Lawrence Berkeley Laboratory under DARPA contract BG 91-66 and
 * contributed to Berkeley.
 *
 * All advertising materials mentioning features or use of this software
 * must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Lawrence Berkeley Laboratory.
 *	This product includes software developed by Harvard University.
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
 *	This product includes software developed by Harvard University.
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
 *	@(#)trap.c	8.4 (Berkeley) 9/23/93
 */

#include "opt_ddb.h"
#include "opt_ktrace.h"
#include "opt_compat_svr4.h"
#include "opt_compat_netbsd32.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/proc.h>
#include <sys/user.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/resource.h>
#include <sys/signal.h>
#include <sys/wait.h>
#include <sys/syscall.h>
#include <sys/syslog.h>
#ifdef KTRACE
#include <sys/ktrace.h>
#endif

#include <vm/vm.h>
#include <vm/vm_kern.h>
#include <uvm/uvm_fault.h>

#include <machine/cpu.h>
#include <machine/ctlreg.h>
#include <machine/trap.h>
#include <machine/instr.h>
#include <machine/pmap.h>

#ifdef DDB
#include <machine/db_machdep.h>
#else
#include <machine/frame.h>
#endif
#ifdef COMPAT_SVR4
#include <machine/svr4_machdep.h>
#endif

#include <sparc64/fpu/fpu_extern.h>
#include <sparc64/sparc64/memreg.h>
#include <sparc64/sparc64/cache.h>

#ifndef offsetof
#define	offsetof(s, f) ((int)&((s *)0)->f)
#endif

#ifdef DEBUG
/* What trap level are we running? */
#define tl() ({ \
	int l; \
	__asm __volatile("rdpr %%tl, %0" : "=r" (l) :); \
	l; \
})
#endif

/* trapstats */
int trapstats = 0;
int protfix = 0;
int protmmu = 0;
int missmmu = 0;
int udmiss = 0;	/* Number of normal/nucleus data/text miss/protection faults */
int udhit = 0;	
int udprot = 0;
int utmiss = 0;
int kdmiss = 0;
int kdhit = 0;	
int kdprot = 0;
int ktmiss = 0;
int iveccnt = 0; /* number if normal/nucleus interrupt/interrupt vector faults */
int uintrcnt = 0;
int kiveccnt = 0;
int kintrcnt = 0;
int intristk = 0; /* interrupts when already on intrstack */
int wfill = 0;
int kwfill = 0;
int wspill = 0;
int wspillskip = 0;
int rftucnt = 0;
int rftuld = 0;
int rftudone = 0;
int rftkcnt[5] = { 0, 0, 0, 0, 0 };

extern int cold;

#ifdef DEBUG
#define RW_64		0x1
#define RW_ERR		0x2
#define RW_FOLLOW	0x4
int	rwindow_debug = RW_ERR;
#define TDB_ADDFLT	0x1
#define TDB_TXTFLT	0x2
#define TDB_TRAP	0x4
#define TDB_SYSCALL	0x8
#define TDB_FOLLOW	0x10
#define TDB_FRAME	0x20
#define TDB_NSAVED	0x40
#define TDB_TL		0x80
#define TDB_STOPSIG	0x100
#define TDB_STOPCALL	0x200
#define TDB_STOPCPIO	0x400
#define TDB_SYSTOP	0x800
int	trapdebug = 0/*|TDB_SYSCALL|TDB_STOPSIG|TDB_STOPCPIO|TDB_ADDFLT|TDB_FOLLOW*/;
/* #define __inline */
#endif

#ifdef DDB
#if 1
#define DEBUGGER(t,f)	do { kdb_trap(t,f); } while (0)
#else
#define DEBUGGER(t,f)	Debugger()
#endif
#else
#define DEBUGGER(t,f)
#define Debugger()
#endif

/*
 * Initial FPU state is all registers == all 1s, everything else == all 0s.
 * This makes every floating point register a signalling NaN, with sign bit
 * set, no matter how it is interpreted.  Appendix N of the Sparc V8 document
 * seems to imply that we should do this, and it does make sense.
 */
__asm(".align 64");
struct	fpstate initfpstate = {
	{ ~0, ~0, ~0, ~0, ~0, ~0, ~0, ~0, ~0, ~0, ~0, ~0, ~0, ~0, ~0, ~0,
	  ~0, ~0, ~0, ~0, ~0, ~0, ~0, ~0, ~0, ~0, ~0, ~0, ~0, ~0, ~0, ~0 }
};

/*
 * There are more than 100 trap types, but most are unused.
 *
 * Trap type 0 is taken over as an `Asynchronous System Trap'.
 * This is left-over Vax emulation crap that should be fixed.
 *
 * Traps not supported on the spitfire are marked with `*',
 * and additions are marked with `+'
 */
static const char T[] = "*trap";
const char *trap_type[] = {
	/* non-user vectors */
	"ast",			/* 0 */
	"power on reset",	/* 1 */
	"watchdog reset",	/* 2 */
	"externally initiated reset",/*3 */
	"software initiated reset",/* 4 */
	"RED state exception",	/* 5 */
	T, T,			/* 6..7 */
	"instruction access exception",	/* 8 */
	"*instruction MMU miss",/* 9 */
	"instruction access error",/* 0a */
	T, T, T, T, T,		/* 0b..0f */
	"illegal instruction",	/* 10 */
	"privileged opcode",	/* 11 */
	"*unimplemented LDD",	/* 12 */
	"*unimplemented STD",	/* 13 */
	T, T, T, T,		/* 14..17 */
	T, T, T, T, T, T, T, T, /* 18..1f */
	"fp disabled",		/* 20 */
	"fp exception ieee 754",/* 21 */
	"fp exception other",	/* 22 */
	"tag overflow",		/* 23 */
	"clean window",		/* 24 */
	T, T, T,		/* 25..27 -- trap continues */
	"division by zero",	/* 28 */
	"*internal processor error",/* 29 */
	T, T, T, T, T, T,	/* 2a..2f */
	"data access exception",/* 30 */
	"*data access MMU miss",/* 31 */
	"data access error",	/* 32 */
	"*data access protection",/* 33 */
	"mem address not aligned",	/* 34 */
	"LDDF mem address not aligned",/* 35 */
	"STDF mem address not aligned",/* 36 */
	"privileged action",	/* 37 */
	"LDQF mem address not aligned",/* 38 */
	"STQF mem address not aligned",/* 39 */
	T, T, T, T, T, T,	/* 3a..3f */
	"*async data error",	/* 40 */
	"level 1 int",		/* 41 */
	"level 2 int",		/* 42 */
	"level 3 int",		/* 43 */
	"level 4 int",		/* 44 */
	"level 5 int",		/* 45 */
	"level 6 int",		/* 46 */
	"level 7 int",		/* 47 */
	"level 8 int",		/* 48 */
	"level 9 int",		/* 49 */
	"level 10 int",		/* 4a */
	"level 11 int",		/* 4b */
	"level 12 int",		/* 4c */
	"level 13 int",		/* 4d */
	"level 14 int",		/* 4e */
	"level 15 int",		/* 4f */
	T, T, T, T, T, T, T, T, /* 50..57 */
	T, T, T, T, T, T, T, T, /* 58..5f */
	"+interrupt vector",	/* 60 */
	"+PA_watchpoint",	/* 61 */
	"+VA_watchpoint",	/* 62 */
	"+corrected ECC error",	/* 63 */
	"+fast instruction access MMU miss",/* 64 */
	T, T, T,		/* 65..67 -- trap continues */
	"+fast data access MMU miss",/* 68 */
	T, T, T,		/* 69..6b -- trap continues */
	"+fast data access protection",/* 6c */
	T, T, T,		/* 6d..6f -- trap continues */
	T, T, T, T, T, T, T, T, /* 70..77 */
	T, T, T, T, T, T, T, T, /* 78..7f */
	"spill 0 normal",	/* 80 */
	T, T, T,		/* 81..83 -- trap continues */
	"spill 1 normal",	/* 84 */
	T, T, T,		/* 85..87 -- trap continues */
	"spill 2 normal",	/* 88 */
	T, T, T,		/* 89..8b -- trap continues */
	"spill 3 normal",	/* 8c */
	T, T, T,		/* 8d..8f -- trap continues */
	"spill 4 normal",	/* 90 */
	T, T, T,		/* 91..93 -- trap continues */
	"spill 5 normal",	/* 94 */
	T, T, T,		/* 95..97 -- trap continues */
	"spill 6 normal",	/* 98 */
	T, T, T,		/* 99..9b -- trap continues */
	"spill 7 normal",	/* 9c */
	T, T, T,		/* 9c..9f -- trap continues */
	"spill 0 other",	/* a0 */
	T, T, T,		/* a1..a3 -- trap continues */
	"spill 1 other",	/* a4 */
	T, T, T,		/* a5..a7 -- trap continues */
	"spill 2 other",	/* a8 */
	T, T, T,		/* a9..ab -- trap continues */
	"spill 3 other",	/* ac */
	T, T, T,		/* ad..af -- trap continues */
	"spill 4 other",	/* b0 */
	T, T, T,		/* b1..b3 -- trap continues */
	"spill 5 other",	/* b4 */
	T, T, T,		/* b5..b7 -- trap continues */
	"spill 6 other",	/* b8 */
	T, T, T,		/* b9..bb -- trap continues */
	"spill 7 other",	/* bc */
	T, T, T,		/* bc..bf -- trap continues */
	"fill 0 normal",	/* c0 */
	T, T, T,		/* c1..c3 -- trap continues */
	"fill 1 normal",	/* c4 */
	T, T, T,		/* c5..c7 -- trap continues */
	"fill 2 normal",	/* c8 */
	T, T, T,		/* c9..cb -- trap continues */
	"fill 3 normal",	/* cc */
	T, T, T,		/* cd..cf -- trap continues */
	"fill 4 normal",	/* d0 */
	T, T, T,		/* d1..d3 -- trap continues */
	"fill 5 normal",	/* d4 */
	T, T, T,		/* d5..d7 -- trap continues */
	"fill 6 normal",	/* d8 */
	T, T, T,		/* d9..db -- trap continues */
	"fill 7 normal",	/* dc */
	T, T, T,		/* dc..df -- trap continues */
	"fill 0 other",		/* e0 */
	T, T, T,		/* e1..e3 -- trap continues */
	"fill 1 other",		/* e4 */
	T, T, T,		/* e5..e7 -- trap continues */
	"fill 2 other",		/* e8 */
	T, T, T,		/* e9..eb -- trap continues */
	"fill 3 other",		/* ec */
	T, T, T,		/* ed..ef -- trap continues */
	"fill 4 other",		/* f0 */
	T, T, T,		/* f1..f3 -- trap continues */
	"fill 5 other",		/* f4 */
	T, T, T,		/* f5..f7 -- trap continues */
	"fill 6 other",		/* f8 */
	T, T, T,		/* f9..fb -- trap continues */
	"fill 7 other",		/* fc */
	T, T, T,		/* fc..ff -- trap continues */

	/* user (software trap) vectors */
	"syscall",		/* 100 */
	"breakpoint",		/* 101 */
	"zero divide",		/* 102 */
	"flush windows",	/* 103 */
	"clean windows",	/* 104 */
	"range check",		/* 105 */
	"fix align",		/* 106 */
	"integer overflow",	/* 107 */
	"svr4 syscall",		/* 108 */
	"4.4 syscall",		/* 109 */
	"kgdb exec",		/* 10a */
	T, T, T, T, T,		/* 10b..10f */
	T, T, T, T, T, T, T, T,	/* 11a..117 */
	T, T, T, T, T, T, T, T,	/* 118..11f */
	"svr4 getcc",		/* 120 */
	"svr4 setcc",		/* 121 */
	"svr4 getpsr",		/* 122 */
	"svr4 setpsr",		/* 123 */
	"svr4 gethrtime",	/* 124 */
	"svr4 gethrvtime",	/* 125 */
	T,			/* 126 */
	"svr4 gethrestime",	/* 127 */
	T, T, T, T, T, T, T, T, /* 128..12f */
	T, T,			/* 130..131 */
	"get condition codes",	/* 132 */
	"set condision codes",	/* 133 */
	T, T, T, T,		/* 134..137 */
	T, T, T, T, T, T, T, T, /* 138..13f */
	T, T, T, T, T, T, T, T, /* 140..147 */
	T, T, T, T, T, T, T, T, /* 148..14f */
	T, T, T, T, T, T, T, T, /* 150..157 */
	T, T, T, T, T, T, T, T, /* 158..15f */
	T, T, T, T,		/* 160..163 */
	"SVID syscall64",	/* 164 */
	"SPARC Intl syscall64",	/* 165 */
	"OS vedor spec syscall",/* 166 */
	"HW OEM syscall",	/* 167 */
	"ret from deferred trap",	/* 168 */
};

#define	N_TRAP_TYPES	(sizeof trap_type / sizeof *trap_type)

static __inline void userret __P((struct proc *, int,  u_quad_t));
void trap __P((unsigned, long, long, struct trapframe *));
static __inline void share_fpu __P((struct proc *, struct trapframe *));
void mem_access_fault __P((unsigned, int, u_long, int, int, struct trapframe *));
void data_access_fault __P((unsigned type, u_long va, u_long pc, struct trapframe *));
void data_access_error __P((unsigned, u_long, u_long, u_long, u_long, struct trapframe *));
void text_access_fault __P((unsigned, u_long, struct trapframe *));
void text_access_error __P((unsigned, u_long, u_long, u_long, u_long, struct trapframe *));
void syscall __P((register_t, struct trapframe *, register_t));

#ifdef DEBUG
void print_trapframe __P((struct trapframe *));
void
print_trapframe(tf)
	struct trapframe *tf;
{

	printf("Trapframe %p:\ttstate: %x\tpc: %x\tnpc: %x\n",
	       tf, (long)tf->tf_tstate, (long)tf->tf_pc, (long)tf->tf_npc);
	printf("fault: %p\tkstack: %p\ty: %x\t", 
	       (long)tf->tf_fault, (long)tf->tf_kstack, (int)tf->tf_y);
	printf("pil: %d\toldpil: %d\ttt: %x\tGlobals:\n", 
	       (int)tf->tf_pil, (int)tf->tf_oldpil, (int)tf->tf_tt);
	printf("%08x%08x %08x%08x %08x%08x %08x%08x\n",
	       (long)(tf->tf_global[0]>>32), (long)tf->tf_global[0],
	       (long)(tf->tf_global[1]>>32), (long)tf->tf_global[1],
	       (long)(tf->tf_global[2]>>32), (long)tf->tf_global[2],
	       (long)(tf->tf_global[3]>>32), (long)tf->tf_global[3]);
	printf("%08x%08x %08x%08x %08x%08x %08x%08x\nouts:\n",
	       (long)(tf->tf_global[4]>>32), (long)tf->tf_global[4],
	       (long)(tf->tf_global[5]>>32), (long)tf->tf_global[5],
	       (long)(tf->tf_global[6]>>32), (long)tf->tf_global[6],
	       (long)(tf->tf_global[7]>>32), (long)tf->tf_global[7]);
	printf("%08x%08x %08x%08x %08x%08x %08x%08x\n",
	       (long)(tf->tf_out[0]>>32), (long)tf->tf_out[0],
	       (long)(tf->tf_out[1]>>32), (long)tf->tf_out[1],
	       (long)(tf->tf_out[2]>>32), (long)tf->tf_out[2],
	       (long)(tf->tf_out[3]>>32), (long)tf->tf_out[3]);
	printf("%08x%08x %08x%08x %08x%08x %08x%08x\n",
	       (long)(tf->tf_out[4]>>32), (long)tf->tf_out[4],
	       (long)(tf->tf_out[5]>>32), (long)tf->tf_out[5],
	       (long)(tf->tf_out[6]>>32), (long)tf->tf_out[6],
	       (long)(tf->tf_out[7]>>32), (long)tf->tf_out[7]);

}
#endif

/*
 * Define the code needed before returning to user mode, for
 * trap, mem_access_fault, and syscall.
 */
static __inline void
userret(p, pc, oticks)
	struct proc *p;
	int pc;
	u_quad_t oticks;
{
	int sig;

	/* take pending signals */
	while ((sig = CURSIG(p)) != 0)
		postsig(sig);
	p->p_priority = p->p_usrpri;
	if (want_ast) {
		want_ast = 0;
		if (p->p_flag & P_OWEUPC) {
			p->p_flag &= ~P_OWEUPC;
			ADDUPROF(p);
		}
	}
	if (want_resched) {
		/*
		 * Since we are curproc, clock will normally just change
		 * our priority without moving us from one queue to another
		 * (since the running process is not on a queue.)
		 * If that happened after we put ourselves on the run queue
		 * but before we switched, we might not be on the queue
		 * indicated by our priority.
		 */
		(void) splstatclock();
		setrunqueue(p);
		p->p_stats->p_ru.ru_nivcsw++;
		mi_switch();
		(void) spl0();
		while ((sig = CURSIG(p)) != 0)
			postsig(sig);
	}

	/*
	 * If profiling, charge recent system time to the trapped pc.
	 */
	if (p->p_flag & P_PROFIL)
		addupc_task(p, pc, (int)(p->p_sticks - oticks));

	curpriority = p->p_priority;
}

/*
 * If someone stole the FPU while we were away, do not enable it
 * on return.  This is not done in userret() above as it must follow
 * the ktrsysret() in syscall().  Actually, it is likely that the
 * ktrsysret should occur before the call to userret.
 *
 * Oh, and don't touch the FPU bit if we're returning to the kernel.
 */
static __inline void share_fpu(p, tf)
	struct proc *p;
	struct trapframe *tf;
{
	if (!(tf->tf_tstate & (PSTATE_PRIV<<TSTATE_PSTATE_SHIFT)) &&
	    (tf->tf_tstate & (PSTATE_PEF<<TSTATE_PSTATE_SHIFT)) && fpproc != p)
		tf->tf_tstate &= ~(PSTATE_PEF<<TSTATE_PSTATE_SHIFT);
}

/*
 * Called from locore.s trap handling, for non-MMU-related traps.
 * (MMU-related traps go through mem_access_fault, below.)
 */
void
trap(type, tstate, pc, tf)
	register unsigned type;
	register long tstate, pc;
	register struct trapframe *tf;
{
	register struct proc *p;
	register struct pcb *pcb;
	register int pstate = (tstate>>TSTATE_PSTATE_SHIFT);
	register int64_t n;
	u_quad_t sticks;

	/* This steps the PC over the trap. */
#define	ADVANCE (n = tf->tf_npc, tf->tf_pc = n, tf->tf_npc = n + 4)

#ifdef DEBUG
	if (tf->tf_pc == tf->tf_npc) {
		printf("trap: tpc %p == tnpc %p\n", tf->tf_pc, tf->tf_npc);
		Debugger();
	}
	{
		/* Check to make sure we're on the normal stack */
		int* sp;

		__asm("mov %%sp, %0" : "=r" (sp) :);
		if (sp < eintstack) {
			printf("trap: We're on the interrupt stack!\ntype=0x%x tf=%p %s\n", 
			       type, tf, type < N_TRAP_TYPES ? trap_type[type] : 
			       ((type == T_AST) ? "ast" : 
				((type == T_RWRET) ? "rwret" : T)));
		}
	}
#endif


#ifdef DEBUG
	if ((trapdebug&TDB_NSAVED && cpcb->pcb_nsaved) || trapdebug&(TDB_FOLLOW|TDB_TRAP)) {
		printf("trap: type 0x%x: pc=%lx &tf=%lx\n",
		       type, pc, tf); 
		printf(" npc=%lx pstate=%b %s\n",
		       (long)tf->tf_npc, pstate, PSTATE_BITS, 
		       type < N_TRAP_TYPES ? trap_type[type] : 
		       ((type == T_AST) ? "ast" : 
			((type == T_RWRET) ? "rwret" : T)));
	}
#if 0
	if (trapdebug & TDB_FRAME) {
		print_trapframe(tf);
	}
#endif
#endif

	uvmexp.traps++;
#ifdef DEBUG
	if ((trapdebug&(TDB_FOLLOW|TDB_TRAP)) || ((trapdebug & TDB_TL) && tl())) {
		extern int trap_trace_dis;
		trap_trace_dis = 1;
		printf("trap: type 0x%x: lvl=%d pc=%lx &tf=%lx",
		       type, (int)tl(), pc, tf); 
		printf(" npc=%lx pstate=%b %s\n",
		       (long)tf->tf_npc, pstate, PSTATE_BITS, 
		       type < N_TRAP_TYPES ? trap_type[type] : 
		       ((type == T_AST) ? "ast" : 
			((type == T_RWRET) ? "rwret" : T)));
		kdb_trap(type, tf);
	}
#endif
	/*
	 * Generally, kernel traps cause a panic.  Any exceptions are
	 * handled early here.
	 */
	if (pstate & PSTATE_PRIV) {
#ifdef DDB
		if (type == T_BREAKPOINT) {
			write_all_windows();
			if (kdb_trap(type, tf)) {
				ADVANCE;
				return;
			}
		}
		if (type == T_PA_WATCHPT || type == T_VA_WATCHPT) {
			if (kdb_trap(type, tf)) {
				/* DDB must turn off watchpoints or something */
				return;
			}
		}
#endif
		/*
		 * The kernel needs to use FPU registers for block
		 * load/store.  If we trap in priviliged code, save
		 * the FPU state if there is any and enable the FPU.
		 *
		 * We rely on the kernel code properly enabling the FPU
		 * in %fprs, otherwise we'll hang here trying to enable
		 * the FPU.
		 */
		if (type == T_FPDISABLED) {
			if (fpproc != NULL) {	/* someone else had it */
				savefpstate(fpproc->p_md.md_fpstate);
				fpproc = NULL;
				/* Enable the FPU */
/*				loadfpstate(initfpstate);*/
			}
			tf->tf_tstate |= (PSTATE_PEF<<TSTATE_PSTATE_SHIFT);
			return;
		}
		goto dopanic;
	}
	if ((p = curproc) == NULL)
		p = &proc0;
	sticks = p->p_sticks;
	pcb = &p->p_addr->u_pcb;
	p->p_md.md_tf = tf;	/* for ptrace/signals */

	switch (type) {

	default:
		if (type < 0x100) {
dopanic:
			printf("trap type 0x%x: pc=%lx",
			       type, pc); 
			printf(" npc=%lx pstate=%b\n",
			       (long)tf->tf_npc, pstate, PSTATE_BITS);
			DEBUGGER(type, tf);
			panic(type < N_TRAP_TYPES ? trap_type[type] : T);
			/* NOTREACHED */
		}
#if defined(COMPAT_SVR4) || defined(SUN4M)
badtrap:
#endif
		/* the following message is gratuitous */
		/* ... but leave it in until we find anything */
		printf("%s[%d]: unimplemented software trap 0x%x\n",
		    p->p_comm, p->p_pid, type);
		trapsignal(p, SIGILL, type);
		break;

#ifdef COMPAT_SVR4
	case T_SVR4_GETCC:
	case T_SVR4_SETCC:
	case T_SVR4_GETPSR:
	case T_SVR4_SETPSR:
	case T_SVR4_GETHRTIME:
	case T_SVR4_GETHRVTIME:
	case T_SVR4_GETHRESTIME:
		if (!svr4_trap(type, p))
			goto badtrap;
		break;
#endif

	case T_AST:
		break;	/* the work is all in userret() */

	case T_ILLINST:
	case T_INST_EXCEPT:
	case T_TEXTFAULT:
		printf("trap: textfault at %p!! sending SIGILL due to trap %d: %s\n", 
		       pc, type, type < N_TRAP_TYPES ? trap_type[type] : T);
		Debugger();
		trapsignal(p, SIGILL, 0);	/* XXX code?? */
		break;

	case T_PRIVINST:
		printf("trap: privinst!! sending SIGILL due to trap %d: %s\n", 
		       type, type < N_TRAP_TYPES ? trap_type[type] : T);
		Debugger();
		trapsignal(p, SIGILL, 0);	/* XXX code?? */
		break;

	case T_FPDISABLED: {
		register struct fpstate *fs = p->p_md.md_fpstate;

		if (fs == NULL) {
			/* NOTE: fpstate must be 64-bit aligned */
			fs = malloc((sizeof *fs), M_SUBPROC, M_WAITOK);
			*fs = initfpstate;
			fs->fs_qsize = 0;
			p->p_md.md_fpstate = fs;
		}
		/*
		 * If we have not found an FPU, we have to emulate it.
		 *
		 * Since All UltraSPARC CPUs have an FPU how can this happen?
		 */
		if (!foundfpu) {
#ifdef notyet
			fpu_emulate(p, tf, fs);
			break;
#else
			trapsignal(p, SIGFPE, 0);	/* XXX code?? */
			break;
#endif
		}
		/*
		 * We may have more FPEs stored up and/or ops queued.
		 * If they exist, handle them and get out.  Otherwise,
		 * resolve the FPU state, turn it on, and try again.
		 */
		if (fs->fs_qsize) {
			fpu_cleanup(p, fs);
			break;
		}
		if (fpproc != p) {		/* we do not have it */
			if (fpproc != NULL)	/* someone else had it */
				savefpstate(fpproc->p_md.md_fpstate);
			loadfpstate(fs);
			fpproc = p;		/* now we do have it */
		}
		tf->tf_tstate |= (PSTATE_PEF<<TSTATE_PSTATE_SHIFT);
		break;
	}

	case T_ALIGN:
	case T_LDDF_ALIGN:
	case T_STDF_ALIGN:
	{
		int64_t dsfsr, dsfar=0, isfsr;

		dsfsr = ldxa(SFSR, ASI_DMMU);
		if (dsfsr & SFSR_FV)
			dsfar = ldxa(SFAR, ASI_DMMU);
		isfsr = ldxa(SFSR, ASI_IMMU);
		/* 
		 * If we're busy doing copyin/copyout continue
		 */
		if (p->p_addr && p->p_addr->u_pcb.pcb_onfault) {
			tf->tf_pc = (vaddr_t)p->p_addr->u_pcb.pcb_onfault;
			tf->tf_npc = tf->tf_pc + 4;
			break;
		}
		
#define fmt64(x)	(int)((x)>>32), (int)((x))
		printf("Alignment error: dsfsr=%08x:%08x dsfar=%x:%x isfsr=%08x:%08x pc=%p\n",
		       fmt64(dsfsr), fmt64(dsfar), fmt64(isfsr), pc);
	}
		
#ifdef DDB
		write_all_windows();
		kdb_trap(type, tf);
#endif
		if ((p->p_md.md_flags & MDP_FIXALIGN) != 0 && 
		    fixalign(p, tf) == 0) {
			ADVANCE;
			break;
		}
		trapsignal(p, SIGBUS, 0);	/* XXX code?? */
		break;

	case T_FP_IEEE_754:
	case T_FP_OTHER:
		/*
		 * Clean up after a floating point exception.
		 * fpu_cleanup can (and usually does) modify the
		 * state we save here, so we must `give up' the FPU
		 * chip context.  (The software and hardware states
		 * will not match once fpu_cleanup does its job, so
		 * we must not save again later.)
		 */
		if (p != fpproc)
			panic("fpe without being the FP user");
		savefpstate(p->p_md.md_fpstate);
		fpproc = NULL;
		/* tf->tf_psr &= ~PSR_EF; */	/* share_fpu will do this */
		if (p->p_md.md_fpstate->fs_qsize == 0) {
			copyin((caddr_t)pc, &p->p_md.md_fpstate->fs_queue[0].fq_instr, sizeof(int));
			p->p_md.md_fpstate->fs_qsize = 1;
			fpu_cleanup(p, p->p_md.md_fpstate);
			ADVANCE;
		} else
			fpu_cleanup(p, p->p_md.md_fpstate);
		/* fpu_cleanup posts signals if needed */
#if 0		/* ??? really never??? */
		ADVANCE;
#endif
		break;

	case T_TAGOF:
		trapsignal(p, SIGEMT, 0);	/* XXX code?? */
		break;

	case T_BREAKPOINT:
		trapsignal(p, SIGTRAP, 0);
		break;

	case T_DIV0:
		ADVANCE;
		trapsignal(p, SIGFPE, FPE_INTDIV_TRAP);
		break;

	case T_CLEANWIN:
		uprintf("T_CLEANWIN\n");	/* XXX Should not get this */
		ADVANCE;
		break;

	case T_RANGECHECK:
		printf("T_RANGECHECK\n");	/* XXX */
		ADVANCE;
		trapsignal(p, SIGILL, 0);	/* XXX code?? */
		break;

	case T_FIXALIGN:
#ifdef DEBUG_ALIGN
		uprintf("T_FIXALIGN\n");
#endif
		/* User wants us to fix alignment faults */
		p->p_md.md_flags |= MDP_FIXALIGN;
		ADVANCE;
		break;

	case T_INTOF:
		uprintf("T_INTOF\n");		/* XXX */
		ADVANCE;
		trapsignal(p, SIGFPE, FPE_INTOVF_TRAP);
		break;
	}
	userret(p, pc, sticks);
	share_fpu(p, tf);
#undef ADVANCE
#ifdef DEBUG
	if (trapdebug&(TDB_FOLLOW|TDB_TRAP)) {
		printf("trap: done\n");
		/* if (type != T_BREAKPOINT) Debugger(); */
	}
#if 0
	if (trapdebug & TDB_FRAME) {
		print_trapframe(tf);
	}
#endif
#endif
}

/*
 * Save windows from PCB into user stack, and return 0.  This is used on
 * window overflow pseudo-traps (from locore.s, just before returning to
 * user mode) and when ptrace or sendsig needs a consistent state.
 * As a side effect, rwindow_save() always sets pcb_nsaved to 0.
 *
 * If the windows cannot be saved, pcb_nsaved is restored and we return -1.
 * 
 * XXXXXX This cannot work properly.  I need to re-examine this register
 * window thing entirely.  
 */
int
rwindow_save(p)
	register struct proc *p;
{
	register struct pcb *pcb = &p->p_addr->u_pcb;
	register struct rwindow64 *rw = &pcb->pcb_rw[0];
	register u_int64_t rwdest;
	register int i, j;
#ifndef TRAPWIN
	register struct trapframe *tf = p->p_md.md_tf;
#endif

	/* Make sure our D$ is not polluted w/bad data */
	blast_vcache();

	i = pcb->pcb_nsaved;
#ifdef DEBUG
	if (rwindow_debug&RW_FOLLOW)
		printf("rwindow_save(%p): nsaved %d\n", p, i);
#endif
	if (i == 0)
		return (0);
#ifdef DEBUG
	if (rwindow_debug&RW_FOLLOW)
		printf("%s[%d]: rwindow: pcb->stack:", p->p_comm, p->p_pid);
#endif
	 while (i > 0) {
		rwdest = rw[i--].rw_in[6];
#ifdef DEBUG
		if (rwindow_debug&RW_FOLLOW)
			printf("window %d at %x:%x\n", i, rwdest);
#endif
		if (rwdest & 1) {
#ifndef TRAPWIN
			struct rwindow64 *rwstack;
			/* 64-bit window */
#endif
#ifdef DEBUG
			if (rwindow_debug&RW_64) {
				printf("rwindow_save: 64-bit tf to %p+BIAS or %p\n", 
				       rwdest, rwdest+BIAS);
				Debugger();
			}
#endif
			rwdest += BIAS;
			if (copyout((caddr_t)rw, (caddr_t)rwdest,
				    sizeof(*rw))) {
#ifdef DEBUG
			if (rwindow_debug&(RW_ERR|RW_64))
				printf("rwindow_save: 64-bit pcb copyout to %p failed\n", rwdest);
#endif
				return (-1);
			}
#ifndef TRAPWIN
			rwstack = (struct rwindow64 *)rwdest;
			for (j=0; j<8; j++) { 
				if (copyout((void *)(&rwstack->rw_local[j]), &tf->tf_local[j], 
					    sizeof (tf->tf_local[j]))) {
#ifdef DEBUG
					if (rwindow_debug&(RW_64|RW_ERR))
						printf("rwindow_save: 64-bit tf suword to %p failed\n", 
						       &rwstack->rw_local[j]);
#endif
					return (-1);
				}
			}
#endif
		} else {
			struct rwindow32 rwstack;

			/* 32-bit window */
			for (j=0; j<8; j++) { 
				rwstack.rw_local[j] = (int)rw[i].rw_local[j];
				rwstack.rw_in[j] = (int)rw[i].rw_in[j];
			}
			if (copyout(&rwstack, (caddr_t)rwdest, sizeof(rwstack))) {
#ifdef DEBUG
				if (rwindow_debug&RW_ERR)
					printf("rwindow_save: 32-bit pcb copyout to %p failed\n", rwdest);
#endif
				return (-1);
			}
		}
/*		rw++; */
	}
	pcb->pcb_nsaved = 0;
#ifdef DEBUG
	if (rwindow_debug&RW_FOLLOW) {
		printf("\n");
		Debugger();
	}
#endif
	return (0);
}

/*
 * Kill user windows (before exec) by writing back to stack or pcb
 * and then erasing any pcb tracks.  Otherwise we might try to write
 * the registers into the new process after the exec.
 */
void
kill_user_windows(p)
	struct proc *p;
{

	write_user_windows();
	p->p_addr->u_pcb.pcb_nsaved = 0;
}

#ifdef DEBUG
int dfdebug = 0;
#endif
extern struct proc *masterpaddr;

void
data_access_fault(type, addr, pc, tf)
	unsigned type;
	u_long addr;
	u_long pc;
	struct trapframe *tf;
{
	register u_int64_t tstate;
	register struct proc *p;
	register struct vmspace *vm;
	register vaddr_t va;
	register int rv;
	vm_prot_t ftype;
	vm_prot_t access_type;
	vaddr_t onfault;
	u_quad_t sticks;
#if DEBUG
	static int lastdouble;
	extern struct pcb* cpcb;
#endif

#ifdef DEBUG
	if (tf->tf_pc == tf->tf_npc) {
		printf("data_access_fault: tpc %p == tnpc %p\n", tf->tf_pc, tf->tf_npc);
		Debugger();
	}
	if (protmmu || missmmu) {
		extern int trap_trace_dis;
		trap_trace_dis = 1;
		printf("%ld: data_access_fault(%x, %lx, %lx, %lx) %s=%d\n",
		       (long)(curproc?curproc->p_pid:-1), type, addr, pc, tf, 
		       (protmmu)?"protmmu":"missmmu", (protmmu)?protmmu:missmmu);
		Debugger();
	}
	write_user_windows();
/*	if (cpcb->pcb_nsaved > 6) trapdebug |= TDB_NSAVED; */
	if ((trapdebug&TDB_NSAVED && cpcb->pcb_nsaved) || trapdebug&(TDB_ADDFLT|TDB_FOLLOW)) {
		printf("%ld: data_access_fault(%lx, %p, %p, %p) nsaved=%d\n",
		       (long)(curproc?curproc->p_pid:-1), (long)type, (void*)addr, 
		       (void*)pc, (void*)tf, (int)cpcb->pcb_nsaved);
		if ((trapdebug&TDB_NSAVED && cpcb->pcb_nsaved)) Debugger();
	}
	if (trapdebug & TDB_FRAME) {
		print_trapframe(tf);
	}
	if ((trapdebug & TDB_TL) && tl()) {
		printf("%d: tl %d data_access_fault(%x, %p, %p, %p) nsaved=%d\n",
		       (int)(curproc?curproc->p_pid:-1), (int)tl(), (int)type, 
		       (void*)addr, (void*)pc, (void*)tf, (int)cpcb->pcb_nsaved);
		Debugger();
	}
	if (trapdebug&TDB_STOPCALL) { 
		Debugger();
	}
#endif

	uvmexp.traps++;
	if ((p = curproc) == NULL)	/* safety check */
		p = &proc0;
	sticks = p->p_sticks;

#if 0
	/* This can happen when we're in DDB w/curproc == NULL and try
	 * to access user space.
	 */
#ifdef DIAGNOSTIC
	if ((addr & PAGE_MASK) && 
	    (addr & PAGE_MASK) != p->p_vmspace->vm_map.pmap->pm_ctx) {
		printf("data_access_fault: va ctx %x != pm ctx %x\n",
		       (addr & PAGE_MASK), p->p_vmspace->vm_map.pmap->pm_ctx);
		Debugger();
	}
#endif
#endif
	tstate = tf->tf_tstate;

	/* Find the faulting va to give to uvm_fault */
	va = trunc_page(addr);

#ifdef DEBUG
	if (lastdouble) {
		printf("stacked data fault @ %x (pc %x);", addr, pc);
		lastdouble = 0;
		if (curproc == NULL)
			printf("NULL proc\n");
		else
			printf("pid %d(%s); sigmask %x, sigcatch %x\n",
				curproc->p_pid, curproc->p_comm,
				curproc->p_sigmask, curproc->p_sigcatch);
	}
#endif
	/* Now munch on protections... */

	access_type = (type == T_FDMMU_PROT) ? VM_PROT_READ|VM_PROT_WRITE : VM_PROT_READ;
	ftype = (type == T_FDMMU_PROT) ? VM_FAULT_PROTECT : VM_FAULT_INVALID;
	if (tstate & (PSTATE_PRIV<<TSTATE_PSTATE_SHIFT)) {
		extern char Lfsbail[];
		/*
		 * If this was an access that we shouldn't try to page in,
		 * resume at the fault handler without any action.
		 */
		if (p->p_addr && p->p_addr->u_pcb.pcb_onfault == Lfsbail)
			goto kfault;

		/*
		 * During autoconfiguration, faults are never OK unless
		 * pcb_onfault is set.  Once running normally we must allow
		 * exec() to cause copy-on-write faults to kernel addresses.
		 */
		if (cold)
			goto kfault;
		if (!(addr&TLB_TAG_ACCESS_CTX)) {
			/* CTXT == NUCLEUS */
			if ((rv=uvm_fault(kernel_map, va, ftype, access_type)) == KERN_SUCCESS) {
#ifdef DEBUG
				if (trapdebug&(TDB_ADDFLT|TDB_FOLLOW))
					printf("data_access_fault: kernel uvm_fault(%x, %x, %x, 0) sez %x -- success\n",
					       kernel_map, (vaddr_t)va, ftype, rv);
#endif
				return;
			}
#ifdef DEBUG
			if (trapdebug&(TDB_ADDFLT|TDB_FOLLOW))
				printf("data_access_fault: kernel uvm_fault(%x, %x, %x, 0) sez %x -- failure\n",
				       kernel_map, (vaddr_t)va, ftype, rv);
#endif
			goto kfault;
		}
	} else
		p->p_md.md_tf = tf;

	vm = p->p_vmspace;
	/* alas! must call the horrible vm code */
	rv = uvm_fault(&vm->vm_map, (vaddr_t)va, ftype, access_type);

#ifdef DEBUG
	if (trapdebug&(TDB_ADDFLT|TDB_FOLLOW))
		printf("data_access_fault: user uvm_fault(%x, %x, %x, FALSE) sez %x\n",
		       &vm->vm_map, (vaddr_t)va, ftype, rv);
#endif
	/*
	 * If this was a stack access we keep track of the maximum
	 * accessed stack size.  Also, if uvm_fault gets a protection
	 * failure it is due to accessing the stack region outside
	 * the current limit and we need to reflect that as an access
	 * error.
	 */
	if ((caddr_t)va >= vm->vm_maxsaddr) {
		if (rv == KERN_SUCCESS) {
			unsigned nss = clrnd(btoc(USRSTACK - va));
			if (nss > vm->vm_ssize)
				vm->vm_ssize = nss;
		} else if (rv == KERN_PROTECTION_FAILURE)
			rv = KERN_INVALID_ADDRESS;
	}
	if (rv != KERN_SUCCESS) {
		/*
		 * Pagein failed.  If doing copyin/out, return to onfault
		 * address.  Any other page fault in kernel, die; if user
		 * fault, deliver SIGSEGV.
		 */
		if (tstate & (PSTATE_PRIV<<TSTATE_PSTATE_SHIFT)) {
kfault:
			onfault = p->p_addr ?
			    (long)p->p_addr->u_pcb.pcb_onfault : 0;
			if (!onfault) {
				(void) splhigh();
				printf("data fault: pc=%lx addr=%lx\n",
				    pc, addr);
				DEBUGGER(type, tf);
				panic("kernel fault");
				/* NOTREACHED */
			}
#ifdef DEBUG
			if (trapdebug&(TDB_ADDFLT|TDB_FOLLOW|TDB_STOPCPIO)) {
				printf("data_access_fault: copyin/out of %p fault -- recover\n", addr);
				DEBUGGER(type, tf);
			}
#endif
			tf->tf_pc = onfault;
			tf->tf_npc = onfault + 4;
			return;
		}
#ifdef DEBUG
		if (trapdebug&(TDB_ADDFLT|TDB_STOPSIG)) {
			extern int trap_trace_dis;
			trap_trace_dis = 1;
			printf("data_access_fault at addr %p: sending SIGSEGV\n", addr);
			Debugger();
		}
#endif
		if (rv == KERN_RESOURCE_SHORTAGE) {
			printf("UVM: pid %d (%s), uid %d killed: out of swap\n",
			       p->p_pid, p->p_comm,
			       p->p_cred && p->p_ucred ?
			       p->p_ucred->cr_uid : -1);
			trapsignal(p, SIGKILL, (u_long)addr);
		} else {
			trapsignal(p, SIGSEGV, (u_long)addr);
		}
	}
	if ((tstate & TSTATE_PRIV) == 0) {
		userret(p, pc, sticks);
		share_fpu(p, tf);
	}
#ifdef DEBUG
	if (trapdebug&(TDB_ADDFLT|TDB_FOLLOW))
		printf("data_access_fault: done\n");
	if (trapdebug & TDB_FRAME) {
		print_trapframe(tf);
	}
	if (trapdebug&(TDB_ADDFLT|TDB_FOLLOW)) {
		extern void* return_from_trap __P((void));
		if ((void*)tf->tf_pc == (void*)return_from_trap) {
			printf("Returning from stack datafault\n");
		}
	}
#endif
}

void
data_access_error(type, sfva, sfsr, afva, afsr, tf)
	register unsigned type;
	register u_long sfva;
	register u_long sfsr;
	register u_long afva;
	register u_long afsr;
	register struct trapframe *tf;
{
	register u_long pc;
	register u_int64_t tstate;
	register struct proc *p;
	register struct vmspace *vm;
	register vaddr_t va = 0; /* Stupid GCC warning */
	register int rv;
	vm_prot_t ftype;
	vm_prot_t access_type;
	vaddr_t onfault;
	u_quad_t sticks;
#ifdef DEBUG
	static int lastdouble;
#endif

#if DEBUG
	if (tf->tf_pc == tf->tf_npc) {
		printf("data_access_error: tpc %p == tnpc %p\n", tf->tf_pc, tf->tf_npc);
		Debugger();
	}
	if (protmmu || missmmu) {
		extern int trap_trace_dis;
		trap_trace_dis = 1;
		printf("%d: data_access_error(%x, %lx, %lx, %lx) %s=%d\n",
		       curproc?curproc->p_pid:-1, type, sfva, afva, tf, 
		       (protmmu)?"protmmu":"missmmu", (protmmu)?protmmu:missmmu);
		Debugger();
	}
	write_user_windows();
	if ((trapdebug&TDB_NSAVED && cpcb->pcb_nsaved) || trapdebug&(TDB_ADDFLT|TDB_FOLLOW))
		printf("%ld data_access_error(%lx, %lx, %lx, %p)=%lx @ %p %lx %%qb\n",
		       (long)curproc?curproc->p_pid:-1, 
		       (long)type, (long)sfva, (long)afva, tf, (long)tf->tf_tstate, 
		       (long)tf->tf_pc, (long)sfsr, SFSR_BITS); 
	if (trapdebug & TDB_FRAME) {
		print_trapframe(tf);
	}
	if ((trapdebug & TDB_TL) && tl()) {
		printf("%ld tl %ld data_access_error(%lx, %lx, %lx, %p)=%lx @ %lx %%qb\n",
		       (long)curproc?curproc->p_pid:-1, (long)tl(),
		       (long)type, (long)sfva, (long)afva, tf, (long)tf->tf_tstate, 
		       (long)tf->tf_pc, (long)sfsr, SFSR_BITS); 
		Debugger();
	}
	if (trapdebug&TDB_STOPCALL) { 
		Debugger();
	}
#endif

	uvmexp.traps++;
	if ((p = curproc) == NULL)	/* safety check */
		p = &proc0;
	sticks = p->p_sticks;

	pc = tf->tf_pc;
	tstate = tf->tf_tstate;

	/*
	 * Our first priority is handling serious faults, such as
	 * parity errors or async faults that might have come through here.
	 * If we have a data fault, but SFSR_FAV is not set in the sfsr,
	 * then things are really bizarre, and we treat it as a hard
	 * error and pass it on to memerr4m. 
	 */
	if ((afsr) != 0 ||
	    (type == T_DATAFAULT && !(sfsr & SFSR_FV))) {
#ifdef not4u
		memerr4m(type, sfsr, sfva, afsr, afva, tf);
		/*
		 * If we get here, exit the trap handler and wait for the
		 * trap to reoccur
		 */
		goto out;
#else
		printf("data memory error type %x sfsr=%p sfva=%p afsr=%p afva=%p tf=%p\n",
		       type, sfsr, sfva, afsr, afva, tf);
		if (tstate & (PSTATE_PRIV<<TSTATE_PSTATE_SHIFT)) {
DEBUGGER(type, tf);
			/* User fault -- Berr */
			trapsignal(p, SIGBUS, (u_long)sfva);
		} else {
			DEBUGGER(type, tf);
			panic("trap: memory error");
		}
#endif
	}

	/*
	 * Figure out what to pass the VM code. We cannot ignore the sfva
	 * register on text faults, since this might be a trap on an
	 * alternate-ASI access to code space. However, we can't help using 
	 * have a DMMU sfar.
	 * Kernel faults are somewhat different: text faults are always
	 * illegal, and data faults are extra complex.  User faults must
	 * set p->p_md.md_tf, in case we decide to deliver a signal.  Check
	 * for illegal virtual addresses early since those can induce more
	 * faults.
	 * All translation faults are illegal, and result in a SIGSEGV
	 * being delivered to the running process (or a kernel panic, for
	 * a kernel fault). We check the translation first to make sure
	 * it is not spurious.
	 * Also, note that in the case where we have an overwritten
	 * text fault (OW==1, AT==2,3), we attempt to service the
	 * second (overwriting) fault, then restart the instruction
	 * (which is from the first fault) and allow the first trap
	 * to reappear. XXX is this right? It will probably change...
	 */
	if ((sfsr & SFSR_FV) == 0 || (sfsr & SFSR_FT) == 0) {
		printf("data_access_error: no fault\n");
		goto out;	/* No fault. Why were we called? */
	}

	/*
	 * This next section is a mess since some chips use sfva, and others
	 * don't on text faults. We want to use sfva where possible, since
	 * we _could_ be dealing with an ASI 0x8,0x9 data access to text space,
	 * which would trap as a text fault, at least on a HyperSPARC. Ugh.
	 * XXX: Find out about MicroSPARCs.
	 */

	if (!(sfsr & SFSR_FV)) {
#ifdef DEBUG
		if (trapdebug&(TDB_ADDFLT|TDB_FOLLOW))
			printf("data_access_error: got fault without valid SFVA\n");
#endif
		goto fault;
	}

	va = trunc_page(sfva);

#ifdef DEBUG
	if (lastdouble) {
		printf("stacked data error @ %x (pc %x); sfsr %x", sfva, pc, sfsr);
		lastdouble = 0;
		if (curproc == NULL)
			printf("NULL proc\n");
		else
			printf("pid %d(%s); sigmask %x, sigcatch %x\n",
				curproc->p_pid, curproc->p_comm,
				curproc->p_sigmask, curproc->p_sigcatch);
	}
#endif
	/* Now munch on protections... */

	access_type = (sfsr & SFSR_W) ? VM_PROT_READ|VM_PROT_WRITE : VM_PROT_READ;
	ftype = VM_FAULT_PROTECT; /* Mapping must exist... */
	if (tstate & (PSTATE_PRIV<<TSTATE_PSTATE_SHIFT)) {
		extern char Lfsbail[];
		/*
		 * If this was an access that we shouldn't try to page in,
		 * resume at the fault handler without any action.
		 */
		if (p->p_addr && p->p_addr->u_pcb.pcb_onfault == Lfsbail)
			goto kfault;

		/*
		 * During autoconfiguration, faults are never OK unless
		 * pcb_onfault is set.  Once running normally we must allow
		 * exec() to cause copy-on-write faults to kernel addresses.
		 */
		if (cold)
			goto kfault;
		if (SFSR_CTXT_IS_PRIM(sfsr) || SFSR_CTXT_IS_NUCLEUS(sfsr)) {
			/* NUCLEUS context */
			if (uvm_fault(kernel_map, va, ftype, access_type) == KERN_SUCCESS)
				return;
			if (SFSR_CTXT_IS_NUCLEUS(sfsr))
				goto kfault;
		}
	} else
		p->p_md.md_tf = tf;

	vm = p->p_vmspace;
	/* alas! must call the horrible vm code */
#ifdef DEBUG
	if (trapdebug&(TDB_ADDFLT|TDB_FOLLOW))
		printf("data_access_error: calling uvm_fault\n");
#endif
	rv = uvm_fault(&vm->vm_map, (vaddr_t)va, ftype, access_type);

	/*
	 * If this was a stack access we keep track of the maximum
	 * accessed stack size.  Also, if uvm_fault gets a protection
	 * failure it is due to accessing the stack region outside
	 * the current limit and we need to reflect that as an access
	 * error.
	 */
	if ((caddr_t)va >= vm->vm_maxsaddr) {
		if (rv == KERN_SUCCESS) {
			unsigned nss = clrnd(btoc(USRSTACK - va));
			if (nss > vm->vm_ssize)
				vm->vm_ssize = nss;
		} else if (rv == KERN_PROTECTION_FAILURE)
			rv = KERN_INVALID_ADDRESS;
	}
	if (rv != KERN_SUCCESS) {
		/*
		 * Pagein failed.  If doing copyin/out, return to onfault
		 * address.  Any other page fault in kernel, die; if user
		 * fault, deliver SIGSEGV.
		 */
fault:
		if (tstate & (PSTATE_PRIV<<TSTATE_PSTATE_SHIFT)) {
kfault:
			onfault = p->p_addr ?
			    (long)p->p_addr->u_pcb.pcb_onfault : 0;
			if (!onfault) {
				(void) splhigh();
				printf("data fault: pc=%lx addr=%lx sfsr=%%qb\n",
				    (u_long)pc, (long)sfva, (long)sfsr, SFSR_BITS);
				DEBUGGER(type, tf);
				panic("kernel fault");
				/* NOTREACHED */
			}
#ifdef DEBUG
			if (trapdebug&(TDB_ADDFLT|TDB_FOLLOW|TDB_STOPCPIO)) {
				printf("data_access_error: kern fault -- skipping instr\n");
				if (trapdebug&TDB_STOPCPIO) DEBUGGER(type, tf);
			}
#endif
			tf->tf_pc = onfault;
			tf->tf_npc = onfault + 4;
			return;
		}
#ifdef DEBUG
		if (trapdebug&(TDB_ADDFLT|TDB_STOPSIG)) {
			extern int trap_trace_dis;
			trap_trace_dis = 1;
			printf("data_access_error at %p: sending SIGSEGV\n", va);
			Debugger();
		}
#endif
		trapsignal(p, SIGSEGV, (u_long)sfva);
	}
out:
	if ((tstate & TSTATE_PRIV) == 0) {
		userret(p, pc, sticks);
		share_fpu(p, tf);
	}
#ifdef DEBUG
	if (trapdebug&(TDB_ADDFLT|TDB_FOLLOW))
		printf("data_access_error: done\n");
	if (trapdebug & TDB_FRAME) {
		print_trapframe(tf);
	}
#endif
}

void
text_access_fault(type, pc, tf)
	register unsigned type;
	register u_long pc;
	register struct trapframe *tf;
{
	register u_int64_t tstate;
	register struct proc *p;
	register struct vmspace *vm;
	register vaddr_t va;
	register int rv;
	vm_prot_t ftype;
	vm_prot_t access_type;
	u_quad_t sticks;

#if DEBUG
	if (tf->tf_pc == tf->tf_npc) {
		printf("text_access_fault: tpc %p == tnpc %p\n", tf->tf_pc, tf->tf_npc);
		Debugger();
	}
	if (protmmu || missmmu) {
		extern int trap_trace_dis;
		trap_trace_dis = 1;
		printf("%d: text_access_fault(%x, %x, %x, %x) %s=%d\n",
		       curproc?curproc->p_pid:-1, type, pc, tf, 
		       (protmmu)?"protmmu":"missmmu", (protmmu)?protmmu:missmmu);
		Debugger();
	}
	write_user_windows();
	if ((trapdebug&TDB_NSAVED && cpcb->pcb_nsaved) || trapdebug&(TDB_TXTFLT|TDB_FOLLOW))
		printf("%d text_access_fault(%x, %x, %x)\n",
		       curproc?curproc->p_pid:-1, type, pc, tf); 
	if (trapdebug & TDB_FRAME) {
		print_trapframe(tf);
	}
	if ((trapdebug & TDB_TL) && tl()) {
		printf("%d tl %d text_access_fault(%x, %x, %x)\n",
		       curproc?curproc->p_pid:-1, tl(), type, pc, tf); 
		Debugger();
	}
	if (trapdebug&TDB_STOPCALL) { 
		Debugger();
	}
#endif

	uvmexp.traps++;
	if ((p = curproc) == NULL)	/* safety check */
		p = &proc0;
	sticks = p->p_sticks;

	tstate = tf->tf_tstate;

	va = trunc_page(pc);

	/* Now munch on protections... */

	access_type = /* VM_PROT_EXECUTE| */VM_PROT_READ;
	ftype = VM_FAULT_INVALID;
	if (tstate & (PSTATE_PRIV<<TSTATE_PSTATE_SHIFT)) {
		(void) splhigh();
		printf("text_access_fault: pc=%x\n", pc);
		DEBUGGER(type, tf);
		panic("kernel fault");
		/* NOTREACHED */
	} else
		p->p_md.md_tf = tf;

	vm = p->p_vmspace;
	/* alas! must call the horrible vm code */
	rv = uvm_fault(&vm->vm_map, (vaddr_t)va, ftype, access_type);

#ifdef DEBUG
	if (trapdebug&(TDB_TXTFLT|TDB_FOLLOW))
		printf("text_access_fault: uvm_fault(%x, %x, %x, FALSE) sez %x\n",
		       &vm->vm_map, (vaddr_t)va, ftype, rv);
#endif
	/*
	 * If this was a stack access we keep track of the maximum
	 * accessed stack size.  Also, if uvm_fault gets a protection
	 * failure it is due to accessing the stack region outside
	 * the current limit and we need to reflect that as an access
	 * error.
	 */
	if ((caddr_t)va >= vm->vm_maxsaddr) {
		if (rv == KERN_SUCCESS) {
			unsigned nss = clrnd(btoc(USRSTACK - va));
			if (nss > vm->vm_ssize)
				vm->vm_ssize = nss;
		} else if (rv == KERN_PROTECTION_FAILURE)
			rv = KERN_INVALID_ADDRESS;
	}
	if (rv != KERN_SUCCESS) {
		/*
		 * Pagein failed. Any other page fault in kernel, die; if user
		 * fault, deliver SIGSEGV.
		 */
		if (tstate & TSTATE_PRIV) {
			(void) splhigh();
			printf("text fault: pc=%x\n",
			       pc);
			DEBUGGER(type, tf);
			panic("kernel fault");
			/* NOTREACHED */
		}
#ifdef DEBUG
		if (trapdebug&(TDB_TXTFLT|TDB_STOPSIG)) {
			extern int trap_trace_dis;
			trap_trace_dis = 1;
			printf("text_access_fault at %p: sending SIGSEGV\n", va);
			Debugger();
		}
#endif
		trapsignal(p, SIGSEGV, (u_long)pc);
	}
	if ((tstate & TSTATE_PRIV) == 0) {
		userret(p, pc, sticks);
		share_fpu(p, tf);
	}
#ifdef DEBUG
	if (trapdebug&(TDB_TXTFLT|TDB_FOLLOW)) {
		printf("text_access_fault: done\n");
		/* kdb_trap(T_BREAKPOINT, tf); */
	}
	if (trapdebug & TDB_FRAME) {
		print_trapframe(tf);
	}
#endif
}


void
text_access_error(type, pc, sfsr, afva, afsr, tf)
	register unsigned type;
	register u_long pc;
	register u_long sfsr;
	register u_long afva;
	register u_long afsr;
	register struct trapframe *tf;
{
	register int64_t tstate;
	register struct proc *p;
	register struct vmspace *vm;
	register vaddr_t va;
	register int rv;
	vm_prot_t ftype;
	vm_prot_t access_type;
	u_quad_t sticks;
#if DEBUG
	static int lastdouble;
#endif
	
#if DEBUG
	if (tf->tf_pc == tf->tf_npc) {
		printf("text_access_error: tpc %p == tnpc %p\n", tf->tf_pc, tf->tf_npc);
		Debugger();
	}
	if (protmmu || missmmu) {
		extern int trap_trace_dis;
		trap_trace_dis = 1;
		printf("%ld: text_access_error(%lx, %lx, %lx, %lx) %s=%d\n",
		       (long)curproc?curproc->p_pid:-1, (long)type, (long)sfsr, (long)afsr, tf, 
		       (protmmu)?"protmmu":"missmmu", (protmmu)?protmmu:missmmu);
		Debugger();
	}
	write_user_windows();
	if ((trapdebug&TDB_NSAVED && cpcb->pcb_nsaved) || trapdebug&(TDB_TXTFLT|TDB_FOLLOW))
		printf("%ld text_access_error(%lx, %lx, %lx, %p)=%lx @ %lx %%qb\n",
		       (long)curproc?curproc->p_pid:-1, 
		       (long)type, pc, (long)afva, tf, (long)tf->tf_tstate, 
		       (long)tf->tf_pc, (long)sfsr, SFSR_BITS); 
	if (trapdebug & TDB_FRAME) {
		print_trapframe(tf);
	}
	if ((trapdebug & TDB_TL) && tl()) {
		printf("%ld tl %ld text_access_error(%lx, %lx, %lx, %p)=%lx @ %lx %%qb\n",
		       (long)curproc?curproc->p_pid:-1, (long)tl(),
		       (long)type, (long)pc, (long)afva, tf, 
		       (long)tf->tf_tstate, (long)tf->tf_pc, (long)sfsr, SFSR_BITS); 
		Debugger();
	}
	if (trapdebug&TDB_STOPCALL) { 
		Debugger();
	}
#endif
	uvmexp.traps++;
	if ((p = curproc) == NULL)	/* safety check */
		p = &proc0;
	sticks = p->p_sticks;

	tstate = tf->tf_tstate;

	if ((afsr) != 0) {
#ifdef not4u
		/* Async text fault??? */
		memerr4m(type, sfsr, pc, afsr, afva, tf);
		/*
		 * If we get here, exit the trap handler and wait for the
		 * trap to reoccur
		 */
		goto out;
#else
		printf("text_access_error: memory error...");
		printf("text memory error type %d sfsr=%p sfva=%p afsr=%p afva=%p tf=%p\n",
		       type, sfsr, pc, afsr, afva, tf);
		DEBUGGER(type, tf);
		panic("text_access_error: memory error");
#endif
	}

	if ((sfsr & SFSR_FV) == 0 || (sfsr & SFSR_FT) == 0)
		goto out;	/* No fault. Why were we called? */

	va = trunc_page(pc);

#ifdef DEBUG
	if (lastdouble) {
		printf("stacked text error @ %x (pc %x); sfsr %x", pc, sfsr);
		lastdouble = 0;
		if (curproc == NULL)
			printf("NULL proc\n");
		else
			printf("pid %d(%s); sigmask %x, sigcatch %x\n",
				curproc->p_pid, curproc->p_comm,
				curproc->p_sigmask, curproc->p_sigcatch);
	}
#endif
	/* Now munch on protections... */

	access_type = /* VM_PROT_EXECUTE| */ VM_PROT_READ;
	ftype = VM_FAULT_PROTECT; /* Protection fault? */
	if (tstate & (PSTATE_PRIV<<TSTATE_PSTATE_SHIFT)) {
		(void) splhigh();
		printf("text error: pc=%lx sfsr=%%qb\n", pc, (long)sfsr, SFSR_BITS);
		DEBUGGER(type, tf);
		panic("kernel fault");
		/* NOTREACHED */
	} else
		p->p_md.md_tf = tf;

	vm = p->p_vmspace;
	/* alas! must call the horrible vm code */
	rv = uvm_fault(&vm->vm_map, (vaddr_t)va, ftype, access_type);

	/*
	 * If this was a stack access we keep track of the maximum
	 * accessed stack size.  Also, if uvm_fault gets a protection
	 * failure it is due to accessing the stack region outside
	 * the current limit and we need to reflect that as an access
	 * error.
	 */
	if ((caddr_t)va >= vm->vm_maxsaddr) {
		if (rv == KERN_SUCCESS) {
			unsigned nss = clrnd(btoc(USRSTACK - va));
			if (nss > vm->vm_ssize)
				vm->vm_ssize = nss;
		} else if (rv == KERN_PROTECTION_FAILURE)
			rv = KERN_INVALID_ADDRESS;
	}
	if (rv != KERN_SUCCESS) {
		/*
		 * Pagein failed.  If doing copyin/out, return to onfault
		 * address.  Any other page fault in kernel, die; if user
		 * fault, deliver SIGSEGV.
		 */
		if (tstate & TSTATE_PRIV) {
			(void) splhigh();
			printf("text error: pc=%lx sfsr=%%qb\n",
			       pc, sfsr, SFSR_BITS);
			DEBUGGER(type, tf);
			panic("kernel fault");
			/* NOTREACHED */
		}
#ifdef DEBUG
		if (trapdebug&(TDB_TXTFLT|TDB_STOPSIG)) {
			extern int trap_trace_dis;
			trap_trace_dis = 1;
			printf("text_access_error at %p: sending SIGSEGV\n", va);
			Debugger();
		}
#endif
		trapsignal(p, SIGSEGV, (u_long)pc);
	}
out:
	if ((tstate & TSTATE_PRIV) == 0) {
		userret(p, pc, sticks);
		share_fpu(p, tf);
	}
#ifdef DEBUG
	if (trapdebug&(TDB_TXTFLT|TDB_FOLLOW))
		printf("text_access_error: done\n");
	if (trapdebug & TDB_FRAME) {
		print_trapframe(tf);
	}
#endif
}

/*
 * System calls.  `pc' is just a copy of tf->tf_pc.
 *
 * Note that the things labelled `out' registers in the trapframe were the
 * `in' registers within the syscall trap code (because of the automatic
 * `save' effect of each trap).  They are, however, the %o registers of the
 * thing that made the system call, and are named that way here.
 *
 * 32-bit system calls on a 64-bit system are a problem.  Each system call
 * argument is stored in the smaller of the argument's true size or a
 * `register_t'.  Now on a 64-bit machine all normal types can be stored in a
 * `register_t'.  (The only exceptions would be 128-bit `quad's or 128-bit
 * extended precision floating point values, which we don't support.)  For
 * 32-bit syscalls, 64-bit integers like `off_t's, double precision floating
 * point values, and several other types cannot fit in a 32-bit `register_t'.
 * These will require reading in two `register_t' values for one argument.
 *
 * In order to calculate the true size of the arguments and therefore whether
 * any argument needs to be split into two slots, the system call args
 * structure needs to be built with the appropriately sized register_t.
 * Otherwise the emul needs to do some magic to split oversized arguments.
 *
 * We can handle most this stuff for normal syscalls by using either a 32-bit
 * or 64-bit array of `register_t' arguments.  Unfortunately ktrace always
 * expects arguments to be `register_t's, so it loses badly.  What's worse,
 * ktrace may need to do size translations to massage the argument array
 * appropriately according to the emulation that is doing the ktrace.
 *  
 */
void
syscall(code, tf, pc)
	register_t code;
	register struct trapframe *tf;
	register_t pc;
{
	register int i, nsys, nap;
	register int64_t *ap;
	register struct sysent *callp;
	register struct proc *p;
	int error, new;
	union args {
		register32_t i[8];
		register64_t l[8];
	} args;
	register_t rval[2];
	u_quad_t sticks;
#ifdef DIAGNOSTIC
	extern struct pcb *cpcb;
#endif

#ifdef DEBUG
	write_user_windows();
	if (tf->tf_pc == tf->tf_npc) {
		printf("syscall: tpc %p == tnpc %p\n", tf->tf_pc, tf->tf_npc);
		Debugger();
	}
	if ((trapdebug&TDB_NSAVED && cpcb->pcb_nsaved) || trapdebug&(TDB_SYSCALL|TDB_FOLLOW))
		printf("%d syscall(%x, %x, %x)\n",
		       curproc?curproc->p_pid:-1, code, tf, pc); 
	if (trapdebug & TDB_FRAME) {
		print_trapframe(tf);
	}
	if (trapdebug & TDB_STOPCALL)
		Debugger();
	if ((trapdebug & TDB_TL) && tl()) {
		printf("%d tl %d syscall(%x, %x, %x)\n",
		       curproc?curproc->p_pid:-1, tl(), code, tf, pc); 
		Debugger();
	}
#endif

	uvmexp.syscalls++;
	p = curproc;
#ifdef DIAGNOSTIC
	if (tf->tf_tstate & TSTATE_PRIV)
		panic("syscall from kernel");
	if (cpcb != &p->p_addr->u_pcb)
		panic("syscall: cpcb/ppcb mismatch");
	if (tf != (struct trapframe *)((caddr_t)cpcb + USPACE) - 1)
		panic("syscall: trapframe");
#endif
	sticks = p->p_sticks;
	p->p_md.md_tf = tf;
	new = code & (SYSCALL_G7RFLAG | SYSCALL_G2RFLAG);
	code &= ~(SYSCALL_G7RFLAG | SYSCALL_G2RFLAG);

	callp = p->p_emul->e_sysent;
	nsys = p->p_emul->e_nsysent;

	/*
	 * The first six system call arguments are in the six %o registers.
	 * Any arguments beyond that are in the `argument extension' area
	 * of the user's stack frame (see <machine/frame.h>).
	 *
	 * Check for ``special'' codes that alter this, namely syscall and
	 * __syscall.  The latter takes a quad syscall number, so that other
	 * arguments are at their natural alignments.  Adjust the number
	 * of ``easy'' arguments as appropriate; we will copy the hard
	 * ones later as needed.
	 */
	ap = &tf->tf_out[0];
	nap = 6;

	switch (code) {
	case SYS_syscall:
		code = *ap++;
		nap--;
		break;
	case SYS___syscall:
		if (callp != sysent)
			break;
		if (tf->tf_out[6] & 1L) {
			/* longs *are* quadwords */
			code = ap[0];
			ap += 1;
			nap -= 1;			
		} else {
			code = ap[_QUAD_LOWWORD];
			ap += 2;
			nap -= 2;
		}
		break;
	}

#ifdef DEBUG
/*	printf("code=%x, nsys=%x\n", code, nsys);*/
	if (trapdebug&(TDB_SYSCALL|TDB_FOLLOW))
		printf("%d syscall(%d[%x]): tstate=%x:%x %s\n", curproc?curproc->p_pid:-1, code, code,
		       (int)(tf->tf_tstate>>32), (int)(tf->tf_tstate),
		       (code < 0 || code >= nsys)? "illegal syscall" : p->p_emul->e_syscallnames[code]);
	p->p_addr->u_pcb.lastcall = ((code < 0 || code >= nsys)? "illegal syscall" : p->p_emul->e_syscallnames[code]);
#endif
	if (code < 0 || code >= nsys)
		callp += p->p_emul->e_nosys;
	else if (tf->tf_out[6] & 1L) {
		register64_t *argp;
#ifndef __arch64__
#ifdef DEBUG
		printf("syscall(): 64-bit stack on a 32-bit kernel????\n");
		Debugger();
#endif
#endif
		/* 64-bit stack -- not really supported on 32-bit kernels */
		callp += code;
		i = callp->sy_narg; /* Why divide? */
#ifdef DEBUG
		if (i != (long)callp->sy_argsize / sizeof(register64_t))
			printf("syscall %s: narg=%hd, argsize=%hd, call=%p, argsz/reg64=%ld\n",
			       (code < 0 || code >= nsys)? "illegal syscall" : p->p_emul->e_syscallnames[code], 
			       callp->sy_narg, callp->sy_argsize, callp->sy_call, (long)callp->sy_argsize / sizeof(register64_t));
#endif
		if (i > nap) {	/* usually false */
#if 0
			register64_t temp[6];
			int j = 0;
#endif
#ifdef DEBUG
			if (trapdebug&(TDB_SYSCALL|TDB_FOLLOW) || i>8) {
				printf("Args64 %d>%d -- need to copyin\n", i , nap);
			}
#endif
			if (i > 8)
				panic("syscall nargs");
#if 0
			/* Read the whole block in */
			error = copyin((caddr_t)tf->tf_out[6] + BIAS +
				       offsetof(struct frame64, fr_argx),
				       (caddr_t)&temp, (i - nap) * sizeof(register64_t));
			/* Copy each to the argument array */
			for (j=0; nap+j < i; j++)
				args.l[nap+j] = temp[j];
#else
			/* Read the whole block in */
			error = copyin((caddr_t)tf->tf_out[6] + BIAS +
				       offsetof(struct frame64, fr_argx),
				       (caddr_t)&args.l[nap], (i - nap) * sizeof(register64_t));
#endif
			if (error) {
#ifdef KTRACE
				if (KTRPOINT(p, KTR_SYSCALL))
					ktrsyscall(p->p_tracep, code,
						   callp->sy_argsize, (register_t*)args.l);
#endif
				goto bad;
			}
			i = nap;
		}
		/* It should be faster to do <=6 longword copies than call bcopy */
		for (argp = &args.l[0]; i--;) 
			*argp++ = *ap++;
		
#ifdef DEBUG
		if (trapdebug&(TDB_SYSCALL|TDB_FOLLOW)) {
			for (i=0; i < callp->sy_narg; i++) 
				printf("arg[%d]=%lx ", i, (long)(args.l[i]));
			printf("\n");
		}
		if (trapdebug&(TDB_STOPCALL|TDB_SYSTOP)) { 
			printf("stop precall\n");
			Debugger();
		}
#endif
	} else {
		register32_t *argp;
		/* 32-bit stack */
		callp += code;

#if defined(__arch64__) && COMPAT_SPARC32 != 1
#ifdef DEBUG
		printf("syscall(): 32-bit stack on a 64-bit kernel????\n");
		Debugger();
#endif
#endif

		i = (long)callp->sy_argsize / sizeof(register32_t);
		if (i > nap) {	/* usually false */
			register32_t temp[6];
			int j = 0;
#ifdef DEBUG
			if (trapdebug&(TDB_SYSCALL|TDB_FOLLOW) || i>8)
				printf("Args %d>%d -- need to copyin\n", i , nap);
#endif
			if (i > 8)
				panic("syscall nargs");
			/* Read the whole block in */
			error = copyin((caddr_t)(tf->tf_out[6] +
						 offsetof(struct frame32, fr_argx)),
				       (caddr_t)&temp, (i - nap) * sizeof(register32_t));
			/* Copy each to the argument array */
			for (j=0; nap+j < i; j++)
				args.i[nap+j] = temp[j];
#ifdef DEBUG
			if (trapdebug&(TDB_SYSCALL|TDB_FOLLOW))	{ 
				int k;
				printf("Copyin args of %d from %p:\n", j, 
				       (caddr_t)(tf->tf_out[6] + offsetof(struct frame32, fr_argx)));
				for (k=0; k<j; k++)
					printf("arg %d = %p at %d val %p\n", k, (long)temp[k], nap+k, (long)args.i[nap+k]);
			}
#endif
			if (error) {
#ifdef KTRACE
				if (KTRPOINT(p, KTR_SYSCALL))
					ktrsyscall(p->p_tracep, code,
					    callp->sy_argsize, (register_t *)args.i);
#endif
				goto bad;
			}
			i = nap;
		}
		/* Need to convert from int64 to int32 or we lose */
		for (argp = &args.i[0]; i--;) 
				*argp++ = *ap++;
#ifdef DEBUG
		if (trapdebug&(TDB_SYSCALL|TDB_FOLLOW)) {
			for (i=0; i < (long)callp->sy_argsize / sizeof(register32_t); i++) 
				printf("arg[%d]=%x ", i, (int)(args.i[i]));
			printf("\n");
		}
		if (trapdebug&(TDB_STOPCALL|TDB_SYSTOP)) { 
			printf("stop precall\n");
			Debugger();
		}
#endif
	}
#ifdef KTRACE
	if (KTRPOINT(p, KTR_SYSCALL))
		ktrsyscall(p->p_tracep, code, callp->sy_argsize, (register_t *)args.i);
#endif
	rval[0] = 0;
	rval[1] = tf->tf_out[1];
	error = (*callp->sy_call)(p, &args, rval);

	switch (error) {
		vaddr_t dest;
	case 0:
		/* Note: fork() does not return here in the child */
		tf->tf_out[0] = rval[0];
		tf->tf_out[1] = rval[1];
		if (new) {
			/* jmp %g2 (or %g7, deprecated) on success */
			dest = tf->tf_global[new & SYSCALL_G2RFLAG ? 2 : 7];
#ifdef DEBUG
			if (trapdebug&(TDB_SYSCALL|TDB_FOLLOW))
				printf("syscall: return tstate=%llx new success to %p retval %x:%x\n", 
				       tf->tf_tstate, dest, rval[0], rval[1]);
#endif
			if (dest & 3) {
				error = EINVAL;
				goto bad;
			}
		} else {
			/* old system call convention: clear C on success */
			tf->tf_tstate &= ~(((int64_t)(ICC_C|XCC_C))<<TSTATE_CCR_SHIFT);	/* success */
			dest = tf->tf_npc;
#ifdef DEBUG
			if (trapdebug&(TDB_SYSCALL|TDB_FOLLOW))
				printf("syscall: return tstate=%llx old success to %p retval %x:%x\n", 
				       tf->tf_tstate, dest, rval[0], rval[1]);
			if (trapdebug&(TDB_SYSCALL|TDB_FOLLOW))
				printf("old pc=%p npc=%p dest=%p\n", tf->tf_pc, tf->tf_npc, dest);
#endif
		}
		tf->tf_pc = dest;
		tf->tf_npc = dest + 4;
		break;

	case ERESTART:
	case EJUSTRETURN:
		/* nothing to do */
		break;

	default:
	bad:
		if (p->p_emul->e_errno)
			error = p->p_emul->e_errno[error];
		tf->tf_out[0] = error;
		tf->tf_tstate |= (((int64_t)(ICC_C|XCC_C))<<TSTATE_CCR_SHIFT);	/* fail */
		dest = tf->tf_npc;
		tf->tf_pc = dest;
		tf->tf_npc = dest + 4;
#ifdef DEBUG
		if (trapdebug&(TDB_SYSCALL|TDB_FOLLOW)) 
			printf("syscall: return tstate=%llx fail %d to %p\n", 
			       tf->tf_tstate, error, dest);
#endif
		break;
	}

	userret(p, pc, sticks);
#ifdef NOTDEF_DEBUG
	if ( code == 202) {
		/* Trap on __sysctl */
		Debugger();
	}
#endif
#ifdef KTRACE
	if (KTRPOINT(p, KTR_SYSRET))
		ktrsysret(p->p_tracep, code, error, rval[0]);
#endif
	share_fpu(p, tf);
#ifdef DEBUG
	if (trapdebug&(TDB_STOPCALL|TDB_SYSTOP)) { 
		Debugger();
	}
#endif
#ifdef DEBUG
	if (trapdebug & TDB_FRAME) {
		print_trapframe(tf);
	}
#endif
}

/*
 * Process the tail end of a fork() for the child.
 */
void
child_return(arg)
	void *arg;
{
	struct proc *p = arg;

	/*
	 * Return values in the frame set by cpu_fork().
	 */
#ifdef NOTDEF_DEBUG
	printf("child_return: proc=%p\n", p);
#endif
	userret(p, p->p_md.md_tf->tf_pc, 0);
#ifdef KTRACE
	if (KTRPOINT(p, KTR_SYSRET))
		ktrsysret(p->p_tracep,
			  (p->p_flag & P_PPWAIT) ? SYS_vfork : SYS_fork, 0, 0);
#endif
}

