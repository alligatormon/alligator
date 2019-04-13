
PORTNAME=	alligator
PORTVERSION=	1.9.33
CATEGORIES=	net-mgmt
MASTER_SITES=https://dl.bintray.com/alligatormon/sources/

MAINTAINER=amoshi.mandrakeuser@gmail.com
COMMENT="alligator metrics aggregator"

LICENCE=APACHE20

MY_DEPENDS=    cmake:devel/cmake \
               /usr/local/include/jansson.h:devel/jansson

BUILD_DEPENDS= ${MY_DEPENDS}
RUN_DEPENDS=   ${MY_DEPENDS}
FETCH_DEPENDS= ${MY_DEPENDS}

post-fetch:
	${MKDIR} ${WRKDIR}/alligator

WRKSRC=		${WRKDIR}/${PORTNAME}-${PORTVERSION}/
#WRKSRC=		${WRKDIR}/../../

USES=		cmake:noninja,insource #:insource #,noninja
# PARSE PORT VERSION TO USE IT INSIDE CMAKE

USE_RC_SUBR=	alligator
WITH_DEBUG=	yes
PLIST_FILES=	bin/alligator

.include <bsd.port.mk>

