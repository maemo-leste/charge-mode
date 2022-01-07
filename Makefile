SDL2_CFLAGS := $(shell sdl2-config --cflags)
SDL2_LIBS  := $(shell sdl2-config --libs)

CC       := gcc
CCFLAGS   := -g -I. $(SDL2_CFLAGS)

LIBS       := $(SDL2_LIBS) -lm

SOURCES    := ${wildcard *.c}
OBJECTS    := $(SOURCES:%.c=%.o)
INSTALL := install -o root -g root --mode=755
INSTALL_DIR := install -d
BINDIR := $(DESTDIR)/usr/bin
INITDIR := $(DESTDIR)/etc/init.d

all: charging_sdl

%.o: %.c
	@echo CC $<
	@$(CC) -c -o $@ $< $(CCFLAGS) $(if $(LIBBATTERY),-DUSE_LIBBATTERY)

charging_sdl: $(OBJECTS)
	@echo LD $@
	@$(CC) -o $@ $^ $(CCFLAGS) $(LIBS)

install: all
	$(INSTALL_DIR) $(BINDIR)
	$(INSTALL_DIR) $(INITDIR)
	$(INSTALL) charging_sdl $(BINDIR)
	$(INSTALL) charge-mode.sh $(BINDIR)
	$(INSTALL) charge-mode $(INITDIR)

.PHONY: clean

clean:
	-rm -fv *.o charging_sdl
