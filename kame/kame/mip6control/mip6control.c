/*	$KAME: mip6control.c,v 1.45 2003/07/23 04:56:17 keiichi Exp $	*/

/*
 * Copyright (C) 1995, 1996, 1997, and 1998 WIDE Project.
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

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/queue.h>
#include <sys/ioctl.h>
#include <err.h>
#include <string.h>
#include <netdb.h>

#include <net/if.h>
#if defined(__FreeBSD__) && __FreeBSD__ >= 3
#include <net/if_var.h>
#endif

#include <netinet/in.h>
#include <arpa/inet.h>
#include <netinet/ip6.h>
#include <netinet/icmp6.h>
#include <netinet6/in6_var.h>
#include <netinet6/nd6.h>

#include <net/if_hif.h>

#include <fcntl.h>
#include <kvm.h>
#include <nlist.h>
#include <limits.h>

#include <netinet6/mip6.h>
#include <netinet6/mip6_var.h>

#define IOC_ENTRY_COUNT 100 /* XXX */

static int getaddress(char *, struct sockaddr_in6 *);
static const char *ip6_sprintf(const struct sockaddr_in6 *);
static const char *raflg_sprintf(u_int8_t);
static const char *buflg_sprintf(u_int8_t);
static const char *bufpsmstate_sprintf(u_int8_t);
static const char *bufssmstate_sprintf(u_int8_t);
static const char *bustate_sprintf(u_int8_t);
static const char *bcflg_sprintf(u_int8_t);
static struct hif_softc *get_hif_softc(char *);
static void kread(u_long, void *, int);
static int parse_address_port(char *, struct in6_addr *, uint16_t *);

static const char *pfx_desc[] = {
	"prefix\t\tplen\tvltime\tvlexp\tpltime\tplexp\thaddr\n",
	"prefix\t\t\t\t\tplen\tvltime\tvlexp\tpltime\tplexp\thaddr\n"
};
static const char *bu_desc[] = {
	"paddr\t\thaddr\t\tcoa\t\tlifetim\tltexp\trefresh\tretexp\tseqno\tflags\tpfsm\tsfsm\tstate\n",
	"paddr\t\t\t\t\thaddr\t\t\t\t\tcoa\t\t\t\t\tlifetim\tltexp\trefresh\tretexp\tseqno\tflags\tpfsm\tsfsm\tstate\n"
};
static const char *ha_desc[] = {
	"lladdr\t\tgaddr\t\tflags\tpref\tlifetim\tltexp\n",
	"lladdr\t\t\t\t\tgaddr\t\t\t\t\tflags\tpref\tlifetim\tltexp\n"
};
#ifdef MIP6_DRAFT13
static const char *bc_desc[] = {
	"phaddr\t\tpcoa\t\taddr\t\tflags\tplen\tseqno\tlifetim\tltexp\tstate\n",
	"phaddr\t\t\t\t\tpcoa\t\t\t\t\taddr\t\t\t\t\tflags\tplen\tseqno\tlifetim\tltexp\tstate\n"
};
#else
static const char *bc_desc[] = {
	"phaddr\t\tpcoa\t\taddr\t\tflags\tseqno\tlifetim\tltexp\tstate\trefcnt\n",
	"phaddr\t\t\t\t\tpcoa\t\t\t\t\taddr\t\t\t\t\tflags\tseqno\tlifetim\tltexp\tstate\trefcnt\n"
};
#endif /* MIP6_DRAFT13 */
static const char *ipaddr_fmt[] = {
	"%-15.15s ",
	"%-39s "
};

struct nlist nl[] = {
	{ "_mip6_config" },
#define N_MIP6_CONFIG 0
	{ "_hif_softc_list" },
#define N_HIF_SOFTC_LIST 1
	{ "_mip6_bc_list" },
#define N_MIP6_BC_LIST 2
	{ "_mip6_unuse_hoa" },
#define N_MIP6_UNUSE_HOA 3
	{ "_mip6_preferred_ifnames" },
#define N_MIP6_PREFERRED_IFNAMES 4
	{ "" },
};

