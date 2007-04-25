CC = gcc
CFLAGS = -Wall -O -g

TARGS = codemux

all: ${TARGS}

clean:
	rm -f ${TARGS} *.o *~

SHARED_OBJ = applib.o gettimeofdayex.o

CODEMUX_OBJ = codemux.o ${SHARED_OBJ}

codemux: ${CODEMUX_OBJ}

