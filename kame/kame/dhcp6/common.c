/*	$KAME: common.c,v 1.82 2003/07/10 15:13:56 jinmei Exp $	*/
/*
 * Copyright (C) 1998 and 1999 WIDE Project.
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

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/sockio.h>
#include <sys/ioctl.h>
#include <sys/queue.h>
#if TIME_WITH_SYS_TIME
# include <sys/time.h>
# include <time.h>
#else
# if HAVE_SYS_TIME_H
#  include <sys/time.h>
# else
#  include <time.h>
# endif
#endif
#include <net/if.h>
#include <net/if_types.h>
#if defined(__FreeBSD__) && __FreeBSD__ >= 3
#include <net/if_var.h>
#endif
#include <net/if_dl.h>
#include <net/if_arp.h>

#include <netinet/in.h>
#include <netinet6/in6_var.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdarg.h>
#include <syslog.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <err.h>
#include <netdb.h>

#ifdef HAVE_GETIFADDRS 
# ifdef HAVE_IFADDRS_H
#  define USE_GETIFADDRS
#  include <ifaddrs.h>
# endif
#endif

#include <dhcp6.h>
#include <config.h>
#include <common.h>
#include <timer.h>

int foreground;
int debug_thresh;

#if 0
static unsigned int if_maxindex __P((void));
#endif
static int dhcp6_count_list __P((struct dhcp6_list *));
static int in6_matchflags __P((struct sockaddr *, char *, int));
static int copyout_option __P((char *, char *, struct dhcp6_listval *));
static int copyin_option __P((int, struct dhcp6opt *, struct dhcp6opt *,
    struct dhcp6_list *));
static ssize_t gethwid __P((char *, int, const char *, u_int16_t *));
static int get_delegated_prefixes __P((char *, char *,
				       struct dhcp6_optinfo *));

int
dhcp6_copy_list(dst, src)
	struct dhcp6_list *dst, *src;
{
	struct dhcp6_listval *ent;

	for (ent = TAILQ_FIRST(src); ent; ent = TAILQ_NEXT(ent, link)) {
		if (dhcp6_add_listval(dst, ent->type,
		    &ent->uv, &ent->sublist) == NULL)
			goto fail;
	}

	return (0);

  fail:
	dhcp6_clear_list(dst);
	return (-1);
}

void
dhcp6_move_list(dst, src)
	struct dhcp6_list *dst, *src;
{
	struct dhcp6_listval *v;

	while ((v = TAILQ_FIRST(src)) != NULL) {
		TAILQ_REMOVE(src, v, link);
		TAILQ_INSERT_TAIL(dst, v, link);
	}
}

void
dhcp6_clear_list(head)
	struct dhcp6_list *head;
{
	struct dhcp6_listval *v;

	while ((v = TAILQ_FIRST(head)) != NULL) {
		TAILQ_REMOVE(head, v, link);
		dhcp6_clear_listval(v);
	}

	return;
}

static int
dhcp6_count_list(head)
	struct dhcp6_list *head;
{
	struct dhcp6_listval *v;
	int i;

	for (i = 0, v = TAILQ_FIRST(head); v; v = TAILQ_NEXT(v, link))
		i++;

	return (i);
}

void
dhcp6_clear_listval(lv)
	struct dhcp6_listval *lv;
{
	dhcp6_clear_list(&lv->sublist);
	free(lv);
}

/*
 * Note: this function only searches for the first entry that matches
 * VAL.  It also does not care about sublists.
 */
struct dhcp6_listval *
dhcp6_find_listval(head, type, val, option)
	struct dhcp6_list *head;
	dhcp6_listval_type_t type;
	void *val;
	int option;
{
	struct dhcp6_listval *lv;

	for (lv = TAILQ_FIRST(head); lv; lv = TAILQ_NEXT(lv, link)) {
		if (lv->type != type)
			continue;

		switch(type) {
		case DHCP6_LISTVAL_NUM:
			if (lv->val_num == *(int *)val)
				return (lv);
			break;
		case DHCP6_LISTVAL_STCODE:
			if (lv->val_num16 == *(u_int16_t *)val)
				return (lv);
			break;
		case DHCP6_LISTVAL_ADDR6:
			if (IN6_ARE_ADDR_EQUAL(&lv->val_addr6,
			    (struct in6_addr *)val)) {
				return (lv);
			}
			break;
		case DHCP6_LISTVAL_PREFIX6:
			if ((option & MATCHLIST_PREFIXLEN) &&
			    lv->val_prefix6.plen ==
			    ((struct dhcp6_prefix *)val)->plen) {
				return (lv);
			} else if (IN6_ARE_ADDR_EQUAL(&lv->val_prefix6.addr,
			    &((struct dhcp6_prefix *)val)->addr) &&
			    lv->val_prefix6.plen ==
			    ((struct dhcp6_prefix *)val)->plen) {
				return (lv);
			}
			break;
		case DHCP6_LISTVAL_IAPD:
			if (lv->val_ia.iaid ==
			    ((struct dhcp6_ia *)val)->iaid) {
				return (lv);
			}
			break;
		}
	}

	return (NULL);
}

struct dhcp6_listval *
dhcp6_add_listval(head, type, val, sublist)
	struct dhcp6_list *head, *sublist;
	dhcp6_listval_type_t type;
	void *val;
{
	struct dhcp6_listval *lv = NULL;

	if ((lv = malloc(sizeof(*lv))) == NULL) {
		dprintf(LOG_ERR, FNAME,
		    "failed to allocate memory for list entry");
		goto fail;
	}
	memset(lv, 0, sizeof(*lv));
	lv->type = type;
	TAILQ_INIT(&lv->sublist);

	switch(type) {
	case DHCP6_LISTVAL_NUM:
		lv->val_num = *(int *)val;
		break;
	case DHCP6_LISTVAL_STCODE:
		lv->val_num16 = *(u_int16_t *)val;
		break;
	case DHCP6_LISTVAL_ADDR6:
		lv->val_addr6 = *(struct in6_addr *)val;
		break;
	case DHCP6_LISTVAL_PREFIX6:
		lv->val_prefix6 = *(struct dhcp6_prefix *)val;
		break;
	case DHCP6_LISTVAL_IAPD:
		lv->val_ia = *(struct dhcp6_ia *)val;
		break;
	default:
		dprintf(LOG_ERR, FNAME,
		    "unexpected list value type (%d)", type);
		goto fail;
	}

	if (sublist && dhcp6_copy_list(&lv->sublist, sublist))
		goto fail;

	TAILQ_INSERT_TAIL(head, lv, link);

	return (lv);

  fail:
	if (lv)
		free(lv);

	return (NULL);
}

struct dhcp6_event *
dhcp6_create_event(ifp, state)
	struct dhcp6_if *ifp;
	int state;
{
	struct dhcp6_event *ev;

	if ((ev = malloc(sizeof(*ev))) == NULL) {
		dprintf(LOG_ERR, FNAME,
		    "failed to allocate memory for an event");
		return (NULL);
	}
	memset(ev, 0, sizeof(*ev));
	ev->ifp = ifp;
	ev->state = state;
	TAILQ_INIT(&ev->data_list);

	return (ev);
}

void
dhcp6_remove_event(ev)
	struct dhcp6_event *ev;
{
	struct dhcp6_serverinfo *sp, *sp_next;

	dprintf(LOG_DEBUG, FNAME, "removing an event on %s, state=%s",
	    ev->ifp->ifname, dhcp6_event_statestr(ev));

	dhcp6_remove_evdata(ev);

	duidfree(&ev->serverid);

	if (ev->timer)
		dhcp6_remove_timer(&ev->timer);
	TAILQ_REMOVE(&ev->ifp->event_list, ev, link);

	for (sp = ev->servers; sp; sp = sp_next) {
		sp_next = sp->next;

		dprintf(LOG_DEBUG, FNAME, "removing server (ID: %s)",
		    duidstr(&sp->optinfo.serverID));
		dhcp6_clear_options(&sp->optinfo);
		free(sp);
	}

	free(ev);
}

