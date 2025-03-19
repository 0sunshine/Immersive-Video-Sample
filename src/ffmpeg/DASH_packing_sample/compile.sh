#!/bin/sh

rm -rf ffmpeg_lib
rm -rf ParsePackingParam/build
rm -f *.o

cd ../
mkdir build_test
cd build_test
export PKG_CONFIG_PATH=/usr/local/lib/pkgconfig:$PKG_CONFIG_PATH
../configure --prefix=/usr --libdir=/usr/lib --enable-static --disable-shared --enable-pic --disable-debug --enable-gpl --enable-nonfree --disable-optimizations --disable-vaapi --enable-libDistributedEncoder --enable-libVROmafPacking
make -j$(nproc)
cd ..

cd DASH_packing_sample
mkdir ffmpeg_lib
cd ffmpeg_lib
rm -f *.*
cp ../../build_test/libavcodec/libavcodec.a ./
cp ../../build_test/libavdevice/libavdevice.a ./
cp ../../build_test/libavfilter/libavfilter.a ./
cp ../../build_test/libavformat/libavformat.a ./
cp ../../build_test/libavutil/libavutil.a ./
cp ../../build_test/libpostproc/libpostproc.a ./
cp ../../build_test/libswresample/libswresample.a ./
cp ../../build_test/libswscale/libswscale.a ./
cd ..

cd ParsePackingParam
mkdir build
cd build
cmake ..
make
sudo make install
cd ../..

gcc -c -z noexecstack -z relro -z now -fstack-protector-strong -fPIE -pie -fPIC -O2 -D_FORTIFY_SOURCE=2 -Wformat -Wformat-security -Wl,-S -Wall -Werror DASH_packing_sample.c
gcc -I/usr/include/ -L/usr/local/lib DASH_packing_sample.o -o DASH_packing_sample -L/usr/local/lib -z now -fPIE -pie -fPIC -Wl,--gc-sections -Wl,--strip-all -lpthread -lm -llzma -lz /usr/lib64/libbz2.so.1 /usr/local/lib/libParsePackingParam.so /usr/local/lib/libDistributedEncoder.so /usr/local/lib/libthrift-0.12.0.so ./ffmpeg_lib/libavformat.a ./ffmpeg_lib/libavcodec.a ./ffmpeg_lib/libavutil.a ./ffmpeg_lib/libswscale.a ./ffmpeg_lib/libswresample.a /usr/local/lib/lib360SCVP.so -lglog /usr/local/lib/libVROmafPacking.so
