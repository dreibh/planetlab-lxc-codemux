CC = gcc
CFLAGS = -Wall -O 

TARGS = codemux

all: ${TARGS}

clean:
	rm -f ${TARGS} *.o *~

SHARED_OBJ = codemuxlib.o debug.o

CODEMUX_OBJ = codemux.o ${SHARED_OBJ}

codemux: ${CODEMUX_OBJ}

install:
	install -D -m 0755 -o root -g root codemux.initscript $(INSTALL_ROOT)/etc/rc.d/init.d/codemux
	install -D -m 0644 -o root -g root codemux.conf $(INSTALL_ROOT)/etc/codemux/codemux.conf
	install -D -m 0755 -o root -g root codemux $(INSTALL_ROOT)/usr/sbin/codemux