void
dhcp6_remove_evdata(ev)
	struct dhcp6_event *ev;
{
	struct dhcp6_eventdata *evd;

	while ((evd = TAILQ_FIRST(&ev->data_list)) != NULL) {
		TAILQ_REMOVE(&ev->data_list, evd, link);
		if (evd->destructor)
			(*evd->destructor)(evd);
		else
			free(evd);
	}
}

#if 0
static unsigned int
if_maxindex()
{
	struct if_nameindex *p, *p0;
	unsigned int max = 0;

	p0 = if_nameindex();
	for (p = p0; p && p->if_index && p->if_name; p++) {
		if (max < p->if_index)
			max = p->if_index;
	}
	if_freenameindex(p0);
	return (max);
}
#endif

int
getifaddr(addr, ifnam, prefix, plen, strong, ignoreflags)
	struct in6_addr *addr;
	char *ifnam;
	struct in6_addr *prefix;
	int plen;
	int strong;		/* if strong host model is required or not */
	int ignoreflags;
{
	struct ifaddrs *ifap, *ifa;
	struct sockaddr_in6 sin6;
	int error = -1;

	if (getifaddrs(&ifap) != 0) {
		err(1, "getifaddr: getifaddrs");
		/*NOTREACHED*/
	}

	for (ifa = ifap; ifa; ifa = ifa->ifa_next) {
		int s1, s2;

		if (strong && strcmp(ifnam, ifa->ifa_name) != 0)
			continue;

		/* in any case, ignore interfaces in different scope zones. */
		if ((s1 = in6_addrscopebyif(prefix, ifnam)) < 0 ||
		    (s2 = in6_addrscopebyif(prefix, ifa->ifa_name)) < 0 ||
		     s1 != s2)
			continue;

		if (ifa->ifa_addr->sa_family != AF_INET6)
			continue;
		if (ifa->ifa_addr->sa_len > sizeof(sin6))
			continue;

		if (in6_matchflags(ifa->ifa_addr, ifa->ifa_name, ignoreflags))
			continue;

		memcpy(&sin6, ifa->ifa_addr, ifa->ifa_addr->sa_len);
#ifdef __KAME__
		if (IN6_IS_ADDR_LINKLOCAL(&sin6.sin6_addr)) {
			sin6.sin6_addr.s6_addr[2] = 0;
			sin6.sin6_addr.s6_addr[3] = 0;
		}
#endif
		if (plen % 8 == 0) {
			if (memcmp(&sin6.sin6_addr, prefix, plen / 8) != 0)
				continue;
		} else {
			struct in6_addr a, m;
			int i;

			memcpy(&a, &sin6.sin6_addr, sizeof(sin6.sin6_addr));
			memset(&m, 0, sizeof(m));
			memset(&m, 0xff, plen / 8);
			m.s6_addr[plen / 8] = (0xff00 >> (plen % 8)) & 0xff;
			for (i = 0; i < sizeof(a); i++)
				a.s6_addr[i] &= m.s6_addr[i];

			if (memcmp(&a, prefix, plen / 8) != 0 ||
			    a.s6_addr[plen / 8] !=
			    (prefix->s6_addr[plen / 8] & m.s6_addr[plen / 8]))
				continue;
		}
		memcpy(addr, &sin6.sin6_addr, sizeof(sin6.sin6_addr));
#ifdef __KAME__
		if (IN6_IS_ADDR_LINKLOCAL(addr))
			addr->s6_addr[2] = addr->s6_addr[3] = 0; 
#endif
		error = 0;
		break;
	}

	freeifaddrs(ifap);
	return (error);
}

int
in6_addrscopebyif(addr, ifnam)
	struct in6_addr *addr;
	char *ifnam;
{
	u_int ifindex; 

	if ((ifindex = if_nametoindex(ifnam)) == 0)
		return (-1);

	if (IN6_IS_ADDR_LINKLOCAL(addr) || IN6_IS_ADDR_MC_LINKLOCAL(addr))
		return (ifindex);

	if (IN6_IS_ADDR_SITELOCAL(addr) || IN6_IS_ADDR_MC_SITELOCAL(addr))
		return (1);	/* XXX */

	if (IN6_IS_ADDR_MC_ORGLOCAL(addr))
		return (1);	/* XXX */

	return (1);		/* treat it as global */
}

/* XXX: this code assumes getifaddrs(3) */
const char *
getdev(addr)
	struct sockaddr_in6 *addr;
{
	struct ifaddrs *ifap, *ifa;
	struct sockaddr_in6 *a6;
	static char ret_ifname[IF_NAMESIZE];

	if (getifaddrs(&ifap) != 0) {
		err(1, "getdev: getifaddrs");
		/* NOTREACHED */
	}

	for (ifa = ifap; ifa; ifa = ifa->ifa_next) {
		if (ifa->ifa_addr->sa_family != AF_INET6)
			continue;

		a6 = (struct sockaddr_in6 *)ifa->ifa_addr;
		if (!IN6_ARE_ADDR_EQUAL(&a6->sin6_addr, &addr->sin6_addr) ||
		    a6->sin6_scope_id != addr->sin6_scope_id)
			continue;

		break;
	}

	if (ifa)
		strlcpy(ret_ifname, ifa->ifa_name, sizeof(ret_ifname));
	freeifaddrs(ifap);

	return (ifa ? ret_ifname : NULL);
}

int
transmit_sa(s, sa, buf, len)
	int s;
	struct sockaddr *sa;
	char *buf;
	size_t len;
{
	int error;

	error = sendto(s, buf, len, 0, sa, sa->sa_len);

	return (error != len) ? -1 : 0;
}

long
random_between(x, y)
	long x;
	long y;
{
	long ratio;

	ratio = 1 << 16;
	while ((y - x) * ratio < (y - x))
		ratio = ratio / 2;
	return (x + ((y - x) * (ratio - 1) / random() & (ratio - 1)));
}

int
prefix6_mask(in6, plen)
	struct in6_addr *in6;
	int plen;
{
	struct sockaddr_in6 mask6;
	int i;

	if (sa6_plen2mask(&mask6, plen))
		return (-1);

	for (i = 0; i < 16; i++)
		in6->s6_addr[i] &= mask6.sin6_addr.s6_addr[i];

	return (0);
}

int
sa6_plen2mask(sa6, plen)
	struct sockaddr_in6 *sa6;
	int plen;
{
	u_char *cp;

	if (plen < 0 || plen > 128)
		return (-1);

	memset(sa6, 0, sizeof(*sa6));
	sa6->sin6_family = AF_INET6;
	sa6->sin6_len = sizeof(*sa6);
	
	for (cp = (u_char *)&sa6->sin6_addr; plen > 7; plen -= 8)
		*cp++ = 0xff;
	*cp = 0xff << (8 - plen);

	return (0);
}

char *
addr2str(sa)
	struct sockaddr *sa;
{
	static char addrbuf[8][NI_MAXHOST];
	static int round = 0;
	char *cp;

	round = (round + 1) & 7;
	cp = addrbuf[round];

	getnameinfo(sa, sa->sa_len, cp, NI_MAXHOST, NULL, 0, NI_NUMERICHOST);

	return (cp);
}

char *
in6addr2str(in6, scopeid)
	struct in6_addr *in6;
	int scopeid;
{
	struct sockaddr_in6 sa6;

	memset(&sa6, 0, sizeof(sa6));
	sa6.sin6_family = AF_INET6;
	sa6.sin6_len = sizeof(sa6);
	sa6.sin6_addr = *in6;
	sa6.sin6_scope_id = scopeid;

	return (addr2str((struct sockaddr *)&sa6));
}

/* return IPv6 address scope type. caller assumes that smaller is narrower. */
int
in6_scope(addr)
	struct in6_addr *addr;
{
	int scope;

	if (addr->s6_addr[0] == 0xfe) {
		scope = addr->s6_addr[1] & 0xc0;

		switch (scope) {
		case 0x80:
			return (2); /* link-local */
			break;
		case 0xc0:
			return (5); /* site-local */
			break;
		default:
			return (14); /* global: just in case */
			break;
		}
	}

