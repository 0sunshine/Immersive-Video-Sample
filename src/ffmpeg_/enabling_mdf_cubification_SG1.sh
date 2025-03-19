#!/bin/bash

curr_dir=`pwd`

if [ ! -e "/opt/intel/mediasdk/include/mfx" ] ; then
    cd /opt/intel/mediasdk/include
    mkdir mfx
    cd mfx
    cp ../*.h ./
    cd ..
fi

cd $curr_dir
cd ../hwaccel/vr360filter_mdf/
mkdir build
cd build
cmake ..
make -j8
make install
cd ../

cd ../../FFmpeg
mkdir build_with_cubification
cd build_with_cubification
cp ../build_ffmpeg_with_cubification.sh ./
export PKG_CONFIG_PATH=/opt/intel/mediasdk/lib64/pkgconfig:$PKG_CONFIG_PATH
export PKG_CONFIG_PATH=/usr/local/lib/pkgconfig:$PKG_CONFIG_PATH
./build_ffmpeg_with_cubification.sh
make -j8

#mv ../h265_3840x1920.h265 ./
mv ../h265_7680x3840.h265 ./
cp ../config_high.xml ./
cp ../run_cubification.sh ./
./run_cubification.sh
