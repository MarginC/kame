/* $KAME: radixwalk.c,v 1.1.1.1 2000/08/16 14:40:30 jinmei Exp $ */
/*
 * Copyright (C) 2000 WIDE Project.
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
 * 3. Neither the name of the project nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE PROJECT AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE PROJECT OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
#define INET6

#include <sys/types.h>
#include <sys/socket.h>

#include <net/route.h>
#include <net/radix.h>

#include <netinet/in.h>

#include <fcntl.h>
#include <kvm.h>
#include <nlist.h>

#include <stdio.h>
#include <netdb.h>
#include <unistd.h>
#include <stdlib.h>
#include <limits.h>
#include <string.h>
#include <err.h>

struct rdtree {
	int rd_b;
	int rd_depth;
	int rd_flags;
	int rd_rtflags;
	struct sockaddr_storage rd_key;
	struct sockaddr_storage rd_mask;
	struct radix_node *rd_kaddr;
	struct rdtree *rd_left;
	struct rdtree *rd_right;
	struct rdtree *rd_dup;
};

#ifndef offsetof
#define offsetof(s, e) ((int)&((s *)0)->e)
#endif
#define kget(o, p) \
	(kread((u_long)(o), (char *)&p, sizeof (p)))
#define DEFAULTINDENTPITCH 2

enum {WHITE, LINE} indenttype;

char indentbuf[_POSIX2_LINE_MAX];
int indentpitch = DEFAULTINDENTPITCH;
int af = AF_INET6;
int rtoffset;
static kvm_t *kvmd;

struct rdtree *get_tree __P((struct radix_node *));
struct sockaddr *kgetsa __P((struct sockaddr *));
void kread __P((u_long, char *, int));
void print_tree __P((struct rdtree *, int, int));
void print_addr __P((struct sockaddr *, struct sockaddr *));
void print_mask6 __P((struct sockaddr_in6 *));
void print_mask4 __P((struct sockaddr_in *));
u_long kinit __P((void));

void
usage()
{
	fprintf(stderr,
		"usage: radixwalk [-a] [-f inet[46]] [-i indenttype]\n");
	exit(1);
}

int
main(argc, argv)
	int argc;
	char *argv[];
{
	int i, ch;
	struct radix_node_head *rt_tables[AF_MAX+1], *rnh, head;
	u_long topaddr;
	struct rdtree *t;

	indenttype = LINE;

	while ((ch = getopt(argc, argv, "ahf:i:")) != -1) {
		switch(ch) {
		case 'a':
			af = AF_UNSPEC;
			break;
		case 'f':
			if (strcasecmp(optarg, "inet6") == 0)
				af = AF_INET6;
			else if (strcasecmp(optarg, "inet4") == 0)
				af = AF_INET;
			else if (strcasecmp(optarg, "inet") == 0)
				af = AF_INET;
			else
				errx(1, "unsupported address family: %s",
				     optarg);
			break;
		case 'i':
			if (strcasecmp(optarg, "line") == 0)
				indenttype = LINE;
			else if (strcasecmp(optarg, "white") == 0)
				indenttype = WHITE;
			else
				errx(1, "unsuported indent type: %s", optarg);
			break;
		case 'h':
		default:
			usage();
		}
	}

	topaddr = kinit();
	kget(topaddr, rt_tables);

	for (i = 0; i <= AF_MAX; i++) {
		if ((rnh = rt_tables[i]) == NULL)
			continue;
		kget(rnh, head);
		if (af == AF_UNSPEC || af == i) {
			switch(i) {
			case AF_INET6:
				rtoffset = offsetof(struct sockaddr_in6,
						    sin6_addr) << 3;
				break;
			case AF_INET:
				rtoffset = offsetof(struct sockaddr_in,
						    sin_addr) << 3;
				break;
			default:
				rtoffset = 0; /* XXX */
				break;
			}

			t = get_tree(head.rnh_treetop);
			if (af == AF_UNSPEC)
				printf("AF: %d\n", i);
			print_tree(t, 0, 0);
		}
	}

	exit(0);
}

struct rdtree *
get_tree(rn)
	struct radix_node *rn;
{
	struct radix_node rnode;
	struct rdtree *rdt;
	int depth_l, depth_r;
	struct sockaddr *sa;
	struct rtentry rt, *rtp;

	if ((rdt = (struct rdtree *)malloc(sizeof(*rdt))) == NULL)
		err(1, "get_tree: malloc");
	memset(rdt, 0, sizeof(*rdt));

	kget(rn, rnode);
	rtp = (struct rtentry *)rn;
	kget(rtp, rt);

	if (rnode.rn_b < 0) {
		rdt->rd_kaddr = rn;
		rdt->rd_b = -1;
		rdt->rd_flags = rnode.rn_flags;
		rdt->rd_rtflags = rt.rt_flags;
		if ((rdt->rd_flags & RNF_ROOT) == 0) {
			if (rnode.rn_key != NULL) {
				sa = kgetsa((struct sockaddr *)rnode.rn_key);
				memcpy(&rdt->rd_key, sa, sa->sa_len);
			}

			if (rnode.rn_mask != NULL) {
				sa = kgetsa((struct sockaddr *)rnode.rn_mask);
				memcpy(&rdt->rd_mask, sa, sa->sa_len);
			}
		}

		if (rnode.rn_dupedkey != NULL)
			rdt->rd_dup = get_tree(rnode.rn_dupedkey);
	} else {
		rdt->rd_kaddr = rn;
		rdt->rd_b = rnode.rn_b;
		rdt->rd_left = get_tree(rnode.rn_l);
		rdt->rd_right = get_tree(rnode.rn_r);

		depth_l = rdt->rd_left ? rdt->rd_left->rd_depth : 0;
		depth_r = rdt->rd_right ? rdt->rd_right->rd_depth : 0;
		rdt->rd_depth = depth_l > depth_r ? depth_l : depth_r;
	}

