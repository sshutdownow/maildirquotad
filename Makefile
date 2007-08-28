#
# Copyright 2007 Igor Popov <igorpopov@newmail.ru>
# $Id$
#
PROG=	maildirquotad


WARNS ?= 3

NO_MAN= true

CFLAGS += -I/usr/local/include
LDADD  += -L/usr/local/lib -levent ${LIBUTIL}

.include <bsd.prog.mk>
