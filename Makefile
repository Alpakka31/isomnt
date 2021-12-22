OBJS = isomnt.o
CC := gcc
CFLAGS := -Wall -Wextra -pedantic -Wformat=2 -O2
PREFIX := /usr/local
BINDIR := ${PREFIX}/bin
PROGNAME = isomnt

.PHONY: all clean

all: $(OBJS)
	$(CC) $(CFLAGS) $(OBJS) -o ${PROGNAME}

isomnt.o: isomnt.c
	$(CC) $(CFLAGS) -c ${PROGNAME}.c

install:
	install -Dm755 ${PROGNAME} $(DESTDIR)$(BINDIR)/${PROGNAME}

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/${PROGNAME}

clean:
	rm -f ${PROGNAME} $(OBJS)