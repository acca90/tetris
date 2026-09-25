# Pixel Boy Tetris — libretro core
#   make                         native build (desktop RetroArch, tests)
#   make CC=aarch64-linux-gnu-gcc TARGET=build/aarch64/pixeltetris_libretro.so   cross build
#   ./build-brick.sh             cross build for the TrimUI Brick inside Docker + package
#   make assets                  re-embed ../assets/*.png after regenerating the art

CC      ?= cc
CFLAGS  ?= -O2
CFLAGS  += -std=c99 -fPIC -Wall -Wextra -Wno-misleading-indentation
LDFLAGS += -shared -Wl,--no-undefined
TARGET  ?= build/native/pixeltetris_libretro.so

all: $(TARGET)

$(TARGET): src/pixeltetris.c src/assets.h src/libretro.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ src/pixeltetris.c -lc

assets:
	python3 tools/png2c.py

clean:
	rm -rf build

.PHONY: all assets clean
