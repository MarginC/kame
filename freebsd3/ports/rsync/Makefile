# Ports collection makefile for:  rsync
# Date created:			  Sat Aug 3, 1996
# Whom:				  David O'Brien (obrien@cs.ucdavis.edu)
#
# $FreeBSD: ports/net/rsync/Makefile,v 1.37 2000/02/17 07:37:23 sumikawa Exp $
#

PORTNAME=	rsync
PORTVERSION=	2.4.5
CATEGORIES=	net ipv6
MASTER_SITES=	ftp://samba.anu.edu.au/pub/rsync/  \
		ftp://sunsite.auc.dk/pub/unix/rsync/  \
		ftp://ftp.sunet.se/pub/unix/admin/rsync/  \
		ftp://ftp.fu-berlin.de/pub/unix/network/rsync/

PATCH_SITES=    ftp://ftp.kame.net/pub/kame/misc/ \
                ftp://ftp.pasta.cs.uit.no/pub/IPv6/misc/
PATCHFILES=     rsync-245-v6-20000901b.diff.gz
PATCH_DIST_STRIP=       -p1

MAINTAINER=     itojun@itojun.org

Y2K=		http://rsync.samba.org/rsync/y2k.html

GNU_CONFIGURE=	yes

.include <bsd.port.pre.mk>

#.if ${OSVERSION} >= 400014
CONFIGURE_ARGS= --enable-ipv6
#.else
#CONFIGURE_ARGS= --disable-ipv6
#.endif
MAN1=		rsync.1
MAN5=		rsyncd.conf.5

pre-configure:
	@${PERL} -pi.orig -e 's:/etc/:${PREFIX}/etc/:g' ${WRKSRC}/rsync.h
	@${PERL} -pi.orig -e 's:/etc/:${PREFIX}/etc/:g' ${WRKSRC}/rsync.1
	@${PERL} -pi.orig -e 's:/etc/:${PREFIX}/etc/:g' ${WRKSRC}/rsyncd.conf.5
.if defined(WITH_SSH) || defined(USE_SSH)
	@${PERL} -pi.orig-rsh -e 's/"rsh"/"ssh"/g' ${WRKSRC}/rsync.h
.endif

post-install:
	@strip ${PREFIX}/bin/rsync
.if !defined(NOPORTDOCS)
	@${MKDIR} ${PREFIX}/share/doc/rsync
	${INSTALL_DATA} ${WRKSRC}/README ${PREFIX}/share/doc/rsync
	${INSTALL_DATA} ${WRKSRC}/COPYING ${PREFIX}/share/doc/rsync
	${INSTALL_DATA} ${WRKSRC}/tech_report.tex ${PREFIX}/share/doc/rsync
.endif

.include <bsd.port.post.mk>
