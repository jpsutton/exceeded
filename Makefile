CC ?= gcc
PREFIX ?= /usr/local
CFLAGS ?= -O2 -g -Wall -Wextra -Wno-unused-parameter
SRC := $(wildcard src/*.c)
OBJ := $(SRC:src/%.c=build/%.o)

SDL_CFLAGS := $(shell pkg-config --cflags sdl2 2>/dev/null)
SDL_LIBS := $(shell pkg-config --libs sdl2 2>/dev/null)
FS_CFLAGS := $(shell pkg-config --cflags fluidsynth 2>/dev/null)
FS_LIBS := $(shell pkg-config --libs fluidsynth 2>/dev/null)
ZLIB_CFLAGS := $(shell pkg-config --cflags zlib 2>/dev/null)
ZLIB_LIBS := $(shell pkg-config --libs zlib 2>/dev/null)
ifeq ($(ZLIB_LIBS),)
ZLIB_LIBS := -lz
endif
ifeq ($(SDL_LIBS),)
EXTRA := -DNO_SDL $(ZLIB_CFLAGS)
else
EXTRA := $(SDL_CFLAGS) $(ZLIB_CFLAGS)
LIBS := $(SDL_LIBS)
ifneq ($(FS_LIBS),)
EXTRA += $(FS_CFLAGS) -DHAVE_FLUIDSYNTH
LIBS += $(FS_LIBS)
endif
endif

engine: $(OBJ)
	$(CC) $(CFLAGS) -o $@ $(OBJ) $(LIBS) $(ZLIB_LIBS) -lpthread -lm

build/%.o: src/%.c $(wildcard src/*.h) | build
	$(CC) $(CFLAGS) $(EXTRA) -c -o $@ $<

build:
	mkdir -p build

clean:
	rm -rf build engine dist

# distribution folder: the program with the bundled SoundFont next to it (soundfont/GeneralUser-GS.sf2 + LICENSE.txt)
dist: engine
	mkdir -p dist/soundfont
	cp engine dist/engine
	cp soundfont/* dist/soundfont/
	cp README.md dist/

# system install (packages use DESTDIR and PREFIX=/usr): the program is `exceeded`, its SoundFont is found at ../share/exceeded/soundfont
install: engine
	install -Dm755 engine $(DESTDIR)$(PREFIX)/bin/exceeded
	install -Dm644 soundfont/GeneralUser-GS.sf2 $(DESTDIR)$(PREFIX)/share/exceeded/soundfont/GeneralUser-GS.sf2
	install -Dm644 soundfont/LICENSE.txt $(DESTDIR)$(PREFIX)/share/licenses/exceeded/GeneralUser-GS-LICENSE.txt
	install -Dm644 packaging/linux/exceeded.desktop $(DESTDIR)$(PREFIX)/share/applications/exceeded.desktop

.PHONY: clean dist install