#define KREAD(addr, buf, type) \
	kread((u_long)addr, (void *)buf, sizeof(type))

kvm_t *kvmd;
int numerichost = 0;
char *__progname;

void
usage()
{
	(void)fprintf(stderr,
		      "usage: %s [-i ifname] [-abcghlmnw]"
		      " [-H home_prefix -P prefixlen]"
		      " [-u address#port] [-v address#port]"
		      " [-S 0|1] [-D 0|1]\n",
		      __progname);
	exit(1);
}

int
main(argc, argv)
     int argc;
     char **argv;
{
	extern char *optarg;
	extern int optind;
	int ch, s;
	int enablemn = 0;
	int disablemn = 0;
	int enableha = 0;
	int longdisp = 0;
	char *ifnarg = "hif0";
	int pfx = 0;
	char *pfxarg = NULL;
	int smhp = 0, gmhp = 0;
	char *smhparg = NULL;
	int sha = 0, sll = 0, gha = 0;
	char *shaarg = NULL, *sllarg = NULL;
	int gbu = 0;
	int gbc = 0;
	int dbc = 0;
	char *dbcarg = NULL;
	char kvm_err[_POSIX2_LINE_MAX];
	int unuseha_s = 0, unuseha_d = 0, unuseha_g = 0;
	char *uharg = NULL;
	int debug = 0;
	char *debugarg = NULL;
	int ipsec = 0;
	char *ipsecarg = NULL;
	int ifid = 0;
	char *ifidarg = NULL;
	int ifnames = 0;
	char *ifname_list = NULL;

	__progname = strrchr(argv[0], '/');
	if (__progname == NULL)
		__progname = argv[0];
	else
		__progname++;

	while ((ch = getopt(argc, argv, "nli:mMgH:hP:A:aL:bcC:u:v:wD:S:T:I:F:")) != -1) {
		switch(ch) {
		case 'm':
			enablemn = 1;
			break;
		case 'M':
			disablemn = 1;
			break;
		case 'n':
			numerichost = 1;
			break;
		case 'g':
			enableha = 1;
			break;
		case 'l':
			longdisp = 1;
			break;
		case 'i':
			ifnarg = optarg;
			break;
		case 'H':
			smhp = 1;
			smhparg = optarg;
			break;
		case 'P':
			pfx = 1;
			pfxarg = optarg;
			break;
		case 'A':
			sha = 1;
			shaarg = optarg;
			break;
		case 'a':
			gha = 1;
			break;
		case 'L':
			sll = 1;
			sllarg = optarg;
			break;
		case 'h':
			gmhp = 1;
			break;
		case 'b':
			gbu = 1;
			break;
		case 'c':
			gbc = 1;
			break;
		case 'C':
			dbc = 1;
			dbcarg = optarg;
			break;
		case 'u':
			unuseha_s = 1;
			uharg = optarg;
			break;
		case 'v':
			unuseha_d = 1;
			uharg = optarg;
			break;
		case 'w':
			unuseha_g = 1;
			break;
		case 'D':
			debug = 1;
			debugarg = optarg;
			break;
		case 'S':
			ipsec = 1;
			ipsecarg = optarg;
			break;
		case 'I':
			ifid = 1;
			ifidarg = optarg;
			break;
		case 'F':
			ifnames = 1;
			ifname_list = optarg;
			break;
		default:
			usage();
			/* NOTREACHED */
		}
	}
	if (argc != optind) {
		usage();
		/* NOTREACHED */
	}

	if ((kvmd = kvm_openfiles(NULL, NULL, NULL, O_RDONLY, kvm_err)) == NULL) {
		fprintf(stderr, "%s\n", kvm_err);
		exit(1);
	}
	if (kvm_nlist(kvmd, nl) < 0) {
		fprintf(stderr, "no namelist\n");
		exit(1);
	}

	if((s = socket(AF_INET6, SOCK_DGRAM, 0)) == -1) {
		perror("socket");
		exit(1);
	}

	if (optind <= 1) {
		struct mip6_config mip6_config;
		struct mip6_preferred_ifnames preferred;
		char *type = "corresponding node";
		int i;

		if (nl[N_MIP6_CONFIG].n_value == 0) {
			fprintf(stderr, "mip6 not found\n");
			exit(1);
		}
		KREAD(nl[N_MIP6_CONFIG].n_value, &mip6_config, mip6_config);
		switch (mip6_config.mcfg_type) {
		case MIP6_CONFIG_TYPE_MOBILENODE:
			KREAD(nl[N_MIP6_PREFERRED_IFNAMES].n_value,
			      &preferred, struct mip6_preferred_ifnames);
			type = "mobile node";
			break;
		case MIP6_CONFIG_TYPE_HOMEAGENT:
			type = "home agent";
			break;
		default:
			if (mip6_config.mcfg_type)
				printf("unknown type %d\n",
				       mip6_config.mcfg_type);
			break;
		}
		printf("node type: %s\n", type);
		printf("IPsec protection: %s\n",
		       mip6_config.mcfg_use_ipsec ? "enable" : "disable");
		if (mip6_config.mcfg_type == MIP6_CONFIG_TYPE_MOBILENODE) {
			printf("preferred IF names: ");
			for (i = 0; i < 3; i++) {
				if (preferred.mip6pi_ifname[i][0] == '\0')
					break;
				printf("%s%s", (i == 0 ? "" : ":"),
				       preferred.mip6pi_ifname[i]);
			}
			printf("\n");
		}
		printf("debug: %s\n",
		       mip6_config.mcfg_debug ? "enable" : "disable");
	}

	if (debug && debugarg) {
		int arg = atoi(debugarg);
		int subcmd = 0;

		switch (arg) {
		case 0:
			subcmd = SIOCSMIP6CFG_DISABLEDEBUG;
			break;
		default:
			subcmd = SIOCSMIP6CFG_ENABLEDEBUG;
			break;
		}
		if(ioctl(s, SIOCSMIP6CFG, (caddr_t)&subcmd) == -1) {
			perror("ioctl");
			exit(1);
		}
	}

	if (smhparg && pfxarg) {
		struct hif_ifreq *ifr;
		struct mip6_prefix *mpfx;

		ifr = malloc(sizeof(struct hif_ifreq) + sizeof(*mpfx));
		if (ifr == NULL) {
			perror("malloc");
			exit(1);
		}
		strcpy(ifr->ifr_name, ifnarg);
		ifr->ifr_count = 1;
		mpfx = (struct mip6_prefix *)((caddr_t)ifr 
					      + sizeof(struct hif_ifreq));
		ifr->ifr_ifru.ifr_mpfx = mpfx;
		bzero(&mpfx->mpfx_prefix, sizeof(mpfx->mpfx_prefix));
		mpfx->mpfx_prefix.sin6_len = sizeof(mpfx->mpfx_prefix);
		mpfx->mpfx_prefix.sin6_family = AF_INET6;
		getaddress(smhparg, &mpfx->mpfx_prefix);
		mpfx->mpfx_prefixlen = atoi(pfxarg);
		mpfx->mpfx_vltime = 0xffff; /* XXX */
		mpfx->mpfx_pltime = 0xff00; /* XXX */
		if(ioctl(s, SIOCAHOMEPREFIX_HIF, (caddr_t)ifr) == -1) {
			perror("ioctl");
			exit(1);
		}
	}

	if (enablemn) {
		int subcmd = SIOCSMIP6CFG_ENABLEMN;
		if(ioctl(s, SIOCSMIP6CFG, (caddr_t)&subcmd) == -1) {
			perror("ioctl");
			exit(1);
		}
	}

	if (disablemn) {
		int subcmd = SIOCSMIP6CFG_DISABLEMN;
		if(ioctl(s, SIOCSMIP6CFG, (caddr_t)&subcmd) == -1) {
			perror("ioctl");
			exit(1);
		}
	}

	if (enableha) {
		int subcmd = SIOCSMIP6CFG_ENABLEHA;
		if(ioctl(s, SIOCSMIP6CFG, (caddr_t)&subcmd) == -1) {
			perror("ioctl");
			exit(1);
		}
	}

	if (gmhp) {
		struct hif_softc *sc;
		struct hif_subnet *hs, hif_subnet;
		struct mip6_subnet *ms, mip6_subnet;
		struct mip6_subnet_prefix *mspfx, mip6_subnet_prefix;
		struct mip6_prefix *mpfx, mip6_prefix;
		struct timeval time;

		gettimeofday(&time, 0);

		sc = get_hif_softc(ifnarg);
		printf(pfx_desc[longdisp]);
		for (hs = TAILQ_FIRST(&sc->hif_hs_list_home);
		     hs;
		     hs = TAILQ_NEXT(hs, hs_entry)) {
			KREAD(hs, &hif_subnet, hif_subnet);
			hs = &hif_subnet;
			KREAD(hs->hs_ms, &mip6_subnet, mip6_subnet);
			ms = &mip6_subnet;
			for (mspfx = TAILQ_FIRST(&ms->ms_mspfx_list);
			     mspfx;
			     mspfx = TAILQ_NEXT(mspfx, mspfx_entry)) {
				KREAD(mspfx, &mip6_subnet_prefix, mip6_subnet_prefix);
				mspfx = &mip6_subnet_prefix;
				KREAD(mspfx->mspfx_mpfx, &mip6_prefix, mip6_prefix);
				mpfx = &mip6_prefix;
				printf(ipaddr_fmt[longdisp],
				       ip6_sprintf(&mpfx->mpfx_prefix));
				printf("%7u %7u %7ld %7u %7ld ",
				       mpfx->mpfx_prefixlen,
				       mpfx->mpfx_vltime,
				       mpfx->mpfx_vlexpire - time.tv_sec,
				       mpfx->mpfx_pltime,
				       mpfx->mpfx_plexpire - time.tv_sec);
				printf(ipaddr_fmt[longdisp],
				       ip6_sprintf(&mpfx->mpfx_haddr));
				printf("\n");
			}
		}
	}

	if(shaarg && sllarg) {
		struct hif_ifreq *ifr;
		struct mip6_ha *mha;

		printf("set homeagent to %s (%s)\n",
		       ifnarg, shaarg);
		ifr = malloc(sizeof(struct hif_ifreq) + sizeof(*mha));
		if (ifr == NULL) {
			perror("malloc");
			exit(1);
		}
		strcpy(ifr->ifr_name, ifnarg);
		ifr->ifr_count = 1;
		mha = (struct mip6_ha *)((caddr_t)ifr 
					 + sizeof(struct hif_ifreq));
		ifr->ifr_ifru.ifr_mha = mha;
		bzero(&mha->mha_lladdr, sizeof(mha->mha_lladdr));
		mha->mha_lladdr.sin6_len = sizeof(mha->mha_lladdr);
		mha->mha_lladdr.sin6_family = AF_INET6;
		getaddress(sllarg, &mha->mha_lladdr);
		bzero(&mha->mha_gaddr, sizeof(mha->mha_gaddr));
		mha->mha_gaddr.sin6_len = sizeof(mha->mha_gaddr);
		mha->mha_gaddr.sin6_family = AF_INET6;
		getaddress(shaarg, &mha->mha_gaddr);
		mha->mha_flags = ND_RA_FLAG_HOME_AGENT;
		mha->mha_pref = 0;
		mha->mha_lifetime = 0xffff;
		if(ioctl(s, SIOCAHOMEAGENT_HIF, (caddr_t)ifr) == -1) {
			perror("ioctl");
			exit(1);
		}
	}

	if (gha) {
		struct hif_softc *sc;
		struct hif_subnet *hs, hif_subnet;
		struct mip6_subnet *ms, mip6_subnet;
		struct mip6_subnet_ha *msha, mip6_subnet_ha;
		struct mip6_ha *mha, mip6_ha;
		struct hif_subnet *hsl[2];
		int i;
		struct timeval time;

		gettimeofday(&time, 0);

		sc = get_hif_softc(ifnarg);
		hsl[0] = TAILQ_FIRST(&sc->hif_hs_list_home);
		hsl[1] = TAILQ_FIRST(&sc->hif_hs_list_foreign);
		printf(ha_desc[longdisp]);
		for (i = 0; i < 2; i++) {
			for (hs = hsl[i];
			     hs;
			     hs = TAILQ_NEXT(hs, hs_entry)) {
				KREAD(hs, &hif_subnet, hif_subnet);
				hs = &hif_subnet;
				KREAD(hs->hs_ms, &mip6_subnet, mip6_subnet);
				ms = &mip6_subnet;
				for (msha = TAILQ_FIRST(&ms->ms_msha_list);
				     msha;
				     msha = TAILQ_NEXT(msha, msha_entry)) {
					KREAD(msha, &mip6_subnet_ha, mip6_subnet_ha);
					msha = &mip6_subnet_ha;
					KREAD(msha->msha_mha, &mip6_ha, mip6_ha);
					mha = &mip6_ha;
					printf(ipaddr_fmt[longdisp],
					       ip6_sprintf(&mha->mha_lladdr));
					printf(ipaddr_fmt[longdisp],
					       ip6_sprintf(&mha->mha_gaddr));
					printf("%-7s %7d %7d %7ld\n",
					       raflg_sprintf(mha->mha_flags),
					       mha->mha_pref,
					       mha->mha_lifetime,
					       mha->mha_expire - time.tv_sec);
				}
			}
		}
	}

	if (gbu) {
		struct hif_softc *sc;
		struct mip6_bu *mbu, mip6_bu;
		struct timeval time;

		gettimeofday(&time, 0);

		sc = get_hif_softc(ifnarg);
		printf(bu_desc[longdisp]);
		for (mbu = LIST_FIRST(&sc->hif_bu_list);
		     mbu;
		     mbu = LIST_NEXT(mbu, mbu_entry)) {
			KREAD(mbu, &mip6_bu, mip6_bu);
			mbu = &mip6_bu;
			printf(ipaddr_fmt[longdisp],
			       ip6_sprintf(&mbu->mbu_paddr));
			printf(ipaddr_fmt[longdisp],
			       ip6_sprintf(&mbu->mbu_haddr));
			printf(ipaddr_fmt[longdisp],
			       ip6_sprintf(&mbu->mbu_coa));
			printf("%7u %7ld %7u %7ld %7u %-7s %-7s %-7s %-7s\n",
			       mbu->mbu_lifetime,
			       mbu->mbu_expire - time.tv_sec,
			       mbu->mbu_refresh,
			       (mbu->mbu_retrans - time.tv_sec) < 0 ? 0
			       : (mbu->mbu_retrans - time.tv_sec), /* XXX */
			       mbu->mbu_seqno,
			       buflg_sprintf(mbu->mbu_flags),
			       bufpsmstate_sprintf(mbu->mbu_pri_fsm_state),
			       bufssmstate_sprintf(mbu->mbu_sec_fsm_state),
			       bustate_sprintf(mbu->mbu_state));
		}
	}

	if (gbc) {
		struct mip6_bc_list mip6_bc_list;
		struct mip6_bc *mbc, mip6_bc;
		struct timeval time;

		gettimeofday(&time, 0);

		if (nl[N_MIP6_BC_LIST].n_value == 0) {
			fprintf(stderr, "bc not found\n");
			exit(1);
		}
		KREAD(nl[N_MIP6_BC_LIST].n_value, &mip6_bc_list, mip6_bc_list);
		printf(bc_desc[longdisp]);
		for (mbc = LIST_FIRST(&mip6_bc_list);
		     mbc;
		     mbc = LIST_NEXT(mbc, mbc_entry)) {
			KREAD(mbc, &mip6_bc, mip6_bc);
			mbc = &mip6_bc;
			printf(ipaddr_fmt[longdisp],
			       ip6_sprintf(&mbc->mbc_phaddr));
			printf(ipaddr_fmt[longdisp],
			       ip6_sprintf(&mbc->mbc_pcoa));
			printf(ipaddr_fmt[longdisp],
			       ip6_sprintf(&mbc->mbc_addr));
			printf(
#ifdef MIP6_DRAFT13
			       "%-7s %7u %7u %7u %7ld %-7s\n",
#else
			       "%-7s %7u %7u %7ld %-7s %7u\n",
#endif /* MIP6_DRAFT13 */
			       buflg_sprintf(mbc->mbc_flags),
#ifdef MIP6_DRAFT13
			       mbc->mbc_prefixlen,
#endif /* MIP6_DRAFT13 */
			       mbc->mbc_seqno,
			       mbc->mbc_lifetime,
			       mbc->mbc_expire - time.tv_sec,
			       bcflg_sprintf(mbc->mbc_state),
			       mbc->mbc_refcnt);
		}
	}

	if (dbc && dbcarg) {
		struct mip6_req mr;

		bzero(&mr, sizeof(mr));
		getaddress(dbcarg, &mr.mip6r_ru.mip6r_sin6);
		if (ioctl(s, SIOCDBC, &mr) < 0) {
			perror("ioctl");
			exit(-1);
		}
	}

	if (unuseha_s || unuseha_d) {
		struct mip6_req mr;

		if (parse_address_port(uharg, &mr.mip6r_ru.mip6r_sin6.sin6_addr,
				    &mr.mip6r_ru.mip6r_sin6.sin6_port))
			exit (-1);
		if (ioctl(s, unuseha_s ? SIOCSUNUSEHA : SIOCDUNUSEHA
		          , &mr) < 0) {
			perror("ioctl");
			exit(-1);
		}
	}

	if (unuseha_g) {
		struct mip6_unuse_hoa_list mip6_unuse_hoa_list;
		struct mip6_unuse_hoa *uh, mip6_unuse_hoa;

		if (nl[N_MIP6_UNUSE_HOA].n_value == 0) {
			fprintf(stderr, "mip6_unuse_hoa not found\n");
			exit(1);
		}
		KREAD(nl[N_MIP6_UNUSE_HOA].n_value, &mip6_unuse_hoa_list,
		      mip6_unuse_hoa_list);
		for (uh = LIST_FIRST(&mip6_unuse_hoa_list);
		     uh;
		     uh = LIST_NEXT(uh, unuse_entry)) {
			struct sockaddr_in6 xxx;
			KREAD(uh, &mip6_unuse_hoa, mip6_unuse_hoa);
			uh = &mip6_unuse_hoa;
			bzero(&xxx, sizeof(xxx));
			xxx.sin6_len = sizeof(xxx);
			xxx.sin6_family = AF_INET6;
			xxx.sin6_addr = uh->unuse_addr;
			printf("%s", ip6_sprintf(&xxx));
			if (uh->unuse_port)
				printf("#%d", ntohs(uh->unuse_port));
			printf("\n");
		}
	}

	if (ipsec && ipsecarg) {
		int arg = atoi(ipsecarg);
		int subcmd = 0;

		switch (arg) {
		case 0:
			subcmd = SIOCSMIP6CFG_DISABLEIPSEC;
			break;
		default:
			subcmd = SIOCSMIP6CFG_ENABLEIPSEC;
			break;
		}
		if(ioctl(s, SIOCSMIP6CFG, (caddr_t)&subcmd) == -1) {
			perror("ioctl");
			exit(1);
		}
	}
	
	if (ifid && ifidarg) {
		struct hif_ifreq *ifr;
		struct sockaddr_in6 ifid_sa;
		struct in6_addr *ifid;

		ifr = malloc(sizeof(struct hif_ifreq) + sizeof(*ifid));
		if (ifr == NULL) {
			perror("malloc");
			exit(1);
		}
		strcpy(ifr->ifr_name, ifnarg);
		ifr->ifr_count = 1;
		ifid = (struct in6_addr *)((caddr_t)ifr 
					   + sizeof(struct hif_ifreq));
		ifr->ifr_ifru.ifr_ifid = ifid;
		getaddress(ifidarg, &ifid_sa);
		*ifid = ifid_sa.sin6_addr;
		if (ioctl(s, SIOCSIFID_HIF, (caddr_t)ifr) == -1) {
			perror("ioctl");
			exit(1);
		}
	}

	if (ifnames && ifname_list) {
		struct mip6_req mr;
		char *delimiter = ifname_list;
		char *ifname = ifname_list;
		int i = 0;

		bzero (&mr, sizeof(mr));

		while (delimiter != NULL) {
			delimiter = strchr(ifname, ':');
			if (delimiter != NULL) {
				*delimiter = '\0';
				strncpy(mr.mip6r_ru.mip6r_ifnames.mip6pi_ifname[i++],
				    ifname, IFNAMSIZ);
				ifname = delimiter + 1;
				if (i > 3)
					break;
			} else {
				strncpy(mr.mip6r_ru.mip6r_ifnames.mip6pi_ifname[i],
				    ifname, IFNAMSIZ);
				break;
			}
		}
		if (ioctl(s, SIOCSPREFERREDIFNAMES, (caddr_t)&mr) == -1) {
			perror("ioctl");
			exit(1);
		}
	}
	
	exit(0);
}

