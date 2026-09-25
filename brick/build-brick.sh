#!/bin/sh
# Cross-compiles the core for the TrimUI Brick (aarch64 Linux) inside Docker and assembles
# the app folder at dist/PixelTetris/ — copy that folder to /mnt/SDCARD/Apps/ on the card.
set -eu
cd "$(dirname "$0")"

docker run --rm -v "$PWD":/src -w /src -u "$(id -u):$(id -g)" \
  "$(docker build -q - <<'DOCKERFILE'
FROM debian:bookworm-slim
RUN apt-get update && apt-get install -y --no-install-recommends gcc-aarch64-linux-gnu libc6-dev-arm64-cross make \
 && rm -rf /var/lib/apt/lists/*
DOCKERFILE
)" make CC=aarch64-linux-gnu-gcc TARGET=build/aarch64/pixeltetris_libretro.so

rm -rf dist/PixelTetris
mkdir -p dist/PixelTetris
cp build/aarch64/pixeltetris_libretro.so package/config.json package/launch.sh dist/PixelTetris/
cp ../assets/icon.png dist/PixelTetris/icon.png
chmod +x dist/PixelTetris/launch.sh
echo "built dist/PixelTetris:"
ls -l dist/PixelTetris
