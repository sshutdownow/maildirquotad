# $$

PORTNAME=	mailquotad
PORTVERSION=	0.2
PORTREVISION=	1
CATEGORIES=	mail
MASTER_SITES=	http://maildirquotad.googlecode.com/files/
DISTFILES=	${PORTNAME}-${PORTVERSION}_1${EXTRACT_SUFX}

USE_PERL5=	yes

NO_BUILD=	yes

USE_RC_SUBR=	${PORTNAME}

MAINTAINER=	ipopov@gmail.com
COMMENT=	maildirquotad is used together with exim for checking the quotas of a mailboxes at RCPT TO time

PLIST_FILES=	sbin/mailquotad

do-install:
	${INSTALL_SCRIPT} ${WRKSRC}/mailquotad ${PREFIX}/sbin/mailquotad

post-install:
	@${MKDIR} -m 0750 /var/run/quotad
	@${CHOWN} mailnull /var/run/quotad


.include <bsd.port.mk>