/* Returns the address in network order */
static int
getaddress(char *address, struct sockaddr_in6 *sin6)
{
	struct addrinfo hints, *res;
	int ai_errno;

	bzero(&hints, sizeof(struct addrinfo));
	hints.ai_family = AF_INET6;

	ai_errno = getaddrinfo(address, NULL, &hints, &res);
	if (ai_errno)
		errx(1, "%s: %s", address, gai_strerror(ai_errno));
	sin6->sin6_len = ((struct sockaddr_in6 *)res->ai_addr)->sin6_len;
	sin6->sin6_family = ((struct sockaddr_in6 *)res->ai_addr)->sin6_family;
	sin6->sin6_addr = ((struct sockaddr_in6 *)res->ai_addr)->sin6_addr;
	sin6->sin6_scope_id
		= ((struct sockaddr_in6 *)res->ai_addr)->sin6_scope_id;
#ifdef __KAME__
	if (IN6_IS_ADDR_LINKLOCAL(&sin6->sin6_addr))
		*(u_int16_t *)&sin6->sin6_addr.s6_addr[2] =
			htons(((struct sockaddr_in6 *)res->ai_addr)->sin6_scope_id);
#endif
	freeaddrinfo(res);

        return 0;
}

static int ip6round = 0;
const char *
ip6_sprintf(sa_addr)
	const struct sockaddr_in6 *sa_addr;
{
	static char ip6buf[8][NI_MAXHOST];
	struct sockaddr_in6 sin6;
	int flags = 0;

	if (numerichost)
		flags |= NI_NUMERICHOST;

	bcopy(sa_addr, &sin6, sizeof(sin6));

	/*
	 * XXX: This is a special workaround for KAME kernels.
	 * sin6_scope_id field of SA should be set in the future.
	 */
	if (IN6_IS_ADDR_LINKLOCAL(&sin6.sin6_addr) ||
	    IN6_IS_ADDR_MC_LINKLOCAL(&sin6.sin6_addr) ||
	    IN6_IS_ADDR_MC_NODELOCAL(&sin6.sin6_addr)) {
		/* XXX: override is ok? */
		sin6.sin6_scope_id = (u_int32_t)ntohs(*(u_short *)&sin6.sin6_addr.s6_addr[2]);
		*(u_short *)&sin6.sin6_addr.s6_addr[2] = 0;
	}

	ip6round = (ip6round + 1) & 7;

	if (getnameinfo((struct sockaddr *)&sin6, sizeof(sin6),
			ip6buf[ip6round], NI_MAXHOST, NULL, 0, flags) != 0)
		return "?";

	return ip6buf[ip6round];
}

