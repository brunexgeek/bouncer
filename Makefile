.POSIX:
CC      = cc
CFLAGS  = -std=c99 -Wall -Wextra -pedantic -Wconversion -Werror=return-type -Werror=incompatible-pointer-types -Werror=sign-compare -Werror=sign-conversion -Wno-missing-field-initializers -O2
LDFLAGS =
LDLIBS  =
PREFIX  = /usr/local

all: net-bouncer

net-bouncer: net-bouncer.c
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ net-bouncer.c $(LDLIBS)

install: net-bouncer
	install -d $(DESTDIR)$(PREFIX)/bin
	install -m 755 bouncer $(DESTDIR)$(PREFIX)/bin/

clean:
	rm -rf net-bouncer
