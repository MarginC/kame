# New ports collection makefile for:	pidentd
# Version required:     2.8.5
# Date created:		19 Oct 1994
# Whom:			torstenb
#
# $FreeBSD: ports/security/pidentd/Makefile,v 1.29 2000/02/14 15:12:34 sumikawa Exp $
#

PORTNAME=	pidentd
PORTVERSION=	2.8.5
CATEGORIES=	security net #ipv6
MASTER_SITES=	ftp://ftp.lysator.liu.se/pub/ident/servers/ \
		ftp://ftp.fu-berlin.de/unix/security/ident/servers/

PATCH_SITES=	http://www.imasy.or.jp/~ume/ipv6/
PATCHFILES=	pidentd-2.8.5-ipv6-1.5.diff.gz
PATCH_DIST_STRIP=	-p2

MAINTAINER=	ume@mahoroba.org

ALL_TARGET=     freebsd
MAKE_ENV=	REALPREFIX=${PREFIX}
MAN8=		identd.8 idecrypt.8

.include <bsd.port.pre.mk>

#.if ${OSVERSION} >= 400014
ADD_GDEFS+=	-DINET6 -D__ss_family=ss_family -D__ss_len=ss_len
ADD_GLIBS+=	-L/usr/local/v6/lib -linet6
#.endif

# Uncomment to activate the use of verifiable "cookies".  The idea is to be
# able to detect fake "logs" intended to get your innocent users in trouble.
# Naturally, since it uses libdes, you must have the "secure" dist installed.
# USE_DES=	true

.if defined(USE_DES)
ADD_GDEFS+=	-DINCLUDE_CRYPT
ADD_GLIBS+=	-ldes
.endif
#.if defined(USE_DES) || ${OSVERSION} >= 400014
MAKE_ENV+=	ADD_GDEFS="${ADD_GDEFS}" ADD_GLIBS="${ADD_GLIBS}"
#.endif

.include <bsd.port.post.mk>