	return(rdt);
}

struct sockaddr *
kgetsa(dst)
	struct sockaddr *dst;
{
	static struct sockaddr_storage ss;
	struct sockaddr *sa = (struct sockaddr *)&ss;

	kget(dst, ss);
	if (sa->sa_len > sizeof (*sa))
		kread((u_long)dst, (char *)sa, sa->sa_len);
	return(sa);
}

void
kread(addr, buf, size)
	u_long addr;
	char *buf;
	int size;
{
	if (kvm_read(kvmd, addr, buf, size) != size)
		errx(1, "%s", kvm_geterr(kvmd));
}

u_long
kinit()
{
	char buf[_POSIX2_LINE_MAX];
	struct nlist nl[] = {{"_rt_tables"}, {""}};

	kvmd = kvm_openfiles(NULL, NULL, NULL, O_RDONLY, buf);
	if (kvmd != NULL) {
		if (kvm_nlist(kvmd, nl) < 0)
			errx(1, "kvm_nlist: %s", kvm_geterr(kvmd));
		if (nl[0].n_type == 0)
			errx(1, "no namelist");
	} else
		errx(1, "kvm not available");

	return(nl[0].n_value);
}

void
print_tree(tn, depth, rightp)
	struct rdtree *tn;
	int depth, rightp;
{
	int plen;
	int indent = depth * indentpitch;

	switch(indenttype) {
	case WHITE:
		printf("%*s", indent, "");
		break;
	case LINE:
		plen = strlen(indentbuf);
		if (indent && plen != indent) {
			/* XXX: overrun */
			sprintf(indentbuf + plen, "%*s+",
				indent - plen - 1, "");
		}
		printf("%s", indentbuf);
		break;
	}

	if (tn->rd_b < 0) {	/* leaf node */
		if ((tn->rd_flags & RNF_ROOT) != 0)
			printf("(root) ");
		print_addr((struct sockaddr *)&tn->rd_key,
			   (tn->rd_rtflags & RTF_HOST) ? NULL :
			   (struct sockaddr *)&tn->rd_mask);
		if (tn->rd_dup != NULL)
			print_tree(tn->rd_dup, depth, 0);
		if (indenttype == LINE && indent)
			indentbuf[plen] = '\0';
	}
	else {			/* internal node */
		printf("[%d]\n", tn->rd_b - rtoffset);
		if (indenttype == LINE && indent) {
			if (rightp)
				indentbuf[plen] = '\0';
			else
				indentbuf[indent - 1] = '|';
		}
		print_tree(tn->rd_left, depth + 1, 0);
		print_tree(tn->rd_right, depth + 1, 1);
		if (indenttype == LINE && indent)
			indentbuf[plen] = '\0';
	}
}

void
print_addr(addr, mask)
	struct sockaddr *addr, *mask;
{
	char addrbuf[NI_MAXHOST];

	if (addr == NULL) {
		printf("null\n");
		return;
	}
	if (addr->sa_family == AF_UNSPEC) {
		/* probably a root node */
		putchar('\n');
		return;
	}

	if (getnameinfo(addr, addr->sa_len, addrbuf, sizeof(addrbuf), NULL,
			0, NI_NUMERICHOST) != 0)
		printf("???");
	else
		printf("%s", addrbuf);

	if (mask != NULL) {
		putchar('/');

		switch(addr->sa_family) {
		case AF_INET6:
			print_mask6((struct sockaddr_in6 *)mask);
			break;
		case AF_INET:
			print_mask4((struct sockaddr_in *)mask);
			break;
		default:
			printf("???");
			break;
		}
	}

	putchar('\n');
}

void
print_mask6(mask)
	struct sockaddr_in6 *mask;
{
	u_char *p, *lim;
	int masklen, illegal = 0;
	struct sockaddr_in6 m0;

	memset(&m0, 0, sizeof(m0));
	memcpy(&m0, mask, mask->sin6_len);
	p  = (u_char *)&m0.sin6_addr;

	for (masklen = 0, lim = p + 16; p < lim; p++) {
		switch (*p) {
		case 0xff:
			masklen += 8;
			break;
		case 0xfe:
			masklen += 7;
			break;
		case 0xfc:
			masklen += 6;
			break;
		case 0xf8:
			masklen += 5;
			break;
		case 0xf0:
			masklen += 4;
			break;
		case 0xe0:
			masklen += 3;
			break;
		case 0xc0:
			masklen += 2;
			break;
		case 0x80:
			masklen += 1;
			break;
		case 0x00:
			break;
		default:
			illegal ++;
			break;
		}
	}
	if (illegal) {
		printf("???");
	}
	else
		printf("%d", masklen);
}

void
print_mask4(mask)
	struct sockaddr_in *mask;
{
	register int b, i;
	struct sockaddr_in m0;
	u_long m1;

	memset(&m0, 0, sizeof(m0));
	memcpy(&m0, mask, mask->sin_len);
	m1 = ntohl(m0.sin_addr.s_addr);

	i = 0;
	for (b = 0; b < 32; b++)
		if (m1 & (1 << b)) {
			register int bb;

			i = b;
			for (bb = b+1; bb < 32; bb++)
				if (!(m1 & (1 << bb))) {
					i = -1;	/* noncontig */
					break;
				}
			break;
		}
	if (i == -1)
		printf("&0x%lx", m1);
	else
		printf("%d", 32-i);
}
