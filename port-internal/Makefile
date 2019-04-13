
PORTNAME=	alligator
PORTVERSION=	1.9.33
CATEGORIES=	net-mgmt
DISTFILES=

MAINTAINER=amoshi.mandrakeuser@gmail.com
COMMENT="alligator metrics aggregator"

LICENCE=APACHE20

MY_DEPENDS=    cmake:devel/cmake \
               /usr/local/include/jansson.h:devel/jansson

BUILD_DEPENDS= ${MY_DEPENDS}
RUN_DEPENDS=   ${MY_DEPENDS}
FETCH_DEPENDS= ${MY_DEPENDS}

#WRKSRC=		${WRKDIR}/${PORTNAME}-${PORTVERSION}/
WRKSRC=		${WRKDIR}/../../

USES=		cmake:insource #,noninja
# PARSE PORT VERSION TO USE IT INSIDE CMAKE

USE_RC_SUBR=	alligator
WITH_DEBUG=	yes
PLIST_FILES=	bin/alligator

.include <bsd.port.mk>