	/* multicast scope. just return the scope field */
	if (addr->s6_addr[0] == 0xff)
		return (addr->s6_addr[1] & 0x0f);

	if (bcmp(&in6addr_loopback, addr, sizeof(addr) - 1) == 0) {
		if (addr->s6_addr[15] == 1) /* loopback */
			return (1);
		if (addr->s6_addr[15] == 0) /* unspecified */
			return (0); /* XXX: good value? */
	}

	return (14);		/* global */
}

static int
in6_matchflags(addr, ifnam, flags)
	struct sockaddr *addr;
	char *ifnam;
	int flags;
{
	int s;
	struct in6_ifreq ifr6;

	if ((s = socket(AF_INET6, SOCK_DGRAM, 0)) < 0) {
		warn("in6_matchflags: socket(DGRAM6)");
		return (-1);
	}
	memset(&ifr6, 0, sizeof(ifr6));
	strncpy(ifr6.ifr_name, ifnam, sizeof(ifr6.ifr_name));
	ifr6.ifr_addr = *(struct sockaddr_in6 *)addr;

	if (ioctl(s, SIOCGIFAFLAG_IN6, &ifr6) < 0) {
		warn("in6_matchflags: ioctl(SIOCGIFAFLAG_IN6, %s)",
		     addr2str(addr));
		close(s);
		return (-1);
	}

	close(s);

	return (ifr6.ifr_ifru.ifru_flags6 & flags);
}

int
get_duid(idfile, duid)
	char *idfile;
	struct duid *duid;
{
	FILE *fp = NULL;
	u_int16_t len = 0, hwtype;
	struct dhcp6opt_duid_type1 *dp; /* we only support the type1 DUID */
	char tmpbuf[256];	/* DUID should be no more than 256 bytes */

	if ((fp = fopen(idfile, "r")) == NULL && errno != ENOENT)
		dprintf(LOG_NOTICE, FNAME, "failed to open DUID file: %s",
		    idfile);

	if (fp) {
		/* decode length */
		if (fread(&len, sizeof(len), 1, fp) != 1) {
			dprintf(LOG_ERR, FNAME, "DUID file corrupted");
			goto fail;
		}
	} else {
		int l;

		if ((l = gethwid(tmpbuf, sizeof(tmpbuf), NULL, &hwtype)) < 0) {
			dprintf(LOG_INFO, FNAME,
			    "failed to get a hardware address");
			goto fail;
		}
		len = l + sizeof(struct dhcp6opt_duid_type1);
	}

	memset(duid, 0, sizeof(*duid));
	duid->duid_len = len;
	if ((duid->duid_id = (char *)malloc(len)) == NULL) {
		dprintf(LOG_ERR, FNAME, "failed to allocate memory");
		goto fail;
	}

	/* copy (and fill) the ID */
	if (fp) {
		if (fread(duid->duid_id, len, 1, fp) != 1) {
			dprintf(LOG_ERR, FNAME, "DUID file corrupted");
			goto fail;
		}

		dprintf(LOG_DEBUG, FNAME,
		    "extracted an existing DUID from %s: %s",
		    idfile, duidstr(duid));
	} else {
		u_int64_t t64;

		dp = (struct dhcp6opt_duid_type1 *)duid->duid_id;
		dp->dh6_duid1_type = htons(1); /* type 1 */
		dp->dh6_duid1_hwtype = htons(hwtype);
		/* time is Jan 1, 2000 (UTC), modulo 2^32 */
		t64 = (u_int64_t)(time(NULL) - 946684800);
		dp->dh6_duid1_time = htonl((u_long)(t64 & 0xffffffff));
		memcpy((void *)(dp + 1), tmpbuf, (len - sizeof(*dp)));

		dprintf(LOG_DEBUG, FNAME, "generated a new DUID: %s",
		    duidstr(duid));
	}

	/* save the (new) ID to the file for next time */
	if (!fp) {
		if ((fp = fopen(idfile, "w+")) == NULL) {
			dprintf(LOG_ERR, FNAME,
			    "failed to open DUID file for save");
			goto fail;
		}
		if ((fwrite(&len, sizeof(len), 1, fp)) != 1) {
			dprintf(LOG_ERR, FNAME, "failed to save DUID");
			goto fail;
		}
		if ((fwrite(duid->duid_id, len, 1, fp)) != 1) {
			dprintf(LOG_ERR, FNAME, "failed to save DUID");
			goto fail;
		}

		dprintf(LOG_DEBUG, FNAME, "saved generated DUID to %s",
		    idfile);
	}

	if (fp)
		fclose(fp);
	return (0);

  fail:
	if (fp)
		fclose(fp);
	if (duid->duid_id) {
		free(duid->duid_id);
		duid->duid_id = NULL; /* for safety */
	}
	return (-1);
}

static ssize_t
gethwid(buf, len, ifname, hwtypep)
	char *buf;
	int len;
	const char *ifname;
	u_int16_t *hwtypep;
{
	struct ifaddrs *ifa, *ifap;
	struct sockaddr_dl *sdl;
	ssize_t l;

	if (getifaddrs(&ifap) < 0)
		return (-1);

	for (ifa = ifap; ifa; ifa = ifa->ifa_next) {
		if (ifname && strcmp(ifa->ifa_name, ifname) != 0)
			continue;
		if (ifa->ifa_addr->sa_family != AF_LINK)
			continue;

		sdl = (struct sockaddr_dl *)ifa->ifa_addr;
		if (len < 2 + sdl->sdl_alen)
			goto fail;
		/*
		 * translate interface type to hardware type based on
		 * http://www.iana.org/assignments/arp-parameters
		 */
		switch(sdl->sdl_type) {
		case IFT_ETHER:
#ifdef IFT_IEEE80211
		case IFT_IEEE80211:
#endif
			*hwtypep = ARPHRD_ETHER;
			break;
		default:
			continue; /* XXX */
		}
		dprintf(LOG_DEBUG, FNAME, "found an interface %s for DUID",
		    ifa->ifa_name);
		memcpy(buf, LLADDR(sdl), sdl->sdl_alen);
		l = sdl->sdl_alen; /* sdl will soon be freed */
		freeifaddrs(ifap);
		return (l);
	}

  fail:
	freeifaddrs(ifap);
	return (-1);
}

void
dhcp6_init_options(optinfo)
	struct dhcp6_optinfo *optinfo;
{
	memset(optinfo, 0, sizeof(*optinfo));

	optinfo->pref = DH6OPT_PREF_UNDEF;
	optinfo->elapsed_time = DH6OPT_ELAPSED_TIME_UNDEF;

	TAILQ_INIT(&optinfo->iapd_list);
	TAILQ_INIT(&optinfo->reqopt_list);
	TAILQ_INIT(&optinfo->stcode_list);
	TAILQ_INIT(&optinfo->dns_list);
	TAILQ_INIT(&optinfo->prefix_list);
}

void
dhcp6_clear_options(optinfo)
	struct dhcp6_optinfo *optinfo;
{

	duidfree(&optinfo->clientID);
	duidfree(&optinfo->serverID);

	dhcp6_clear_list(&optinfo->iapd_list);
	dhcp6_clear_list(&optinfo->reqopt_list);
	dhcp6_clear_list(&optinfo->stcode_list);
	dhcp6_clear_list(&optinfo->dns_list);
	dhcp6_clear_list(&optinfo->prefix_list);

	if (optinfo->relaymsg_msg)
		free(optinfo->relaymsg_msg);

	if (optinfo->ifidopt_id)
		free(optinfo->ifidopt_id);

	dhcp6_init_options(optinfo);
}

int
dhcp6_copy_options(dst, src)
	struct dhcp6_optinfo *dst, *src;
{
	if (duidcpy(&dst->clientID, &src->clientID))
		goto fail;
	if (duidcpy(&dst->serverID, &src->serverID))
		goto fail;
	dst->rapidcommit = src->rapidcommit;

