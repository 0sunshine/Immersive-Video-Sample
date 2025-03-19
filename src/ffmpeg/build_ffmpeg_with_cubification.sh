#!/bin/bash


../configure \
    --prefix=/usr/local \
    --libdir=/usr/local/lib \
    --enable-static \
    --disable-shared \
    --enable-gpl \
    --enable-nonfree \
    --disable-optimizations \
    --enable-libmfx \
    --enable-libDistributedEncoder \
    --enable-libvr360filter_mdf \
    #--extra-cflags='-I../libavfilter/vr360filter/MDF_SG1_PV1.2/SG1_PV1.2/ ' \
    #--extra-ldflags='-L/opt/intel/common/mdf/lib64' \
    #--extra-libs="-lstdc++ -ligfxcmrt " \
    #--extra-cxxflags="-fpermissive" \