static const char *
raflg_sprintf(flags)
	u_int8_t flags;
{
	static char buf[] = "MOH";

	snprintf(buf, sizeof(buf), "%s%s%s",
		 (flags & ND_RA_FLAG_MANAGED ? "M" : "-"),
		 (flags & ND_RA_FLAG_OTHER ? "O" : "-"),
		 (flags & ND_RA_FLAG_HOME_AGENT ? "H" : "-"));

	return buf;
}

/* parse address and port in formart like 'addr#port' */
/* ex) ::#80 means unspecified addr and port 80 */
static int
parse_address_port(arg, addr, port)
	char *arg;
	struct in6_addr *addr;
	uint16_t *port;
{
	int error;
	char *p = NULL;
	struct addrinfo hints, *res;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET6;
	hints.ai_flags = AI_NUMERICHOST;
	if ((p = strchr(arg, '#')))
		*p++ = '\0';

	error = getaddrinfo(arg, NULL, &hints, &res);
	if (error) {
		perror("getaddrinfo");
		return 1;
	}

	if (res->ai_family == AF_INET6) 
		*addr = ((struct sockaddr_in6 *)(res->ai_addr))->sin6_addr;
	
	if (p)
		*port = htons(atoi(p));
	else
		*port = 0;
	
	freeaddrinfo(res);

	return 0;
}


