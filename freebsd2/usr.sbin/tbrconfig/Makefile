PROG=	tbrconfig
SRCS=	tbrconfig.c

MAN8=	tbrconfig.8

LDADD+=	-L${.OBJDIR}/../../lib/libinet6 \
	-L/usr/local/v6/lib -linet6
DPADD+= ${.OBJDIR}/../../lib/libinet6/libinet6.a \
	/usr/local/v6/lib/libinet6.a

.PATH:	${.CURDIR}/../../kame/tbrconfig

.include <bsd.prog.mk>
