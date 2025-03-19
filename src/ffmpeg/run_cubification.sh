#!/bin/bash

export LD_LIBRARY_PATH=/usr/local/lib:$LD_LIBRARY_PATH
export LD_LIBRARY_PATH=/usr/local/lib64:$LD_LIBRARY_PATH

#cp ../../cubification_mdf/map_ERP3840x1920_Cubemap2880x1920/Dewarp_genx.isa ./
#cp ../../cubification_mdf/map_ERP3840x1920_Cubemap2880x1920/xCooridnate.bin ./
#cp ../../cubification_mdf/map_ERP3840x1920_Cubemap2880x1920/yCooridnate.bin ./
#./ffmpeg -hwaccel vaapi -hwaccel_output_format vaapi -i h265_3840x1920.h265 -vf erp2cubmap_mdf,hwdownload,format=nv12 -pix_fmt yuv420p -input_type 1 -rc 1 -c:v distributed_encoder -s 2880x1920 -g 15 -tile_row 6 -tile_column 9 -la_depth 2 -config_file config_high.xml -b 15M -map 0:v -y cubification_4k.h265

cp ../../tools/bin/map_ERP7680x3840_Cubemap5760x3840/Dewarp_genx.isa ./
cp ../../tools/bin/map_ERP7680x3840_Cubemap5760x3840/xCooridnate.bin ./
cp ../../tools/bin/map_ERP7680x3840_Cubemap5760x3840/yCooridnate.bin ./
./ffmpeg -hwaccel vaapi -hwaccel_output_format vaapi -i h265_7680x3840.h265 -vf erp2cubmap_mdf,hwdownload,format=nv12 -pix_fmt yuv420p -input_type 1 -rc 1 -c:v distributed_encoder -s 5760x3840 -g 25 -tile_row 6 -tile_column 9 -la_depth 2 -config_file config_high.xml -b 30M -map 0:v -y cubification_8k.h265

