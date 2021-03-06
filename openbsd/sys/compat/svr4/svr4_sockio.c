/*	$OpenBSD: svr4_sockio.c,v 1.6 1996/05/07 08:49:20 deraadt Exp $	 */
/*	$NetBSD: svr4_sockio.c,v 1.10 1996/05/03 17:09:15 christos Exp $	 */

/*
 * Copyright (c) 1995 Christos Zoulas
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
 * 3. The name of the author may not be used to endorse or promote products
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
 */

#include <sys/param.h>
#include <sys/proc.h>
#include <sys/systm.h>
#include <sys/file.h>
#include <sys/filedesc.h>
#include <sys/ioctl.h>
#include <sys/termios.h>
#include <sys/tty.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/mount.h>
#include <net/if.h>
#include <sys/malloc.h>

#include <sys/syscallargs.h>

#include <compat/svr4/svr4_types.h>
#include <compat/svr4/svr4_util.h>
#include <compat/svr4/svr4_signal.h>
#include <compat/svr4/svr4_syscallargs.h>
#include <compat/svr4/svr4_stropts.h>
#include <compat/svr4/svr4_ioctl.h>
#include <compat/svr4/svr4_sockio.h>

static int bsd_to_svr4_flags __P((int));

#define bsd_to_svr4_flag(a) \
	if (bf & __CONCAT(I,a))	sf |= __CONCAT(SVR4_I,a)

static int
bsd_to_svr4_flags(bf)
	int bf;
{
	int sf = 0;
	bsd_to_svr4_flag(FF_UP);
	bsd_to_svr4_flag(FF_BROADCAST);
	bsd_to_svr4_flag(FF_DEBUG);
	bsd_to_svr4_flag(FF_LOOPBACK);
	bsd_to_svr4_flag(FF_POINTOPOINT);
	bsd_to_svr4_flag(FF_NOTRAILERS);
	bsd_to_svr4_flag(FF_RUNNING);
	bsd_to_svr4_flag(FF_NOARP);
	bsd_to_svr4_flag(FF_PROMISC);
	bsd_to_svr4_flag(FF_ALLMULTI);
	bsd_to_svr4_flag(FF_MULTICAST);
	return sf;
}

int
svr4_sock_ioctl(fp, p, retval, fd, cmd, data)
	struct file *fp;
	struct proc *p;
	register_t *retval;
	int fd;
	u_long cmd;
	caddr_t data;
{
	int error;
	int (*ctl) __P((struct file *, u_long,  caddr_t, struct proc *)) =
			fp->f_ops->fo_ioctl;

	*retval = 0;

	switch (cmd) {
	case SVR4_SIOCGIFNUM:
		{
			struct ifnet *ifp;
			struct ifaddr *ifa;
			int ifnum = 0;

			/*
			 * This does not return the number of physical
			 * interfaces (if_index), but the number of interfaces
			 * + addresses like ifconf() does, because this number
			 * is used by code that will call SVR4_SIOCGIFCONF to
			 * find the space needed for SVR4_SIOCGIFCONF. So we
			 * count the number of ifreq entries that the next
			 * SVR4_SIOCGIFCONF will return. Maybe a more correct
			 * fix is to make SVR4_SIOCGIFCONF return only one
			 * entry per physical interface?
			 */

			for (ifp = ifnet.tqh_first;
			     ifp != 0; ifp = ifp->if_list.tqe_next)
				if ((ifa = ifp->if_addrlist.tqh_first) == NULL)
					ifnum++;
				else
					for (;ifa != NULL;
					    ifa = ifa->ifa_list.tqe_next)
						ifnum++;


			DPRINTF(("SIOCGIFNUM %d\n", ifnum));
			return copyout(&ifnum, data, sizeof(ifnum));
		}

	case SVR4_SIOCGIFFLAGS:
		{
			struct ifreq br;
			struct svr4_ifreq sr;

			if ((error = copyin(data, &sr, sizeof(sr))) != 0)
				return error;

			(void) strncpy(br.ifr_name, sr.svr4_ifr_name,
			    sizeof(br.ifr_name));

			if ((error = (*ctl)(fp, SIOCGIFFLAGS, 
					    (caddr_t) &br, p)) != 0) {
				DPRINTF(("SIOCGIFFLAGS %s: error %d\n", 
					 sr.svr4_ifr_name, error));
				return error;
			}

			sr.svr4_ifr_flags = bsd_to_svr4_flags(br.ifr_flags);
			DPRINTF(("SIOCGIFFLAGS %s = %x\n", 
				sr.svr4_ifr_name, sr.svr4_ifr_flags));
			return copyout(&sr, data, sizeof(sr));
		}

	case SVR4_SIOCGIFCONF:
		{
			struct svr4_ifconf sc;

			if ((error = copyin(data, &sc, sizeof(sc))) != 0)
				return error;

			DPRINTF(("ifreq %d svr4_ifreq %d ifc_len %d\n",
				sizeof(struct ifreq), sizeof(struct svr4_ifreq),
				sc.svr4_ifc_len));

			if ((error = (*ctl)(fp, OSIOCGIFCONF,
					    (caddr_t) &sc, p)) != 0)
				return error;

			DPRINTF(("SIOCGIFCONF\n"));
			return 0;
		}


	default:
		DPRINTF(("Unknown svr4 sockio %lx\n", cmd));
		return 0;	/* ENOSYS really */
	}
}
