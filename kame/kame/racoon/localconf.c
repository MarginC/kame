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
/* YIPS @(#)$Id: localconf.c,v 1.7 2000/04/26 20:05:24 sakane Exp $ */

#include <sys/types.h>
#include <sys/param.h>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>

#include "var.h"
#include "misc.h"
#include "vmbuf.h"
#include "plog.h"
#include "debug.h"

#include "localconf.h"
#include "algorithm.h"
#include "isakmp_var.h"
#include "isakmp.h"
#include "ipsec_doi.h"
#include "admin_var.h"
#include "vendorid.h"

struct localconf *lcconf;

static struct algorithm_strength **initalgstrength __P((void));

static struct algorithm_strength **
initalgstrength()
{
	struct algorithm_strength **new;
	int i;

	new = CALLOC(MAXALGCLASS * sizeof(*new), struct algorithm_strength **);
	if (new == NULL) {
		plog(logp, LOCATION, NULL,
			"calloc (%s)\n", strerror(errno)); 
		return NULL;
	}

	for (i = 0; i < MAXALGCLASS; i++) {
		new[i] = CALLOC(sizeof(*new[i]), struct algorithm_strength *);
		if (new[i] == NULL) {
			plog(logp, LOCATION, NULL,
				"calloc (%s)\n", strerror(errno)); 
			return NULL;
		}
	}

	return new;
}

void
initlcconf()
{
	static struct localconf localconf;

	memset(&localconf, 0, sizeof(localconf));

	lcconf = &localconf;

	lcconf->algstrength = initalgstrength();
	if (lcconf->algstrength == NULL) {
		fprintf(stderr, "failed to allocate algorithm strength\n");
		exit(1);
		/*NOTREACHED*/
	}

	lcconf->racoon_conf = LC_DEFAULT_CF;
	lcconf->autograbaddr = 1;
	lcconf->port_isakmp = PORT_ISAKMP;
	lcconf->port_admin = PORT_ADMIN;
	lcconf->default_af = AF_INET;
	lcconf->pad_random = LC_DEFAULT_PAD_RANDOM;
	lcconf->pad_maxsize = LC_DEFAULT_PAD_MAXSIZE;
	lcconf->pad_restrict = LC_DEFAULT_PAD_STRICT;
	lcconf->pad_excltail = LC_DEFAULT_PAD_EXCLTAIL;
	lcconf->retry_counter = LC_DEFAULT_RETRY_COUNTER;
	lcconf->retry_interval = LC_DEFAULT_RETRY_INTERVAL;
	lcconf->count_persend = LC_DEFAULT_COUNT_PERSEND;
	lcconf->secret_size = LC_DEFAULT_SECRETSIZE;
	lcconf->retry_checkph1 = LC_DEFAULT_RETRY_CHECKPH1;
	lcconf->wait_ph2complete = LC_DEFAULT_WAIT_PH2COMPLETE;
	lcconf->strict_address = FALSE;

	/* set vendor id */
    {
	/*
	 * XXX move this section to vendorid.c. and calculate HASH same time.
	 */
	char *vid = VENDORID;
	lcconf->vendorid = vmalloc(strlen(vid));
	if (lcconf->vendorid == NULL) {
		fprintf(stderr, "failed to set vendorid.\n");
		exit(1);
	}
	memcpy(lcconf->vendorid->v, vid, strlen(vid));
    }
}

vchar_t *
getpsk(id0)
	vchar_t *id0;
{
	FILE *fp;
	char *id;
	char buf[1024];	/* XXX how is variable length ? */
	char *p, *q;
	vchar_t *key = NULL;
	int len;

	fp = fopen(lcconf->pathinfo[LC_PATHTYPE_PSK], "r");
	if (fp == NULL) {
		plog(logp, LOCATION, NULL,
			"failed to open pre_share_key file %s\n",
			lcconf->pathinfo[LC_PATHTYPE_PSK]);
		return NULL;
	}

	id = CALLOC(1 + id0->l - sizeof(struct ipsecdoi_id_b), char *);
	if (id == NULL) {
		plog(logp, LOCATION, NULL,
			"calloc (%s)\n", strerror(errno)); 
		goto end;
	}
	memcpy(id, id0->v + sizeof(struct ipsecdoi_id_b),
		id0->l - sizeof(struct ipsecdoi_id_b));
	id[id0->l - sizeof(struct ipsecdoi_id_b)] = '\0';

	while (fgets(buf, sizeof(buf), fp) != NULL) {
		/* search the end of 1st string. */
		for (p = buf; *p != '\0' && !isspace(*p); p++)
			;
		if (*p == '\0')
			continue;	/* no 2nd parameter */
		*p = '\0';
		/* search the fist of 2nd string. */
		while (isspace(*++p))
			;
		if (*p == '\0')
			continue;	/* no 2nd parameter */
		p--;
		if (strcmp(id, buf) == 0) {
			p++;
			len = 0;
			for (q = p; *q != '\0' && *q != '\n'; q++)
				len++;
			*q = '\0';
			key = vmalloc(len);
			if (key == NULL) {
				plog(logp, LOCATION, NULL,
					"failed to allocate key buffer.\n");
				goto end;
			}
			memcpy(key->v, p, key->l);
			goto end;
		}
	}

end:
	if (id)
		free(id);
	fclose(fp);
	return key;
}

