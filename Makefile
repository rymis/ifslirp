
CC = gcc
PKGS = slirp
PKG_CFLAGS != pkg-config $(PKGS) --cflags
PKG_LIBS != pkg-config $(PKGS) --libs

CFLAGS = -Wall -W -O2 -g $(PKG_CFLAGS)
LDFLAGS = -Wall -W -O2 -g $(PKG_LIBS) -lpthread

SRCS = $(wildcard *.c)
OBJS = $(patsubst %.c,%.o,$(SRCS))

PROG = ifslirp

all: $(PROG)

$(PROG): $(OBJS)
	$(CC) -o $(PROG) $(OBJS) $(LDFLAGS)

%.o: %.c
	$(CC) -c $(CFLAGS) $<

clean:
	rm -f *.o *core $(PROG)