static const char *
buflg_sprintf(flags)
	u_int8_t flags;
{
	static char buf[] = "AHLc";

	snprintf(buf, sizeof(buf), "%s%s%s%s",
		 (flags & IP6MU_ACK ?    "A" : "-"),
		 (flags & IP6MU_HOME ?   "H" : "-"),
		 (flags & IP6MU_LINK ?   "L" : "-"),
		 (flags & IP6MU_CLONED ? "c" : "-"));

	return buf;
}

static const char *
bufpsmstate_sprintf(state)
	u_int8_t state;
{
	static char *buf[] = {
		"IDLE",
		"RRINIT",
		"RRREDO",
		"RRDEL",
		"WAITA",
		"WAITAR",
		"WAITD",
		"BOUND"
	};

	return buf[state];
}

static const char *
bufssmstate_sprintf(state)
	u_int8_t state;
{
	static char *buf[] = {
		"START",
		"WAITHC",
		"WAITH",
		"WAITC"
	};

	return buf[state];
}

static const char *
bustate_sprintf(flags)
	u_int8_t flags;
{
	static char buf[] = "MUAS";

	snprintf(buf, sizeof(buf), "%s%s",
	    (flags & MIP6_BU_STATE_MIP6NOTSUPP ? "M" : "-"),
	    (flags & MIP6_BU_STATE_BUNOTSUPP ? "U" : "-"));

	return buf;
}