	if (dhcp6_copy_list(&dst->iapd_list, &src->iapd_list))
		goto fail;
	if (dhcp6_copy_list(&dst->reqopt_list, &src->reqopt_list))
		goto fail;
	if (dhcp6_copy_list(&dst->stcode_list, &src->stcode_list))
		goto fail;
	if (dhcp6_copy_list(&dst->dns_list, &src->dns_list))
		goto fail;
	if (dhcp6_copy_list(&dst->prefix_list, &src->prefix_list))
		goto fail;
	dst->pref = src->pref;

	if (src->relaymsg_msg) {
		if ((dst->relaymsg_msg = malloc(src->relaymsg_len)) == NULL)
			goto fail;
		dst->relaymsg_len = src->relaymsg_len;
		memcpy(dst->relaymsg_msg, src->relaymsg_msg,
		    src->relaymsg_len);
	}

	if (src->ifidopt_id) {
		if ((dst->ifidopt_id = malloc(src->ifidopt_len)) == NULL)
			goto fail;
		dst->ifidopt_len = src->ifidopt_len;
		memcpy(dst->ifidopt_id, src->ifidopt_id, src->ifidopt_len);
	}

	return (0);

  fail:
	/* cleanup temporary resources */
	dhcp6_clear_options(dst);
	return (-1);
}

int
dhcp6_get_options(p, ep, optinfo)
	struct dhcp6opt *p, *ep;
	struct dhcp6_optinfo *optinfo;
{
	struct dhcp6opt *np, opth;
	int i, opt, optlen, reqopts;
	u_int16_t num;
	char *cp, *val;
	u_int16_t val16;
	struct dhcp6opt_ia optia;
	struct dhcp6_ia ia;
	struct dhcp6_list sublist;

	for (; p + 1 <= ep; p = np) {
		struct duid duid0;

		/*
		 * get the option header.  XXX: since there is no guarantee
		 * about the header alignment, we need to make a local copy.
		 */
		memcpy(&opth, p, sizeof(opth));
		optlen = ntohs(opth.dh6opt_len);
		opt = ntohs(opth.dh6opt_type);

		cp = (char *)(p + 1);
		np = (struct dhcp6opt *)(cp + optlen);

		dprintf(LOG_DEBUG, FNAME, "get DHCP option %s, len %d",
		    dhcp6optstr(opt), optlen);

		/* option length field overrun */
		if (np > ep) {
			dprintf(LOG_INFO, FNAME, "malformed DHCP options");
			return (-1);
		}

		switch (opt) {
		case DH6OPT_CLIENTID:
			if (optlen == 0)
				goto malformed;
			duid0.duid_len = optlen;
			duid0.duid_id = cp;
			dprintf(LOG_DEBUG, "  DUID: %s", duidstr(&duid0));
			if (duidcpy(&optinfo->clientID, &duid0)) {
				dprintf(LOG_ERR, FNAME, "failed to copy DUID");
				goto fail;
			}
			break;
		case DH6OPT_SERVERID:
			if (optlen == 0)
				goto malformed;
			duid0.duid_len = optlen;
			duid0.duid_id = cp;
			dprintf(LOG_DEBUG, "  DUID: %s", duidstr(&duid0));
			if (duidcpy(&optinfo->serverID, &duid0)) {
				dprintf(LOG_ERR, FNAME, "failed to copy DUID");
				goto fail;
			}
			break;
		case DH6OPT_STATUS_CODE:
			if (optlen < sizeof(u_int16_t))
				goto malformed;
			memcpy(&val16, cp, sizeof(val16));
			num = ntohs(val16);
			dprintf(LOG_DEBUG, "  status code: %s",
			    dhcp6_stcodestr(num));

			/* need to check duplication? */

			if (dhcp6_add_listval(&optinfo->stcode_list,
			    DHCP6_LISTVAL_STCODE, &num, NULL) == NULL) {
				dprintf(LOG_ERR, FNAME, "failed to copy "
				    "status code");
				goto fail;
			}

			break;
		case DH6OPT_ORO:
			if ((optlen % 2) != 0 || optlen == 0)
				goto malformed;
			reqopts = optlen / 2;
			for (i = 0, val = cp; i < reqopts;
			     i++, val += sizeof(u_int16_t)) {
				u_int16_t opttype;

				memcpy(&opttype, val, sizeof(u_int16_t));
				num = ntohs(opttype);

				dprintf(LOG_DEBUG, "  requested option: %s",
					dhcp6optstr(num));

				if (dhcp6_find_listval(&optinfo->reqopt_list,
				    DHCP6_LISTVAL_NUM, &num, 0)) {
					dprintf(LOG_INFO, FNAME, "duplicated "
					    "option type (%s)",
					    dhcp6optstr(opttype));
					goto nextoption;
				}

				if (dhcp6_add_listval(&optinfo->reqopt_list,
				    DHCP6_LISTVAL_NUM, &num, NULL) == NULL) {
					dprintf(LOG_ERR, FNAME,
					    "failed to copy requested option");
					goto fail;
				}
			  nextoption:
			}
			break;
		case DH6OPT_PREFERENCE:
			if (optlen != 1)
				goto malformed;
			dprintf(LOG_DEBUG, "", "  preference: %d",
			    (int)*(u_char *)cp);
			if (optinfo->pref != DH6OPT_PREF_UNDEF) {
				dprintf(LOG_INFO, FNAME,
				    "duplicated preference option");
			} else
				optinfo->pref = (int)*(u_char *)cp;
			break;
		case DH6OPT_ELAPSED_TIME:
			if (optlen != 2)
				goto malformed;
			memcpy(&val16, cp, sizeof(val16));
			val16 = ntohs(val16);
			dprintf(LOG_DEBUG, "", "  elapsed time: %lu",
			    (u_int32_t)val16);
			if (optinfo->elapsed_time !=
			    DH6OPT_ELAPSED_TIME_UNDEF) {
				dprintf(LOG_INFO, FNAME,
				    "duplicated elapsed time option");
			} else
				optinfo->elapsed_time = val16;
			break;
		case DH6OPT_RAPID_COMMIT:
			if (optlen != 0)
				goto malformed;
			optinfo->rapidcommit = 1;
			break;
		case DH6OPT_DNS:
			if (optlen % sizeof(struct in6_addr) || optlen == 0)
				goto malformed;
			for (val = cp; val < cp + optlen;
			     val += sizeof(struct in6_addr)) {
				if (dhcp6_find_listval(&optinfo->dns_list,
				    DHCP6_LISTVAL_ADDR6, &num, 0)) {
					dprintf(LOG_INFO, FNAME, "duplicated "
					    "DNS address (%s)",
					    in6addr2str((struct in6_addr *)val,
						0));
					goto nextdns;
				}

				if (dhcp6_add_listval(&optinfo->dns_list,
				    DHCP6_LISTVAL_ADDR6, val, NULL) == NULL) {
					dprintf(LOG_ERR, FNAME,
					    "failed to copy DNS address");
					goto fail;
				}
			  nextdns:
			}
			break;
		case DH6OPT_IA_PD:
			if (optlen + sizeof(struct dhcp6opt) <
			    sizeof(optia))
				goto malformed;
			memcpy(&optia, p, sizeof(optia));
			ia.iaid = ntohl(optia.dh6_ia_iaid);
			ia.t1 = ntohl(optia.dh6_ia_t1);
			ia.t2 = ntohl(optia.dh6_ia_t2);

			dprintf(LOG_DEBUG, "",
			    "  IA_PD: ID=%lu, T1=%lu, T2=%lu",
			    ia.iaid, ia.t1, ia.t2);

			/* duplication check */
			if (dhcp6_find_listval(&optinfo->iapd_list,
			    DHCP6_LISTVAL_IAPD, &ia, 0)) {
				dprintf(LOG_INFO, FNAME,
				    "duplicated IA_PD %lu", ia.iaid);
				break; /* ignore this IA_PD */
			}

			/* take care of sub-options */
			TAILQ_INIT(&sublist);
			if (copyin_option(opt,
			    (struct dhcp6opt *)((char *)p + sizeof(optia)),
			    (struct dhcp6opt *)(cp + optlen), &sublist)) {
				goto fail;
			}

			/* link this option set */
			if (dhcp6_add_listval(&optinfo->iapd_list,
			    DHCP6_LISTVAL_IAPD, &ia, &sublist) == NULL) {
				dhcp6_clear_list(&sublist);
				goto fail;
			}
			dhcp6_clear_list(&sublist);

			break;
		case DH6OPT_PREFIX_DELEGATION:
			if (get_delegated_prefixes(cp, cp + optlen, optinfo))
				goto fail;
			break;
		default:
			/* no option specific behavior */
			dprintf(LOG_INFO, FNAME,
			    "unknown or unexpected DHCP6 option %s, len %d",
			    dhcp6optstr(opt), optlen);
			break;
		}
	}

