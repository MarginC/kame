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

/*
 * draft-ietf-dhc-v6exts-11
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <dhcp6opt.h>

static struct dhcp6_opt dh6opttab[] = {
	/* IP Address Extension */
	{ OC6_IPADDR, OL6_N,	"IP Address",			OT6_NONE, },

	/* General Extension */
	{ OC6_TIMEOFFSET, 4,		"Time Offset",		OT6_NUM, },
	{ OC6_TIMEZONE, OL6_N,	"IEEE 1003.1 POSIX Timezone",	OT6_STR, },
	{ OC6_DNS, OL6_16N,	"Domain Name Server",		OT6_V6, },
	{ OC6_DOMAIN, OL6_N,	"Domain Name",			OT6_STR, },

	/* Application and Service Parameters */
	{ OC6_DIRAGENT, OL6_N,	"Directory Agent",		OT6_NONE, },
	{ OC6_SVCSCOPE, OL6_N,	"Service Scope"	,		OT6_NONE, },
	{ OC6_NTPSERVER, OL6_16N,	"Network Time Protocol Servers",
	  							OT6_V6, },
	{ OC6_NISDOMAIN, OL6_N,	"NIS Domain",			OT6_STR, },
	{ OC6_NISSERVER, OL6_16N,	"NIS Servers",		OT6_V6, },
	{ OC6_NISPLUSDOMAIN, OL6_N,	"NIS+ Domain",		OT6_STR, },
	{ OC6_NISPLUSSERVER, OL6_16N,	"NIS+ Servers",		OT6_V6, },

	/* TCP Parameters */
	{ OC6_TCPKEEPALIVEINT, 4,	"TCP Keepalive Interval",
	  							OT6_NUM, },

	/* DHCPv6 Extensions */
	{ OC6_MAXSIZE, 4,	"Maximum DHCPv6 Message Size",	OT6_NUM, },
	{ OC6_CONFPARAM, OL6_N,	"DHCP Retransmission and Configuration Parameter",
							OT6_NONE, },
	{ OC6_PLATSPECIFIC, OL6_N,	"Platform Specific Information",
	  							OT6_NONE, },
	{ OC6_PLATCLASSID, OL6_N,	"Platform Class Identifier",
	  							OT6_STR, },
	{ OC6_CLASSID, OL6_N,	"Class Identifier",		OT6_STR, },
	{ OC6_RECONFMADDR, 16,	"Reconfigure Multicast Address", OT6_V6, },
	{ OC6_RENUMSERVERADDR, 16,	"Renumber DHCPv6 Server Address",
							OT6_V6, },
	{ OC6_DHCPICMPERR, OL6_N,	"DHCP Relay ICMP Error Message",
	  							OT6_NONE, },
	{ OC6_CLISVRAUTH, OL6_N,	"Client-Server Authentication",
	  							OT6_NONE, },
	{ OC6_CLIKEYSELECT, 4,	"Client Key Selection",		OT6_NUM, },

	/* End Extension */
	{ OC6_END, OL6_Z,	"End",				OT6_NONE, },

	{ 0 },
};

struct dhcp6_opt *dh6o_pad;
struct dhcp6_opt *dh6o_end;
int dhcp6_param[] = {
	-1,	2000,	4,	100,	1000,
	10,	2000,	2000,	12000,	10,
	12000,	2000,	10000,	1000,	5000,
	600000,	120000
};

void
dhcp6opttab_init()
{
	dh6o_pad = dhcp6opttab_bycode(0);
	dh6o_end = dhcp6opttab_bycode(65536);
}

struct dhcp6_opt *
dhcp6opttab_byname(name)
	char *name;
{
	struct dhcp6_opt *p;

	for (p = dh6opttab; p->code; p++)
		if (strcmp(name, p->name) == 0)
			return p;
	return NULL;
}

struct dhcp6_opt *
dhcp6opttab_bycode(code)
	u_int code;
{
	struct dhcp6_opt *p;

	for (p = dh6opttab; p->code; p++)
		if (p->code == code)
			return p;
	return NULL;
}
