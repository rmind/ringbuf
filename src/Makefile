#
# This file is in the Public Domain.
#

CFLAGS=		-std=gnu99 -O2 -flto -g -W -Wextra -Werror -D_BSD_SOURCE
CFLAGS+=	-Wstrict-prototypes -Wmissing-prototypes -Wpointer-arith
CFLAGS+=	-Wshadow -Wcast-qual -Wwrite-strings

ifeq ($(DEBUG),1)
CFLAGS+=	-O0 -DDEBUG -fno-omit-frame-pointer
else
CFLAGS+=	-DNDEBUG
LDFLAGS=	-flto
endif

LIB=		libringbuf
INCS=		ringbuf.h

OBJS=		ringbuf.o

$(LIB).la:	LDFLAGS+=	-rpath $(LIBDIR)
install/%.la:	ILIBDIR=	$(DESTDIR)/$(LIBDIR)
install:	IINCDIR=	$(DESTDIR)/$(INCDIR)/
#install:	IMANDIR=	$(DESTDIR)/$(MANDIR)/man3/

lib: $(LIB).la

%.lo: %.c
	libtool --mode=compile --tag CC $(CC) $(CFLAGS) -c $<

$(LIB).la: $(shell echo $(OBJS) | sed 's/\.o/\.lo/g')
	libtool --mode=link --tag CC $(CC) $(LDFLAGS) -o $@ $(notdir $^)

install/%.la: %.la
	mkdir -p $(ILIBDIR)
	libtool --mode=install install -c $(notdir $@) $(ILIBDIR)/$(notdir $@)

install: $(addprefix install/,$(LIB).la)
	libtool --mode=finish $(LIBDIR)
	mkdir -p $(IINCDIR) && install -c $(INCS) $(IINCDIR)
	#mkdir -p $(IMANDIR) && install -c $(MANS) $(IMANDIR)

clean:
	libtool --mode=clean rm
	@ rm -rf .libs *.o *.lo *.la

.PHONY: all lib install clean