	return (0);

  malformed:
	dprintf(LOG_INFO, FNAME, "malformed DHCP option: type %d, len %d",
	    opt, optlen);
  fail:
	dhcp6_clear_options(optinfo);
	return (-1);
}

static int
copyin_option(type, p, ep, list)
	int type;
	struct dhcp6opt *p, *ep;
	struct dhcp6_list *list;
{
	int opt, optlen;
	char *cp;
	struct dhcp6opt *np, opth;
	struct dhcp6opt_stcode opt_stcode;
	struct dhcp6opt_ia_pd_prefix opt_iapd_prefix;
	struct dhcp6_prefix iapd_prefix;
	struct dhcp6_list sublist;

	TAILQ_INIT(&sublist);

	for (; p + 1 <= ep; p = np) {
		memcpy(&opth, p, sizeof(opth));
		optlen = ntohs(opth.dh6opt_len);
		opt = ntohs(opth.dh6opt_type);

		cp = (char *)(p + 1);
		np = (struct dhcp6opt *)(cp + optlen);

		dprintf(LOG_DEBUG, FNAME, "get DHCP option %s, len %d",
		    dhcp6optstr(opt), optlen);

		if (np > ep) {
			dprintf(LOG_INFO, FNAME, "malformed DHCP option");
			goto fail;
		}

		switch (opt) {
		case DH6OPT_IA_PD_PREFIX:
			/* check option context */
			if (type != DH6OPT_IA_PD) {
				dprintf(LOG_INFO, FNAME,
				    "%s is an invalid position for %s",
				    dhcp6optstr(type), dhcp6optstr(opt));
				goto nextoption; /* or discard the message? */
			}
			/* check option length */
			if (optlen + sizeof(opth) < sizeof(opt_iapd_prefix))
				goto malformed;

			/* copy and convert option values */
			memcpy(&opt_iapd_prefix, p, sizeof(opt_iapd_prefix));
			if (opt_iapd_prefix.dh6_iapd_prefix_prefix_len > 128) {
				dprintf(LOG_INFO, FNAME,
				    "invalid prefix length (%d)",
				    opt_iapd_prefix.dh6_iapd_prefix_prefix_len);
				goto malformed;
			}
			iapd_prefix.pltime = ntohl(opt_iapd_prefix.dh6_iapd_prefix_preferred_time);
			iapd_prefix.vltime = ntohl(opt_iapd_prefix.dh6_iapd_prefix_valid_time);
			iapd_prefix.plen =
			    opt_iapd_prefix.dh6_iapd_prefix_prefix_len;
			memcpy(&iapd_prefix.addr,
			    &opt_iapd_prefix.dh6_iapd_prefix_prefix_addr,
			    sizeof(iapd_prefix.addr));
			/* clear padding bits in the prefix address */
			prefix6_mask(&iapd_prefix.addr, iapd_prefix.plen);

			dprintf(LOG_DEBUG, "  IA_PD prefix: "
			    "%s/%d pltime=%lu vltime=%lu",
			    in6addr2str(&iapd_prefix.addr, 0),
			    iapd_prefix.plen,
			    iapd_prefix.pltime, iapd_prefix.vltime);

			if (dhcp6_find_listval(list, DHCP6_LISTVAL_PREFIX6,
			    &iapd_prefix, 0)) {
				dprintf(LOG_INFO, FNAME, 
				    "duplicated IA_PD prefix %s/%d",
				    iapd_prefix.pltime, iapd_prefix.vltime);
				goto nextoption;
			}

			/* take care of sub-options */
			TAILQ_INIT(&sublist);
			if (copyin_option(opt,
			    (struct dhcp6opt *)(char *)p +
			    sizeof(opt_iapd_prefix),
			    ep, &sublist)) {
				goto fail;
			}

			if (dhcp6_add_listval(list, DHCP6_LISTVAL_PREFIX6,
			    &iapd_prefix, &sublist) == NULL) {
				dhcp6_clear_list(&sublist);
				goto fail;
			}
			dhcp6_clear_list(&sublist);
			break;
		case DH6OPT_STATUS_CODE:
			/* check option context */
			if (type != DH6OPT_IA_PD &&
			    type != DH6OPT_IA_PD_PREFIX) {
				dprintf(LOG_INFO, FNAME,
				    "%s is an invalid position for %s",
				    dhcp6optstr(type), dhcp6optstr(opt));
				goto nextoption; /* or discard the message? */
			}
			/* check option length */
			if (optlen + sizeof(opth) < sizeof(opt_stcode))
				goto malformed;

			/* copy and convert option values */
			memcpy(&opt_stcode, p, sizeof(opt_stcode));
			opt_stcode.dh6_stcode_code =
			    ntohs(opt_stcode.dh6_stcode_code);

			dprintf(LOG_DEBUG, "  status code: %s",
			    dhcp6_stcodestr(opt_stcode.dh6_stcode_code));

			/* duplication check */
			if (dhcp6_find_listval(list, DHCP6_LISTVAL_STCODE,
			    &opt_stcode.dh6_stcode_code, 0)) {
				dprintf(LOG_INFO, FNAME,
				    "duplicated status code (%d)",
				    opt_stcode.dh6_stcode_code);
				goto nextoption;
			}

			/* copy-in the code value */
			if (dhcp6_add_listval(list, DHCP6_LISTVAL_STCODE,
			    &opt_stcode.dh6_stcode_code, NULL) == NULL)
				goto fail;

			break;
		}
	  nextoption:
	}

	return (0);

  malformed:
	dprintf(LOG_INFO, "", "  malformed DHCP option: type %d", opt);

  fail:
	dhcp6_clear_list(&sublist);
	return (-1);
}

static int
get_delegated_prefixes(p, ep, optinfo)
	char *p, *ep;
	struct dhcp6_optinfo *optinfo;
{
	char *np, *cp;
	struct dhcp6opt opth;
	struct dhcp6opt_prefix_info pi;
	struct dhcp6_prefix prefix;
	int optlen, opt;

	for (; p + sizeof(struct dhcp6opt) <= ep; p = np) {
		/* XXX: alignment issue */
		memcpy(&opth, p, sizeof(opth));
		optlen =  ntohs(opth.dh6opt_len);
		opt = ntohs(opth.dh6opt_type);

		cp = p + sizeof(opth);
		np = cp + optlen;
		dprintf(LOG_DEBUG, "  prefix delegation option: %s, "
			"len %d", dhcp6optstr(opt), optlen);

		if (np > ep) {
			dprintf(LOG_INFO, FNAME, "malformed DHCP options");
			return (-1);
		}

		switch(opt) {
		case DH6OPT_PREFIX_INFORMATION:
			if (optlen != sizeof(pi) - 4)
				goto malformed;

			memcpy(&pi, p, sizeof(pi));

			if (pi.dh6_pi_plen > 128) {
				dprintf(LOG_INFO, FNAME,
				    "invalid prefix length (%d)",
				    pi.dh6_pi_plen);
				goto malformed;
			}

			/* clear padding bits in the prefix address */
			prefix6_mask(&pi.dh6_pi_paddr, pi.dh6_pi_plen);

			/* copy the information into internal format */
			memset(&prefix, 0, sizeof(prefix));
			prefix.addr = pi.dh6_pi_paddr;
			prefix.plen = pi.dh6_pi_plen;
			/* XXX */
			prefix.vltime = ntohl(pi.dh6_pi_duration);
			prefix.pltime = ntohl(pi.dh6_pi_duration);

			if (prefix.vltime != DHCP6_DURATITION_INFINITE) {
				dprintf(LOG_DEBUG, "  prefix information: "
				    "%s/%d duration %lu",
				    in6addr2str(&prefix.addr, 0),
				    prefix.plen, prefix.vltime);
			} else {
				dprintf(LOG_DEBUG, "  prefix information: "
				    "%s/%d duration infinity",
				    in6addr2str(&prefix.addr, 0),
				    prefix.plen);
			}

			if (dhcp6_find_listval(&optinfo->prefix_list,
			    DHCP6_LISTVAL_PREFIX6, &prefix, 0)) {
				dprintf(LOG_INFO, FNAME, 
				    "duplicated prefix (%s/%d)",
				    in6addr2str(&prefix.addr, 0),
				    prefix.plen);
				goto nextoption;
			}

			if (dhcp6_add_listval(&optinfo->prefix_list,
			    DHCP6_LISTVAL_PREFIX6, &prefix, NULL) == NULL) {
				dprintf(LOG_ERR, FNAME,
				    "failed to copy a prefix");
				goto fail;
			}
		}

	  nextoption:
	}