/*
 * get PSK by address.
 */
vchar_t *
getpskbyaddr(remote)
	struct sockaddr *remote;
{
	FILE *fp;
	char addr[NI_MAXHOST], port[NI_MAXSERV];
	char buf[1024];	/* XXX how is variable length ? */
	char *p, *q;
	vchar_t *key = NULL;
	int len;

	fp = fopen(lcconf->pathinfo[LC_PATHTYPE_PSK], "r");
	if (fp == NULL) {
		plog(logp, LOCATION, NULL,
			"failed to open pre_share_key file %s\n",
			lcconf->pathinfo[LC_PATHTYPE_PSK]);
		return NULL;
	}

	GETNAMEINFO(remote, addr, port);

	while (fgets(buf, sizeof(buf), fp) != NULL) {
		/* search the end of 1st string. */
		for (p = buf; *p != '\0' && !isspace(*p); p++)
			;
		if (*p == '\0')
			continue;	/* no 2nd parameter */
		*p = '\0';
		/* search the fist of 2nd string. */
		while (isspace(*++p))
			;
		if (*p == '\0')
			continue;	/* no 2nd parameter */
		p--;
		if (strcmp(buf, addr) == 0) {
			p++;
			len = 0;
			for (q = p; *q != '\0' && *q != '\n'; q++)
				len++;
			*q = '\0';
			key = vmalloc(len);
			if (key == NULL) {
				plog(logp, LOCATION, NULL,
					"failed to allocate key buffer.\n");
				goto end;
			}
			memcpy(key->v, p, key->l);
			goto end;
		}
	}

end:
	fclose(fp);
	return key;
}

static int lc_doi2idtype[] = {
	-1,
	-1,
	LC_IDENTTYPE_FQDN,
	LC_IDENTTYPE_USERFQDN,
	-1,
	-1,
	-1,
	-1,
	-1,
	-1,
	-1,
	LC_IDENTTYPE_KEYID,
};

/*
 * convert DOI value to idtype
 * OUT	-1   : NG
 *	other: converted.
 */
int
doi2idtype(idtype)
	int idtype;
{
	if (ARRAYLEN(lc_doi2idtype) > idtype)
		return lc_doi2idtype[idtype];
	return -1;
}

static int lc_idtype2doi[] = {
	IPSECDOI_ID_FQDN,
	IPSECDOI_ID_USER_FQDN,
	IPSECDOI_ID_KEY_ID,
	0,	/* When type is "address", then it's dealed with default. */
};

/*
 * convert idtype to DOI value.
 * OUT	-1   : NG
 *	other: converted.
 */
int
idtype2doi(idtype)
	int idtype;
{
	if (ARRAYLEN(lc_idtype2doi) > idtype)
		return lc_idtype2doi[idtype];
	return -1;
}

static int lc_sittype2doi[] = {
	IPSECDOI_SIT_IDENTITY_ONLY,
	IPSECDOI_SIT_SECRECY,
	IPSECDOI_SIT_INTEGRITY,
};

/*
 * convert sittype to DOI value.
 * OUT	-1   : NG
 *	other: converted.
 */
int
sittype2doi(sittype)
	int sittype;
{
	if (ARRAYLEN(lc_sittype2doi) > sittype)
		return lc_sittype2doi[sittype];
	return -1;
}

static int lc_doitype2doi[] = {
	IPSEC_DOI,
};

/*
 * convert doitype to DOI value.
 * OUT	-1   : NG
 *	other: converted.
 */
int
doitype2doi(doitype)
	int doitype;
{
	if (ARRAYLEN(lc_doitype2doi) > doitype)
		return lc_doitype2doi[doitype];
	return -1;
}

