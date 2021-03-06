/*-
 * Copyright (c) 1997 John D. Polstra
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
 * $Id: setdef0.c,v 1.1.1.1 1998/08/21 03:17:42 msmith Exp $
 */

#ifdef __ELF__

#include <sys/param.h>
#include <sys/kernel.h>

/*
 * DEFINE_SET creates the section and label for a set, and emits the
 * count word at the front of it.
 */
#define DEFINE_SET(set, count)				\
	__asm__(".section .set." #set ",\"aw\"");	\
	__asm__(".globl " #set);			\
	__asm__(".type " #set ",@object");		\
	__asm__(".p2align 3");				\
	__asm__(#set ":");				\
	__asm__(".quad " #count);			\
	__asm__(".previous")

#include "setdefs.h"		/* Contains a `DEFINE_SET' for each set */

#endif	/* __ELF__ */