	return (0);

  malformed:
	dprintf(LOG_INFO,
		"", "  malformed prefix delegation option: type %d, len %d",
		opt, optlen);
  fail:
	return (-1);
}

#define COPY_OPTION(t, l, v, p) do { \
	if ((void *)(ep) - (void *)(p) < (l) + sizeof(struct dhcp6opt)) { \
		dprintf(LOG_INFO, FNAME, "option buffer short for %s", dhcp6optstr((t))); \
		goto fail; \
	} \
	opth.dh6opt_type = htons((t)); \
	opth.dh6opt_len = htons((l)); \
	memcpy((p), &opth, sizeof(opth)); \
	if ((l)) \
		memcpy((p) + 1, (v), (l)); \
	(p) = (struct dhcp6opt *)((char *)((p) + 1) + (l)); \
 	(len) += sizeof(struct dhcp6opt) + (l); \
	dprintf(LOG_DEBUG, FNAME, "set %s (len %d)", dhcp6optstr((t)), (l)); \
} while (0)

int
dhcp6_set_options(bp, ep, optinfo)
	struct dhcp6opt *bp, *ep;
	struct dhcp6_optinfo *optinfo;
{
	struct dhcp6opt *p = bp, opth;
	struct dhcp6_listval *stcode, *op;
	int len = 0, optlen;
	char *tmpbuf = NULL;

	if (optinfo->clientID.duid_len) {
		COPY_OPTION(DH6OPT_CLIENTID, optinfo->clientID.duid_len,
			    optinfo->clientID.duid_id, p);
	}

	if (optinfo->serverID.duid_len) {
		COPY_OPTION(DH6OPT_SERVERID, optinfo->serverID.duid_len,
			    optinfo->serverID.duid_id, p);
	}

	if (optinfo->rapidcommit)
		COPY_OPTION(DH6OPT_RAPID_COMMIT, 0, NULL, p);

	if (optinfo->pref != DH6OPT_PREF_UNDEF) {
		u_int8_t p8 = (u_int8_t)optinfo->pref;

		COPY_OPTION(DH6OPT_PREFERENCE, sizeof(p8), &p8, p);
	}

	if (optinfo->elapsed_time != DH6OPT_ELAPSED_TIME_UNDEF) {
		u_int16_t p16 = (u_int16_t)optinfo->elapsed_time;

		p16 = htons(p16);
		COPY_OPTION(DH6OPT_ELAPSED_TIME, sizeof(p16), &p16, p);
	}

	for (stcode = TAILQ_FIRST(&optinfo->stcode_list); stcode;
	     stcode = TAILQ_NEXT(stcode, link)) {
		u_int16_t code;

		code = htons(stcode->val_num);
		COPY_OPTION(DH6OPT_STATUS_CODE, sizeof(code), &code, p);
	}

	if (!TAILQ_EMPTY(&optinfo->reqopt_list)) {
		struct dhcp6_listval *opt;
		u_int16_t *valp;

		tmpbuf = NULL;
		optlen = dhcp6_count_list(&optinfo->reqopt_list) *
			sizeof(u_int16_t);
		if ((tmpbuf = malloc(optlen)) == NULL) {
			dprintf(LOG_ERR, FNAME,
			    "memory allocation failed for options");
			goto fail;
		}
		valp = (u_int16_t *)tmpbuf;
		for (opt = TAILQ_FIRST(&optinfo->reqopt_list); opt;
		     opt = TAILQ_NEXT(opt, link), valp++) {
			*valp = htons((u_int16_t)opt->val_num);
		}
		COPY_OPTION(DH6OPT_ORO, optlen, tmpbuf, p);
		free(tmpbuf);
	}

	if (!TAILQ_EMPTY(&optinfo->dns_list)) {
		struct in6_addr *in6;
		struct dhcp6_listval *d;

		tmpbuf = NULL;
		optlen = dhcp6_count_list(&optinfo->dns_list) *
			sizeof(struct in6_addr);
		if ((tmpbuf = malloc(optlen)) == NULL) {
			dprintf(LOG_ERR, FNAME,
			    "memory allocation failed for DNS options");
			goto fail;
		}
		in6 = (struct in6_addr *)tmpbuf;
		for (d = TAILQ_FIRST(&optinfo->dns_list); d;
		     d = TAILQ_NEXT(d, link), in6++) {
			memcpy(in6, &d->val_addr6, sizeof(*in6));
		}
		COPY_OPTION(DH6OPT_DNS, optlen, tmpbuf, p);
		free(tmpbuf);
	}

	for (op = TAILQ_FIRST(&optinfo->iapd_list); op;
	    op = TAILQ_NEXT(op, link)) {
		int optlen;

		tmpbuf = NULL;
		if ((optlen = copyout_option(NULL, NULL, op)) < 0) {
			dprintf(LOG_INFO, FNAME,
			    "failed to count option length");
			goto fail;
		}
		if ((void *)ep - (void *)p < optlen) {
			dprintf(LOG_INFO, FNAME, "short buffer");
			goto fail;
		}
		if ((tmpbuf = malloc(optlen)) == NULL) {
			dprintf(LOG_NOTICE, FNAME,
			    "memory allocation failed for DNS options");
			goto fail;
		}
		if (copyout_option(tmpbuf, tmpbuf + optlen, op) < 0) {
			dprintf(LOG_ERR, FNAME,
			    "failed to construct an option");
			goto fail;
		}
		memcpy(p, tmpbuf, optlen);
		free(tmpbuf);
		p = (struct dhcp6opt *)((char *)p + optlen);
		len += optlen;
	}

	if (!TAILQ_EMPTY(&optinfo->prefix_list)) {
		char *tp;
		struct dhcp6_listval *dp;
		struct dhcp6opt_prefix_info pi;

		tmpbuf = NULL;
		optlen = dhcp6_count_list(&optinfo->prefix_list) *
			sizeof(struct dhcp6opt_prefix_info);
		if ((tmpbuf = malloc(optlen)) == NULL) {
			dprintf(LOG_ERR, FNAME,
				"memory allocation failed for options");
			goto fail;
		}
		for (dp = TAILQ_FIRST(&optinfo->prefix_list), tp = tmpbuf; dp;
		     dp = TAILQ_NEXT(dp, link), tp += sizeof(pi)) {
			/*
			 * XXX: We need a temporary structure due to alignment
			 * issue.
			 */
			memset(&pi, 0, sizeof(pi));
			pi.dh6_pi_type = htons(DH6OPT_PREFIX_INFORMATION);
			pi.dh6_pi_len = htons(sizeof(pi) - 4);
			pi.dh6_pi_duration = htonl(dp->val_prefix6.vltime);
			pi.dh6_pi_plen = dp->val_prefix6.plen;
			memcpy(&pi.dh6_pi_paddr, &dp->val_prefix6.addr,
			       sizeof(struct in6_addr));
			memcpy(tp, &pi, sizeof(pi));
		}
		COPY_OPTION(DH6OPT_PREFIX_DELEGATION, optlen, tmpbuf, p);
		free(tmpbuf);
		     
	}

