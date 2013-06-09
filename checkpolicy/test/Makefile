#
# Makefile for building the dispol program
#
PREFIX ?= $(DESTDIR)/usr
BINDIR=$(PREFIX)/bin
LIBDIR=$(PREFIX)/lib
INCLUDEDIR ?= $(PREFIX)/include

SE_CFLAGS ?= -g -Wall -W -Werror -O2 -pipe
override SE_CFLAGS += -I$(INCLUDEDIR) $(CFLAGS)

LDLIBS=-shared -fPIC -lfl -lselinux -lsepol -L$(LIBDIR)

all: dispol dismod

dispol: dispol.o

dismod: dismod.o

clean:
	-rm -f dispol dismod *.o 
