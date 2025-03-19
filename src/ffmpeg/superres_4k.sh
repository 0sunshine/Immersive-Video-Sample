#!/bin/bash -ex

TORCH_PREFIX="/path/to/libtorch"
CUDNN_PREFIX="/usr/local/lib/python3.7/dist-packages/nvidia/cudnn"
CUDA_PREFIX="/usr/local/cuda-12.0"
# input_width x rows = image_width
# input_height x columns = image_height
ORIGIN_WIDTH="3840"
ORIGIN_HEIGHT="1920"
ROWS="8"
COLUMNS="6"
INPUT_WIDTH="`expr ${ORIGIN_WIDTH} / ${ROWS}`"
INPUT_HEIGHT="`expr ${ORIGIN_HEIGHT} / ${COLUMNS}`"
SPLIT_STRING="${ROWS}x${COLUMNS}"
INPUT_RESOLUTION="${INPUT_WIDTH}x${INPUT_HEIGHT}"
EDSR_PATH="/path/to/tids-internal/360SR/EDSR"
MODEL_PATH="${EDSR_PATH}/traced_EDSR_model_${INPUT_RESOLUTION}.pt"
LIB360SR_PATH="${EDSR_PATH}/inference/build-Release/src/"

FFMPEG_SOURCE="./build/"
FFMPEG_BINARY="${FFMPEG_SOURCE}/ffmpeg_g"
AVCODEC_PATH="${FFMPEG_SOURCE}/libavcodec/"
AVDEVICE_PATH="${FFMPEG_SOURCE}/libavdevice/"
AVFILTER_PATH="${FFMPEG_SOURCE}/libavfilter/"
AVFORMAT_PATH="${FFMPEG_SOURCE}/libavformat/"
AVUTIL_PATH="${FFMPEG_SOURCE}/libavutil/"
POSTPROC_PATH="${FFMPEG_SOURCE}/libpostproc/"
SWRESAMPLE_PATH="${FFMPEG_SOURCE}/libswresample/"
SWSCALE_PATH="${FFMPEG_SOURCE}/libswscale/"
INPUT_VIDEO="./test_3840x1920.mp4"
OUTPUT_VIDEO="${FFMPEG_SOURCE}/output_8k.mp4"

# this is the options of filter "superres"
# MODEL_PATH       - path to torch script file
# SPLIT_STRING     - string of model input resolution
# INPUT_RESOLUTION - string of input split
SUPERRES_PARAM="model=${MODEL_PATH}:split_string=${SPLIT_STRING}:resolution=${INPUT_RESOLUTION}"

export LD_LIBRARY_PATH=${AVCODEC_PATH}:${LD_LIBRARY_PATH} && \
export LD_LIBRARY_PATH=${AVDEVICE_PATH}:${LD_LIBRARY_PATH} && \
export LD_LIBRARY_PATH=${AVFILTER_PATH}:${LD_LIBRARY_PATH} && \
export LD_LIBRARY_PATH=${AVFORMAT_PATH}:${LD_LIBRARY_PATH} && \
export LD_LIBRARY_PATH=${POSTPROC_PATH}:${LD_LIBRARY_PATH} && \
export LD_LIBRARY_PATH=${SWRESAMPLE_PATH}:${LD_LIBRARY_PATH} && \
export LD_LIBRARY_PATH=${LIB360SR_PATH}:${LD_LIBRARY_PATH}

${FFMPEG_BINARY} \
    -i ${INPUT_VIDEO} \
    -vf superres=${SUPERRES_PARAM} \
    -vframes 25 \
    -y ${OUTPUT_VIDEO}