	if (optinfo->relaymsg_len) {
		COPY_OPTION(DH6OPT_RELAY_MSG, optinfo->relaymsg_len,
			    optinfo->relaymsg_msg, p);
	}

	if (optinfo->ifidopt_id) {
		COPY_OPTION(DH6OPT_INTERFACE_ID, optinfo->ifidopt_len,
			    optinfo->ifidopt_id, p);
	}

	return (len);

  fail:
	if (tmpbuf)
		free(tmpbuf);
	return (-1);
}
#undef COPY_OPTION

/*
 * Construct a DHCPv6 option along with sub-options in the wire format.
 * If the packet buffer is NULL, just calculate the length of the option
 * (and sub-options) so that the caller can allocate a buffer to store the
 * option(s).
 * This function basically assumes that the caller prepares enough buffer to
 * store all the options.  However, it also takes the buffer end and checks
 * the possibility of overrun for safety.
 */
static int
copyout_option(p, ep, optval)
	char *p, *ep;
	struct dhcp6_listval *optval;
{
	struct dhcp6opt *opt;
	struct dhcp6opt_stcode stcodeopt;
	struct dhcp6opt_ia ia;
	struct dhcp6opt_ia_pd_prefix pd_prefix;
	char *subp;
	struct dhcp6_listval *subov;
	int optlen, headlen, sublen, opttype;

	/* check invariant for safety */
	if (p && ep <= p)
		return (-1);

	/* first, detect the length of the option head */
	switch(optval->type) {
	case DHCP6_LISTVAL_IAPD:
		memset(&ia, 0, sizeof(ia));
		headlen = sizeof(ia);
		opttype = DH6OPT_IA_PD;
		opt = (struct dhcp6opt *)&ia;
		break;
	case DHCP6_LISTVAL_PREFIX6:
		memset(&pd_prefix, 0, sizeof(pd_prefix));
		headlen = sizeof(pd_prefix);
		opttype = DH6OPT_IA_PD_PREFIX;
		opt = (struct dhcp6opt *)&pd_prefix;
		break;
	case DHCP6_LISTVAL_STCODE:
		memset(&stcodeopt, 0, sizeof(stcodeopt));
		headlen = sizeof(stcodeopt);
		opttype = DH6OPT_STATUS_CODE;
		opt = (struct dhcp6opt *)&stcodeopt;
		break;
	default:
		/*
		 * we encounter an unknown option.  this should be an internal
		 * error.
		 */
		dprintf(LOG_ERR, FNAME, "unknown option: code %d",
		    optval->type);
		return (-1);
	}

	/* then, calculate the length of and/or fill in the sub-options */
	subp = NULL;
	sublen = 0;
	if (p)
		subp = p + headlen;
	for (subov = TAILQ_FIRST(&optval->sublist); subov;
	    subov = TAILQ_NEXT(subov, link)) {
		int s;

		if ((s = copyout_option(subp, ep, subov)) < 0)
			return (-1);
		if (p)
			subp += s;
		sublen += s;
	}

	/* finally, deal with the head part again */
	optlen = headlen + sublen;
	if (!p)
		return(optlen);

	dprintf(LOG_DEBUG, FNAME, "set %s", dhcp6optstr(opttype));
	if (ep - p < headlen) /* check it just in case */
		return (-1);

	/* fill in the common part */
	opt->dh6opt_type = htons(opttype);
	opt->dh6opt_len = htons(optlen - sizeof(struct dhcp6opt));

	/* fill in type specific fields */
	switch(optval->type) {
	case DHCP6_LISTVAL_IAPD:
		ia.dh6_ia_iaid = htonl(optval->val_ia.iaid);
		ia.dh6_ia_t1 = htonl(optval->val_ia.t1);
		ia.dh6_ia_t2 = htonl(optval->val_ia.t2);
		break;
	case DHCP6_LISTVAL_PREFIX6:
		pd_prefix.dh6_iapd_prefix_preferred_time =
		    htonl(optval->val_prefix6.pltime);
		pd_prefix.dh6_iapd_prefix_valid_time =
		    htonl(optval->val_prefix6.vltime);
		pd_prefix.dh6_iapd_prefix_prefix_len =
		    optval->val_prefix6.plen;
		pd_prefix.dh6_iapd_prefix_prefix_addr =
		    optval->val_prefix6.addr;
		break;
	case DHCP6_LISTVAL_STCODE:
		stcodeopt.dh6_stcode_code = htons(optval->val_num16);
		break;
	default:
		/*
		 * XXX: this case should be rejected at the beginning of this
		 * function.
		 */
		return (-1);
	}

	/* copyout the data (p must be non NULL at this point) */
	memcpy(p, opt, headlen);
	return (optlen);
}

void
dhcp6_set_timeoparam(ev)
	struct dhcp6_event *ev;
{
	ev->retrans = 0;
	ev->init_retrans = 0;
	ev->max_retrans_cnt = 0;
	ev->max_retrans_dur = 0;
	ev->max_retrans_time = 0;

	switch(ev->state) {
	case DHCP6S_SOLICIT:
		ev->init_retrans = SOL_TIMEOUT;
		ev->max_retrans_time = SOL_MAX_RT;
		break;
	case DHCP6S_INFOREQ:
		ev->init_retrans = INF_TIMEOUT;
		ev->max_retrans_time = INF_MAX_RT;
		break;
	case DHCP6S_REQUEST:
		ev->init_retrans = REQ_TIMEOUT;
		ev->max_retrans_time = REQ_MAX_RT;
		ev->max_retrans_cnt = REQ_MAX_RC;
		break;
	case DHCP6S_RENEW:
		ev->init_retrans = REN_TIMEOUT;
		ev->max_retrans_time = REN_MAX_RT;
		break;
	case DHCP6S_REBIND:
		ev->init_retrans = REB_TIMEOUT;
		ev->max_retrans_time = REB_MAX_RT;
		break;
	case DHCP6S_RELEASE:
		ev->init_retrans = REL_TIMEOUT;
		ev->max_retrans_cnt = REL_MAX_RC;
		break;
	default:
		dprintf(LOG_ERR, FNAME, "unexpected event state %d on %s",
		    ev->state, ev->ifp->ifname);
		exit(1);
	}
}

void
dhcp6_reset_timer(ev)
	struct dhcp6_event *ev;
{
	double n, r;
	char *statestr;
	struct timeval interval;

	switch(ev->state) {
	case DHCP6S_INIT:
		/*
		 * The first Solicit message from the client on the interface
		 * MUST be delayed by a random amount of time between
		 * 0 and SOL_MAX_DELAY.
		 * [dhcpv6-28 17.1.2]
		 */
		ev->retrans = (random() % (SOL_MAX_DELAY));
		break;
	default:
		if (ev->state == DHCP6S_SOLICIT && ev->timeouts == 0) {
			/*
			 * The first RT MUST be selected to be strictly
			 * greater than IRT by choosing RAND to be strictly
			 * greater than 0.
			 * [dhcpv6-28 17.1.2]
			 */
			r = (double)((random() % 1000) + 1) / 10000;
			n = ev->init_retrans + r * ev->init_retrans;
		} else {
			r = (double)((random() % 2000) - 1000) / 10000;

			if (ev->timeouts == 0) {
				n = ev->init_retrans + r * ev->init_retrans;
			} else
				n = 2 * ev->retrans + r * ev->retrans;
		}
		if (ev->max_retrans_time && n > ev->max_retrans_time)
			n = ev->max_retrans_time + r * ev->max_retrans_time;
		ev->retrans = (long)n;
		break;
	}

	interval.tv_sec = (ev->retrans * 1000) / 1000000;
	interval.tv_usec = (ev->retrans * 1000) % 1000000;
	dhcp6_set_timer(&interval, ev->timer);

	statestr = dhcp6_event_statestr(ev);

