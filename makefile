CC ?= gcc
#CC ?= clang

CFLAGS  ?= -std=c99 -pedantic -Wall -Wextra -O3 -s -pipe
LDFLAGS ?=

XCFLAGS  ?= -std=c99 -pedantic -Wall -Wextra -I/usr/include/X11/ -O3 -s -pipe
XLDFLAGS ?= -lX11

DRMCFLAGS  ?= -std=c99 -D_GNU_SOURCE -pedantic -Wall -Wextra -O3 -s -pipe -DUSE_DRM $(shell pkg-config --cflags libdrm)
DRMLDFLAGS ?= $(shell pkg-config --libs libdrm)

HDR = glyph.h yaft.h conf.h color.h parse.h terminal.h util.h \
	ctrlseq/esc.h ctrlseq/csi.h ctrlseq/osc.h ctrlseq/dcs.h \
	fb/common.h fb/linux.h fb/drm.h fb/freebsd.h fb/netbsd.h fb/openbsd.h \
	x/x.h

DESTDIR   =
PREFIX    = $(DESTDIR)/usr
MANPREFIX = $(DESTDIR)/usr/share/man

all: yaft

yaft: mkfont_bdf

yaft-drm: mkfont_bdf

yaft-drm-meslo:

yaft-drm-terminus:

yaftx: mkfont_bdf

mkfont_bdf: tools/mkfont_bdf.c tools/mkfont_bdf.h tools/bdf.h tools/util.h
	$(CC) -o $@ $< $(CFLAGS) $(LDFLAGS)

glyph.h: mkfont_bdf
	./mkfont_bdf table/alias fonts/milkjf_k16.bdf fonts/milkjf_8x16r.bdf fonts/milkjf_8x16.bdf > glyph.h

yaft: yaft.c $(HDR)
	$(CC) -o $@ $< $(CFLAGS) $(LDFLAGS)

yaft-drm: yaft.c $(HDR)
	$(CC) -o $@ $< $(DRMCFLAGS) $(DRMLDFLAGS)

yaft-drm-meslo: yaft.c $(HDR)
	cp glyph_meslo.h glyph.h
	$(CC) -o $@ $< $(DRMCFLAGS) $(DRMLDFLAGS)

yaft-drm-terminus: yaft.c $(HDR)
	cp glyph_terminus.h glyph.h
	$(CC) -o $@ $< $(DRMCFLAGS) $(DRMLDFLAGS)

yaftx: x/yaftx.c $(HDR)
	$(CC) -o $@ $< $(XCFLAGS) $(XLDFLAGS)

install:
	mkdir -p $(PREFIX)/share/terminfo
	tic -o $(PREFIX)/share/terminfo info/yaft.src
	mkdir -p $(PREFIX)/bin/
	install -m755 ./yaft $(PREFIX)/bin/yaft
	install -m755 ./yaft_wall $(PREFIX)/bin/yaft_wall
	mkdir -p $(MANPREFIX)/man1/
	install -m644 ./man/yaft.1 $(MANPREFIX)/man1/yaft.1

install-drm:
	mkdir -p $(PREFIX)/share/terminfo
	tic -o $(PREFIX)/share/terminfo info/yaft.src
	mkdir -p $(PREFIX)/bin/
	install -m755 ./yaft-drm-meslo $(PREFIX)/bin/yaft-drm-meslo
	install -m755 ./yaft-drm-terminus $(PREFIX)/bin/yaft-drm-terminus
	ln -sf yaft-drm-meslo $(PREFIX)/bin/yaft-drm

installx:
	mkdir -p $(PREFIX)/share/terminfo
	tic -o $(PREFIX)/share/terminfo info/yaft.src
	mkdir -p $(PREFIX)/bin/
	install -m755 ./yaftx $(PREFIX)/bin/yaftx

uninstall:
	rm -f $(PREFIX)/bin/yaft
	rm -f $(PREFIX)/bin/yaft-drm $(PREFIX)/bin/yaft-drm-meslo $(PREFIX)/bin/yaft-drm-terminus
	rm -f $(PREFIX)/bin/yaft_wall

uninstallx:
	rm -f $(PREFIX)/bin/yaftx

clean:
	rm -f yaft yaft-drm yaft-drm-meslo yaft-drm-terminus yaftx mkfont_bdf glyph.h
