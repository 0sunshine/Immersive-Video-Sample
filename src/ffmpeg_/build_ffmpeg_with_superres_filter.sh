#!/bin/bash -ex

EDSR_PATH="/path/to/360SR/EDSR"

mkdir -p build && cd build && rm -rf *

../configure \
    --prefix=/usr/local \
    --libdir=/usr/local/lib \
    --disable-static \
    --enable-shared \
    --enable-gpl \
    --enable-nonfree \
    --enable-superres \
    --enable-libx264 \
    --disable-optimizations \
    --disable-x86asm \
    --extra-cflags="-I${EDSR_PATH}/inference/src/" \
    --extra-ldflags="-L${EDSR_PATH}/inference/build-Release/src/" \
    --extra-libs="-l360SR -lstdc++" && \
make -j