	dprintf(LOG_DEBUG, FNAME, "reset a timer on %s, "
		"state=%s, timeo=%d, retrans=%d",
		ev->ifp->ifname, statestr, ev->timeouts, ev->retrans);
}

int
duidcpy(dd, ds)
	struct duid *dd, *ds;
{
	dd->duid_len = ds->duid_len;
	if ((dd->duid_id = malloc(dd->duid_len)) == NULL) {
		dprintf(LOG_ERR, FNAME, "memory allocation failed");
		return (-1);
	}
	memcpy(dd->duid_id, ds->duid_id, dd->duid_len);

	return (0);
}

int
duidcmp(d1, d2)
	struct duid *d1, *d2;
{
	if (d1->duid_len == d2->duid_len) {
		return (memcmp(d1->duid_id, d2->duid_id, d1->duid_len));
	} else
		return (-1);
}

void
duidfree(duid)
	struct duid *duid;
{
	if (duid->duid_id)
		free(duid->duid_id);
	duid->duid_id = NULL;
	duid->duid_len = 0;
}

char *
dhcp6optstr(type)
	int type;
{
	static char genstr[sizeof("opt_65535") + 1]; /* XXX thread unsafe */

	if (type > 65535)
		return ("INVALID option");

	switch(type) {
	case DH6OPT_CLIENTID:
		return ("client ID");
	case DH6OPT_SERVERID:
		return ("server ID");
	case DH6OPT_IA:
		return ("identity association");
	case DH6OPT_IA_TMP:
		return ("IA for temporary");
	case DH6OPT_IADDR:
		return ("IA address");
	case DH6OPT_ORO:
		return ("option request");
	case DH6OPT_PREFERENCE:
		return ("preference");
	case DH6OPT_ELAPSED_TIME:
		return ("elapsed time");
	case DH6OPT_RELAY_MSG:
		return ("relay message");
	case DH6OPT_AUTH:
		return ("authentication");
	case DH6OPT_UNICAST:
		return ("server unicast");
	case DH6OPT_STATUS_CODE:
		return ("status code");
	case DH6OPT_RAPID_COMMIT:
		return ("rapid commit");
	case DH6OPT_USER_CLASS:
		return ("user class");
	case DH6OPT_VENDOR_CLASS:
		return ("vendor class");
	case DH6OPT_VENDOR_OPTS:
		return ("vendor specific info");
	case DH6OPT_INTERFACE_ID:
		return ("interface ID");
	case DH6OPT_RECONF_MSG:
		return ("reconfigure message");
	case DH6OPT_DNS:
		return ("DNS");
	case DH6OPT_PREFIX_DELEGATION:
		return ("prefix delegation");
	case DH6OPT_PREFIX_INFORMATION:
		return ("prefix information");
	case DH6OPT_IA_PD:
		return ("IA_PD");
	case DH6OPT_IA_PD_PREFIX:
		return ("IA_PD prefix");
	default:
		snprintf(genstr, sizeof(genstr), "opt_%d", type);
		return (genstr);
	}
}

char *
dhcp6msgstr(type)
	int type;
{
	static char genstr[sizeof("msg255") + 1]; /* XXX thread unsafe */

	if (type > 255)
		return ("INVALID msg");

	switch(type) {
	case DH6_SOLICIT:
		return ("solicit");
	case DH6_ADVERTISE:
		return ("advertise");
	case DH6_REQUEST:
		return ("request");
	case DH6_CONFIRM:
		return ("confirm");
	case DH6_RENEW:
		return ("renew");
	case DH6_REBIND:
		return ("rebind");
	case DH6_REPLY:
		return ("reply");
	case DH6_RELEASE:
		return ("release");
	case DH6_DECLINE:
		return ("decline");
	case DH6_RECONFIGURE:
		return ("reconfigure");
	case DH6_INFORM_REQ:
		return ("information request");
	case DH6_RELAY_FORW:
		return ("relay-forward");
	case DH6_RELAY_REPLY:
		return ("relay-reply");
	default:
		snprintf(genstr, sizeof(genstr), "msg%d", type);
		return (genstr);
	}
}

char *
dhcp6_stcodestr(code)
	u_int16_t code;
{
	static char genstr[sizeof("code255") + 1]; /* XXX thread unsafe */

	if (code > 255)
		return ("INVALID code");

	switch(code) {
	case DH6OPT_STCODE_SUCCESS:
		return ("success");
	case DH6OPT_STCODE_UNSPECFAIL:
		return ("unspec failure");
	case DH6OPT_STCODE_NOADDRAVAIL:
		return ("no addresses");
	case DH6OPT_STCODE_NOBINDING:
		return ("no binding");
	case DH6OPT_STCODE_NOTONLINK:
		return ("not on-link");
	case DH6OPT_STCODE_USEMULTICAST:
		return ("use multicast");
	case DH6OPT_STCODE_NOPREFIXAVAIL:
		return ("no prefixes");
	default:
		snprintf(genstr, sizeof(genstr), "code%d", code);
		return (genstr);
	}
}

char *
duidstr(duid)
	struct duid *duid;
{
	int i, n;
	char *cp, *ep;
	static char duidstr[sizeof("xx:") * 128 + sizeof("...")];

	cp = duidstr;
	ep = duidstr + sizeof(duidstr);
	for (i = 0; i < duid->duid_len && i <= 128; i++) {
		n = snprintf(cp, ep - cp, "%s%02x", i == 0 ? "" : ":",
		    duid->duid_id[i] & 0xff);
		if (n < 0)
			return NULL;
		cp += n;
	}
	if (i < duid->duid_len)
		snprintf(cp, ep - cp, "%s", "...");

	return (duidstr);
}

char *dhcp6_event_statestr(ev)
	struct dhcp6_event *ev;
{
	switch(ev->state) {
	case DHCP6S_INIT:
		return ("INIT");
	case DHCP6S_SOLICIT:
		return ("SOLICIT");
	case DHCP6S_INFOREQ:
		return ("INFOREQ");
	case DHCP6S_REQUEST:
		return ("REQUEST");
	case DHCP6S_RENEW:
		return ("RENEW");
	case DHCP6S_REBIND:
		return ("REBIND");
	case DHCP6S_RELEASE:
		return ("RELEASE");
	case DHCP6S_IDLE:
		return ("IDLE");
	default:
		return ("???"); /* XXX */
	}
}

void
setloglevel(debuglevel)
	int debuglevel;
{
	if (foreground) {
		switch(debuglevel) {
		case 0:
			debug_thresh = LOG_ERR;
			break;
		case 1:
			debug_thresh = LOG_INFO;
			break;
		default:
			debug_thresh = LOG_DEBUG;
			break;
		}
	} else {
		switch(debuglevel) {
		case 0:
			setlogmask(LOG_UPTO(LOG_ERR));
			break;
		case 1:
			setlogmask(LOG_UPTO(LOG_INFO));
			break;
		}
	}
}

void
dprintf(int level, const char *fname, const char *fmt, ...)
{
	va_list ap;
	char logbuf[LINE_MAX];
	int printfname = 1;

	va_start(ap, fmt);
	vsnprintf(logbuf, sizeof(logbuf), fmt, ap);

	if (*fname == '\0')
		printfname = 0;

	if (foreground && debug_thresh >= level) {
		time_t now;
		struct tm *tm_now;
		const char *month[] = {
			"Jan", "Feb", "Mar", "Apr", "May", "Jun",
			"Jul", "Aug", "Sep", "Oct", "Nov", "Dec",
		};

		if ((now = time(NULL)) < 0)
			exit(1); /* XXX */
		tm_now = localtime(&now);
		fprintf(stderr, "%s%s%3s/%02d/%04d %02d:%02d:%02d %s\n",
		    fname, printfname ? ": " : "",
		    month[tm_now->tm_mon], tm_now->tm_mday,
		    tm_now->tm_year + 1900,
		    tm_now->tm_hour, tm_now->tm_min, tm_now->tm_sec,
		    logbuf);
	} else
		syslog(level, "%s%s%s", fname, printfname ? ": " : "", logbuf);
}
