/*	$KAME: mip6stat.h,v 1.7 2001/05/16 06:41:48 jinmei Exp $	*/

/*
 * Copyright (c) 1999, 2000 and 2001 Ericsson Radio Systems AB
 * All rights reserved.
 *
 * Author:  Magnus Braathen <magnus.braathen@era.ericsson.se>
 */

#include <netinet/in.h>
#include <kvm.h>
#include <fcntl.h>

#define kget(p, d)  (kread((u_long)(p), (char *)&(d), sizeof (d)))
#define kgetp(p, d) (kread((u_long)(p), (char *) (d), sizeof (d)))

/* column widths; each followed by one space */
#define	WID_IP6	 (nflag ? 40 : (lflag ? 24 : 18)) /* width of ip6 addr column */
#define	WID_IP6P (nflag ? 44 : (lflag ? 28 : 22)) /* width of ip6 addr column */

extern int nflag, lflag;
extern kvm_t *kd;

extern int kread(u_long addr, char *buf, int size);
extern void trimdomain(char *cp);
extern char *ip6addr_print(struct in6_addr *in6, int plen, char *);

extern void bcachepr(u_long);
extern void bulistpr(u_long);
extern void foraddrpr(u_long);
extern void haddrpr(u_long);
extern void halistpr(u_long);
extern void configpr(u_long, u_long, u_long);