static const char *
bcflg_sprintf(flags)
	u_int8_t flags;
{
	static char buf[] = "DRA";

	snprintf(buf, sizeof(buf), "%s%s%s",
		 (flags & MIP6_BC_STATE_DAD_WAIT ? "D" : "-"),
		 (flags & MIP6_BC_STATE_BR_WAITSENT ? "R" : "-"),
		 (flags & MIP6_BC_STATE_BA_WAITSENT ? "A" : "-"));

	return buf;
}

static struct hif_softc *
get_hif_softc(ifname)
	char *ifname;
{
	TAILQ_HEAD(hif_softc_list, hif_softc) hif_softc_list;
	struct hif_softc *sc;
	static struct hif_softc hif_softc;
	u_short ifindex = if_nametoindex(ifname);

	if (ifindex == 0) {
		fprintf(stderr, "%s: not found\n", ifname);
		exit(1);
	}

	if (nl[N_HIF_SOFTC_LIST].n_value == 0) {
		fprintf(stderr, "hif not found\n");
		exit(1);
	}
	KREAD(nl[N_HIF_SOFTC_LIST].n_value, &hif_softc_list, hif_softc_list);
	for (sc = TAILQ_FIRST(&hif_softc_list);
	     sc;
	     sc = TAILQ_NEXT(sc, hif_entry)) {
		KREAD(sc, &hif_softc, hif_softc);
		sc = &hif_softc;
		if (sc->hif_if.if_index == ifindex)
			break;
	}
	if (sc == NULL) {
		fprintf(stderr, "%s: not found\n", ifname);
		exit(1);
	}

	return sc;
}

static void
kread(addr, buf, size)
	u_long addr;
	void *buf;
	int size;
{
	if (kvm_read(kvmd, addr, buf, size) != size) {
		fprintf(stderr, "%s\n", kvm_geterr(kvmd));
		exit(1);
	}
}
