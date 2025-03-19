/*
 * Intel tile encoder
 *
 * Copyright (c) 2018 Intel Cooperation
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#ifndef TILE_ENCODER_H
#define TILE_ENCODER_H
#define FILE_DEBUG

#include "libavutil/fifo.h"

#include "avcodec.h"
#include "internal.h"
#include <stdio.h>
#include <pthread.h>
#include <unistd.h>
#include <stdbool.h>

#include "genTiledstreamAPI.h"

#define MAX_TILES 256
#define FIFO_SIZE 8024

enum ENC_LIB{
    ENC_NULL = 0,
    ENC_X265 = 1,
    ENC_SVT  = 2
};

enum TILE_MODE{
    NULL_MODE = 0,
    FIX_SIZE  = 1,
    AVG_SIZE  = 2
};
typedef int (*ENC_CLOSE)(void*);
typedef int (*ENC_INIT)(void*);
typedef int (*ENC_FRAME)(void*, AVPacket*, const AVFrame*, int*);

typedef struct TileInfo{
        int            top;
        int            left;
        int            tWidth;
        int            tHeight;
        int            tBitrate;
        int            tMaxrate;
        AVFifoBuffer*  outpkt_fifo;
        int            proc_idx;
        int            eos;
        void*          enc_ctx;
        AVPacket*      internal_pkt;
#ifdef FILE_DEBUG
        int            nGetpkt;
        int            nSpkt;
        FILE*          file;
#endif
} TileInfo;

typedef struct TileEncoderInfo{
    void          *ctx;
    int           tile_idx;
}TileEncoderInfo;

typedef struct EncoderWrapper{
        AVCodecContext* avctx;

        int             width;
        int             height;
        void*           enc_param;

        bool            uniform_split;
        int             tile_num;
        int             tile_w;
        int             tile_h;
        TileInfo        tile_info[MAX_TILES];

        ENC_CLOSE       enc_close;
        ENC_INIT        enc_init;
        ENC_FRAME       enc_frame;

        TileEncoderInfo *tile_enc_info;
        pthread_t       *tid;
        int             initialized;

        void            *pGen;
        param_gen_tiledStream paramTiledStream;
        pthread_mutex_t mutex;
        pthread_cond_t cond;
} EncoderWrapper;

int get_tile_frame_copy(EncoderWrapper* wrapper, int tile_idx, const AVFrame *pic, AVFrame** tile_pic );
int get_tile_frame_nocopy(EncoderWrapper* wrapper, int tile_idx, const AVFrame *pic, AVFrame** tile_pic );

int bs_tile_stitching(EncoderWrapper* wrapper, AVPacket* outPkt);
int get_tile_bitrate(EncoderWrapper* wrapper, int idx);
int get_tile_maxrate(EncoderWrapper* wrapper, int idx);

int libx265_enc_close(void* ctx);
int libx265_enc_init(void* ctx);
int libx265_enc_frame(void* ctx, AVPacket *pkt, const AVFrame *pic, int *got_packet);

int svt_enc_close(void* ctx);
int svt_enc_init(void* ctx);
int svt_enc_frame(void* ctx, AVPacket *pkt, const AVFrame *pic, int *got_packet);
int svt_enc_tile(TileEncoderInfo *tile_enc_info);
int bFifoReady( EncoderWrapper* wrapper );

#endif /* TILE_ENCODER_H */

