#!/bin/sh

rm -rf ffmpeg_lib
rm -rf ParseEncodingParam/build
rm -f *.o

cd ../
rm -rf build_transcoder
mkdir build_transcoder
cd build_transcoder
export PKG_CONFIG_PATH=/usr/local/lib/pkgconfig:$PKG_CONFIG_PATH
../configure --prefix=/usr --libdir=/usr/lib --enable-static --disable-shared --enable-pic --enable-debug --enable-gpl --enable-nonfree --disable-optimizations --disable-vaapi --enable-libopenhevc --enable-libDistributedEncoder
make -j$(nproc)
cd ..

cd transcoding_sample
mkdir ffmpeg_lib
cd ffmpeg_lib
rm -f *.*
cp ../../build_transcoder/libavcodec/libavcodec.a ./
cp ../../build_transcoder/libavdevice/libavdevice.a ./
cp ../../build_transcoder/libavfilter/libavfilter.a ./
cp ../../build_transcoder/libavformat/libavformat.a ./
cp ../../build_transcoder/libavutil/libavutil.a ./
cp ../../build_transcoder/libpostproc/libpostproc.a ./
cp ../../build_transcoder/libswresample/libswresample.a ./
cp ../../build_transcoder/libswscale/libswscale.a ./
cd ..

cd ParseEncodingParam
mkdir build
cd build
cmake ..
make
sudo make install
cd ../..

g++ -g -c -z noexecstack -z relro -z now -fstack-protector-strong -fPIE -pie -fPIC -O2 -D_FORTIFY_SOURCE=2 -Wformat -Wformat-security -Wl,-S -Wall -Werror transcoding_sample.cpp
g++ -I/usr/include/ -L/usr/local/lib transcoding_sample.o -o transcoding_sample -L/usr/local/lib -z now -fPIE -pie -fPIC -Wl,--gc-sections -Wl,--strip-all -lpthread -lm -llzma -lz /usr/lib64/libbz2.so.1 /usr/local/lib/libParseEncodingParam.so /usr/local/lib/libDistributedEncoder.so /usr/local/lib/libthrift-0.12.0.so ./ffmpeg_lib/libavformat.a ./ffmpeg_lib/libavcodec.a ./ffmpeg_lib/libavutil.a ./ffmpeg_lib/libswscale.a ./ffmpeg_lib/libswresample.a /usr/local/lib/lib360SCVP.so /usr/lib64/libopenhevc.so -lglog
