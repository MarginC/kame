#	$Id: Makefile,v 1.8 1997/12/13 20:38:12 pst Exp $
#	$NetBSD: Makefile,v 1.15 1997/10/18 15:31:20 lukem Exp $
#	from: @(#)Makefile	8.2 (Berkeley) 4/3/94

# Uncomment the following to provide defaults for gate-ftp operation
#
#CFLAGS+=-DGATE_SERVER=\"ftp-gw.host\" # -DGATE_PORT=21

PROG=	ftp
SRCS=	cmds.c cmdtab.c complete.c domacro.c fetch.c ftp.c main.c ruserpass.c \
	util.c
CFLAGS+=-I${.CURDIR}/../../contrib-crypto/telnet
CFLAGS+=-DINET6

LDADD+=	-ledit -ltermcap
DPADD+=	${LIBEDIT} ${LIBTERMCAP}
LDADD+= -L${.OBJDIR}/../../lib/libinet6 \
        -L/usr/local/v6/lib -linet6
DPADD+= ${.OBJDIR}/../../lib/libinet6/libinet6.a \
        /usr/local/v6/lib/libinet6.a

LINKS=	${BINDIR}/ftp ${BINDIR}/pftp \
	${BINDIR}/ftp ${BINDIR}/gate-ftp
MLINKS=	ftp.1 pftp.1 \
	ftp.1 gate-ftp.1
#SRCS=	cmds.c cmdtab.c ftp.c main.c ruserpass.c domacro.c
#LINKS=  ${BINDIR}/ftp ${BINDIR}/pftp
#MLINKS= ftp.1 pftp.1

.include <bsd.prog.mk>
