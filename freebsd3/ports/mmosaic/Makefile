# New ports collection makefile for:    mMosaic
# Date created:         Fri Apr 14 10:36:57 JST 2000
# Whom:                 sumikawa
#
# $Id: Makefile,v 1.3 2000/06/25 10:58:38 sumikawa Exp $
#

PORTNAME=	mMosaic
PORTVERSION=	3.6.2
CATEGORIES=	www
MASTER_SITES=	ftp://tsi.enst.fr/pub/multicast/mMosaic/

REQUIRES_MOTIF=	yes

MAINTAINER=	sumikawa@kame.net

LIB_DEPENDS=	jpeg.9:${PORTSDIR}/graphics/jpeg \
		png.3:${PORTSDIR}/graphics/png

USE_NEWGCC=	yes
WRKSRC=		${WRKDIR}/${DISTNAME:S/-/-src-/}

.include <bsd.port.mk>
