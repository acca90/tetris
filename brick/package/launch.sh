#!/bin/sh
# Pixel Boy Tetris — runs the bundled libretro core in the stock RetroArch, no content needed.
echo $0 $*
progdir=$(cd "$(dirname "$0")" && pwd)
RA_DIR=/mnt/SDCARD/RetroArch
cd $RA_DIR/
HOME=$RA_DIR/ $RA_DIR/ra64.trimui -v -L "$progdir/pixeltetris_libretro.so"
