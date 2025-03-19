#!/bin/sh

g++ -g -c transcoding_sample.cpp
g++ -I/usr/include/ -L/usr/local/lib transcoding_sample.o -o transcoding_sample -L/usr/local/lib -lpthread -lm -llzma -lz /usr/lib64/libbz2.so.1 /usr/local/lib/libParseEncodingParam.so /usr/local/lib/libDistributedEncoder.so /usr/local/lib/libthrift-0.12.0.so ./ffmpeg_lib/libavformat.a ./ffmpeg_lib/libavcodec.a ./ffmpeg_lib/libavutil.a ./ffmpeg_lib/libswscale.a ./ffmpeg_lib/libswresample.a /usr/local/lib/lib360SCVP.so /usr/lib64/libopenhevc.so -lglog
