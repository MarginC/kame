/*	$OpenBSD: divdi3.c,v 1.2 1997/11/07 15:56:40 niklas Exp $	*/
/*	$NetBSD: divdi3.c,v 1.5 1995/10/07 09:26:24 mycroft Exp $	*/

/*-
 * Copyright (c) 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This software was developed by the Computer Systems Engineering group
 * at Lawrence Berkeley Laboratory under DARPA contract BG 91-66 and
 * contributed to Berkeley.
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
 */

#if defined(LIBC_SCCS) && !defined(lint)
#if 0
static char sccsid[] = "@(#)divdi3.c	8.1 (Berkeley) 6/4/93";
#else
static char rcsid[] = "$OpenBSD: divdi3.c,v 1.2 1997/11/07 15:56:40 niklas Exp $";
#endif
#endif /* LIBC_SCCS and not lint */

#include "quad.h"

/*
 * Divide two signed quads.
 * ??? if -1/2 should produce -1 on this machine, this code is wrong
 */
quad_t
__divdi3(a, b)
	quad_t a, b;
{
	u_quad_t ua, ub, uq;
	int neg;

	if (a < 0)
		ua = -(u_quad_t)a, neg = 1;
	else
		ua = a, neg = 0;
	if (b < 0)
		ub = -(u_quad_t)b, neg ^= 1;
	else
		ub = b;
	uq = __qdivrem(ua, ub, (u_quad_t *)0);
	return (neg ? -uq : uq);
}