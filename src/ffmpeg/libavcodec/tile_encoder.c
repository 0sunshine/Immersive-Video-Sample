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

#include <stdint.h>
#include <string.h>

#include "libavutil/attributes.h"
#include "libavutil/avassert.h"
#include "libavutil/dict.h"
#include "libavutil/error.h"
#include "libavutil/imgutils.h"
#include "libavutil/internal.h"
#include "libavutil/log.h"
#include "libavutil/mem.h"
#include "libavutil/pixdesc.h"
#include "libavutil/opt.h"

#include "tile_encoder.h"

//pthread_mutex_t mutex;
//pthread_cond_t cond;

typedef struct TileEncoderContext {
    const AVClass *class;
    EncoderWrapper api;
    enum ENC_LIB   enc_lib;
    enum TILE_MODE tile_mode;

    //for average size tile, the input just give the layout, such as 3x3, 4x4
    int            tiles_gw;
    int            tiles_gh;

    //for fix size tile, the last one of colum or row is not the fixed value
    int            fix_tiles_w;
    int            fix_tiles_h;
    char          *params;
} TileEncoderContext;

// Support fix size tile
static int assign_tiles_fix( TileEncoderContext* ctx )
{
    EncoderWrapper* wrapper = &(ctx->api);

    int *tiles_col_width, *tiles_row_height;
    tiles_col_width = (int *)malloc(ctx->tiles_gw * sizeof(int));
    tiles_row_height = (int *)malloc(ctx->tiles_gh * sizeof(int));
    for (int i=0;i<ctx->tiles_gw - 1;i++)tiles_col_width[i]=ctx->fix_tiles_w;
    for (int i=0;i<ctx->tiles_gh - 1;i++)tiles_row_height[i]=ctx->fix_tiles_h;

    wrapper->tile_num = ctx->tiles_gw * ctx->tiles_gh;
    wrapper->tile_w = ctx->tiles_gw;
    wrapper->tile_h = ctx->tiles_gh;

    for(int i = 0; i < ctx->tiles_gh; i++)
    {
        for(int j = 0; j < ctx->tiles_gw; j++)
        {
            int idx = i * ctx->tiles_gw + j;
            wrapper->tile_info[idx].left    = (j == 0) ? 0 : wrapper->tile_info[idx - 1].left + tiles_col_width[j-1];
            wrapper->tile_info[idx].top     = (i == 0) ? 0 : wrapper->tile_info[(i-1)*ctx->tiles_gw + j].top + tiles_row_height[i-1];
            wrapper->tile_info[idx].tHeight = (i == ctx->tiles_gh - 1) ? wrapper->height - wrapper->tile_info[idx].top : tiles_row_height[i];
            wrapper->tile_info[idx].tWidth  = (j == ctx->tiles_gw - 1) ? wrapper->width - wrapper->tile_info[idx].left : tiles_col_width[j];
        }
    }

    if(tiles_col_width)
    {
        free(tiles_col_width);
        tiles_col_width = NULL;
    }
    if(tiles_row_height)
    {
        free(tiles_row_height);
        tiles_row_height = NULL;
    }

    return 0;
}
static int assign_tiles_avg( TileEncoderContext* ctx )
{
    EncoderWrapper* wrapper = &(ctx->api);

    wrapper->tile_num = ctx->tiles_gw * ctx->tiles_gh;
    wrapper->tile_w = ctx->tiles_gw;
    wrapper->tile_h = ctx->tiles_gh;

#define LCU_SIZE 64

    // Width and Height should be divisible by LCU_SIZE
    int width_in_lcu = wrapper->width / LCU_SIZE;
    int height_in_lcu = wrapper->height / LCU_SIZE;

    // (6.5.1) in Rec. ITU-T H.265 v5 (02/2018)
    int *tiles_col_width, *tiles_row_height;
    tiles_col_width = (int *)malloc(ctx->tiles_gw * sizeof(int));
    tiles_row_height = (int *)malloc(ctx->tiles_gh * sizeof(int));
    for( int i=0; i<ctx->tiles_gw; i++)
    {
        tiles_col_width[i] = (i+1) * width_in_lcu / ctx->tiles_gw - i * width_in_lcu / ctx->tiles_gw;
    }
    for( int i=0; i<ctx->tiles_gh; i++)
    {
        tiles_row_height[i] = (i+1) * height_in_lcu / ctx->tiles_gh - i * height_in_lcu / ctx->tiles_gh;

    }

    for(int i = 0; i < ctx->tiles_gh; i++)
    {
        for(int j = 0; j < ctx->tiles_gw; j++)
        {
            int idx = i * ctx->tiles_gw + j;
            wrapper->tile_info[idx].left    = (j == 0) ? 0 : wrapper->tile_info[idx - 1].left + tiles_col_width[j-1] * LCU_SIZE;
            wrapper->tile_info[idx].top     = (i == 0) ? 0 : wrapper->tile_info[(i-1)*ctx->tiles_gw + j].top + tiles_row_height[i-1] * LCU_SIZE;
            wrapper->tile_info[idx].tHeight = tiles_row_height[i] * LCU_SIZE;
            wrapper->tile_info[idx].tWidth  = tiles_col_width[j] * LCU_SIZE;
        }
    }

    if(tiles_col_width)
    {
        free(tiles_col_width);
        tiles_col_width = NULL;
    }
    if(tiles_row_height)
    {
        free(tiles_row_height);
        tiles_row_height = NULL;
    }

    return 0;
}

/// assign bit rate for each tile.
int get_tile_bitrate(EncoderWrapper* wrapper, int idx)
{
    int bit_rate = wrapper->avctx->bit_rate;
    double percent = 0.0;

    if( 0==bit_rate ) bit_rate = wrapper->avctx->bit_rate_tolerance;

    ///FIXME if there is more suitable way to calculate bit rate for each tile
    percent = (double)( wrapper->tile_info[idx].tHeight * wrapper->tile_info[idx].tWidth ) / (double)(wrapper->width * wrapper->height);

    return (int) (bit_rate * percent);

 }

int get_tile_maxrate(EncoderWrapper* wrapper, int idx)
{
    int max_rate = wrapper->avctx->rc_max_rate;

    ///FIXME if there is more suitable way to calculate bit rate for each tile
    double percent = (double)( wrapper->tile_info[idx].tHeight * wrapper->tile_info[idx].tWidth ) / (double)(wrapper->width * wrapper->height);

    return (int) (max_rate * percent);

}

int bFifoReady( EncoderWrapper* wrapper )
{
    int eos = 0;
    int ready = 0;
    for(int i=0; i<wrapper->tile_num; i++){
        if( wrapper->tile_info[i].outpkt_fifo ){
            if( av_fifo_size(wrapper->tile_info[i].outpkt_fifo) ){
                ready++;
            }else{
                if(wrapper->tile_info[i].eos) eos++;
            }
        }
    }
    if( ready == wrapper->tile_num ) return 1;
    if( eos == wrapper->tile_num ) return AVERROR_EOF;

    return 0;
}
int bs_tile_stitching(EncoderWrapper* wrapper, AVPacket* outPkt)
{
    int ret = 0;
    AVPacket pkt[MAX_TILES];
    int bReady = bFifoReady(wrapper);
    int totalsize=0;
    uint8_t* dst = NULL;
    if( AVERROR_EOF == bReady ) return AVERROR_EOF;

    if( 1 == bReady ){
        for(int i=0; i<wrapper->tile_num; i++){
            av_fifo_generic_read( wrapper->tile_info[i].outpkt_fifo, &pkt[i],  sizeof(AVPacket),  NULL);
#ifdef FILE_DEBUG
            wrapper->tile_info[i].nSpkt += 1;
            av_log(wrapper->avctx, AV_LOG_DEBUG, "######tile id=%d, getpkt=%d, stitched packet=%d#########\n", i, wrapper->tile_info[i].nGetpkt, wrapper->tile_info[i].nSpkt);
            av_log(wrapper->avctx, AV_LOG_DEBUG, "**********tile id = %d, packet size = %d, packet addr=%p!!!\n", i,pkt[i].size, pkt[i].data);
#endif
            totalsize += pkt[i].size;
        }

        // Sometimes the size of output is larger than size of input,
        // so we alloc 2 times larger size packet.
        ret = ff_alloc_packet2(wrapper->avctx, outPkt, 2*totalsize, 2*totalsize);
        if( ret < 0) return -1;

        dst = outPkt->data;

        // call stitching library
        wrapper->paramTiledStream.pOutputTiledBitstream = dst;

        for (int i = 0; i < wrapper->paramTiledStream.tilesHeightCount; i++)
        {
            for (int j = 0; j < wrapper->paramTiledStream.tilesWidthCount; j++)
            {
                param_oneStream_info *ptempinput = wrapper->paramTiledStream.pTiledBitstream[i*wrapper->paramTiledStream.tilesWidthCount + j];
                ptempinput->pTiledBitstreamBuffer = pkt[i*wrapper->paramTiledStream.tilesWidthCount + j].data;
                ptempinput->inputBufferLen = pkt[i*wrapper->paramTiledStream.tilesWidthCount + j].size;
            }
        }

        wrapper->paramTiledStream.inputBistreamsLen = totalsize;
        genTiledStream_process(&(wrapper->paramTiledStream), wrapper->pGen);
        dst += wrapper->paramTiledStream.outputiledbistreamlen;
        outPkt->size = wrapper->paramTiledStream.outputiledbistreamlen;
/*
#ifdef FILE_DEBUG
        for(int i=0; i<wrapper->tile_num; i++){
            memcpy(dst, pkt[i].data, pkt[i].size);
            dst += pkt[i].size;
            fwrite(pkt[i].data, 1, pkt[i].size, wrapper->tile_info[i].file);
        }
#endif
*/
        // Send vps+sps+pps info
        AVCodecContext* avctx = wrapper->avctx;
        if(avctx->extradata_size == 0)
        {
            unsigned char *headerAddr;
            avctx->extradata_size = genTiledStream_getParam(wrapper->pGen, ID_GEN_TILED_BITSTREAMS_HEADER, &headerAddr);
            avctx->extradata = av_malloc(avctx->extradata_size + AV_INPUT_BUFFER_PADDING_SIZE);
            if (!avctx->extradata) {
                av_log(avctx, AV_LOG_ERROR,
                       "Cannot allocate HEVC header of size %d.\n", avctx->extradata_size);
                return AVERROR(ENOMEM);
            }
            memcpy(avctx->extradata, headerAddr, avctx->extradata_size);
        }

        switch (wrapper->paramTiledStream.sliceType) {
        case SLICE_IDR:
        case SLICE_I:
            avctx->coded_frame->pict_type = AV_PICTURE_TYPE_I;
            break;
        case SLICE_P:
            avctx->coded_frame->pict_type = AV_PICTURE_TYPE_P;
            break;
        case SLICE_B:
            avctx->coded_frame->pict_type = AV_PICTURE_TYPE_B;
            break;
        }

        //outPkt->pts = paramTiledStream.pts;

        ///unref the packet read from fifo
        for(int i=0; i<wrapper->tile_num; i++){
            av_packet_unref(&pkt[i]);
            free(pkt[i].data);
        }

        return 0;
    }
    return -1;
}

int get_tile_frame_copy(EncoderWrapper* wrapper, int tile_idx, const AVFrame *pic, AVFrame** tile_pic )
{
    int ret = 0;
    uint8_t* src = NULL;
    uint8_t* dst = NULL;
    int factor = 1;
    AVFrame* frame = NULL;

    if( NULL == *tile_pic ){
        *tile_pic = av_frame_alloc();
        if (!*tile_pic) {
            av_freep(*tile_pic);
            return AVERROR(ENOMEM);
        }
    }

    frame = *tile_pic;
    frame->height = wrapper->tile_info[tile_idx].tHeight;
    frame->width  = wrapper->tile_info[tile_idx].tWidth;

    frame->format = pic->format;

    if (!frame->data[0]) {
        ret = av_frame_get_buffer(frame, 32);
        if (ret < 0){
            av_freep(*tile_pic);
            return ret;
        }
    }

    ///current copy is based on YUV420p format
    for( int planner=0; planner<3; planner++ ){
        if( planner > 0 ){
            factor = 2;
        }
        src = pic->data[planner] + pic->linesize[planner]*(wrapper->tile_info[tile_idx].top / factor) + wrapper->tile_info[tile_idx].left / factor;
        dst = frame->data[planner];
        for( int i=0; i<frame->height/factor; i++ ){
            src += pic->linesize[planner];
            dst += frame->linesize[planner];
            memcpy( dst, src, frame->width / factor );
        }
    }

    return ret;
}

int get_tile_frame_nocopy(EncoderWrapper* wrapper, int tile_idx, const AVFrame *pic, AVFrame** tile_pic )
{
    AVFrame* frame = NULL;
    int factor = 1;

    if( NULL == *tile_pic ){
        *tile_pic = av_frame_alloc();
        if (!*tile_pic) {
            av_freep(*tile_pic);
            return AVERROR(ENOMEM);
        }
    }

    frame = *tile_pic;
    frame->height = wrapper->tile_info[tile_idx].tHeight;
    frame->width = wrapper->tile_info[tile_idx].tWidth;
    frame->format = pic->format;

    for( int i=0; i<4; i++ ){
        if( i > 0 ){
            factor = 2;
        }
        frame->data[i] = pic->data[i] + pic->linesize[i]*(wrapper->tile_info[tile_idx].top / factor) + wrapper->tile_info[tile_idx].left / factor;
        frame->linesize[i] = pic->linesize[i];
    }

    return 0;
}

static av_cold int tile_encode_close(AVCodecContext *avctx)
{
    TileEncoderContext *ctx = avctx->priv_data;
    EncoderWrapper *wrapper = &(ctx->api);
    AVFifoBuffer* fifo = NULL;

    if(wrapper->pGen)
    {
        genTiledStream_unInit(wrapper->pGen);
    }

    if (wrapper->paramTiledStream.pTiledBitstream)
    {
        for (int i = 0; i < wrapper->paramTiledStream.tilesHeightCount; i++)
        {
            for (int j = 0; j < wrapper->paramTiledStream.tilesWidthCount; j++)
            {
                free(wrapper->paramTiledStream.pTiledBitstream[i*wrapper->paramTiledStream.tilesWidthCount + j]);
                wrapper->paramTiledStream.pTiledBitstream[i*wrapper->paramTiledStream.tilesWidthCount + j] = NULL;
            }
        }
        free(wrapper->paramTiledStream.pTiledBitstream);
        wrapper->paramTiledStream.pTiledBitstream = NULL;
    }
    if(avctx->extradata)
    {
        free(avctx->extradata);
        avctx->extradata = NULL;
    }

    if(wrapper->tid)
    {
        free(wrapper->tid);
        wrapper->tid = NULL;
    }
    if(wrapper->tile_enc_info)
    {
        free(wrapper->tile_enc_info);
        wrapper->tile_enc_info = NULL;
    }

    if( NULL != ctx->api.enc_close )
        ctx->api.enc_close(&(ctx->api));

    for( int i=0; i < ctx->api.tile_num; i++ ){

#ifdef FILE_DEBUG
        if(ctx->api.tile_info[i].file) fclose(ctx->api.tile_info[i].file);
#endif

        fifo = ctx->api.tile_info[i].outpkt_fifo;
        while ( fifo && av_fifo_size(fifo)) {
            AVPacket pkt;
            av_fifo_generic_read(fifo, &pkt,  sizeof(pkt),  NULL);
            free(pkt.data);
            av_packet_unref(&pkt);
        }
        av_fifo_free(fifo);
        fifo = NULL;
    }
    return 0;
}

static av_cold int tile_encode_init(AVCodecContext *avctx)
{
    TileEncoderContext *ctx = avctx->priv_data;
    EncoderWrapper* wrapper = &(ctx->api);
    int ret = 0;
    char filename[256];

    wrapper->width = avctx->coded_width;
    wrapper->height = avctx->coded_height;

    wrapper->avctx = avctx;
    switch(ctx->tile_mode){
        case FIX_SIZE:
            wrapper->uniform_split = false;
            assign_tiles_fix( ctx );
            break;
        case AVG_SIZE:
            wrapper->uniform_split = true;
            assign_tiles_avg( ctx );
            break;
        default:
            break;
    }


    switch(ctx->enc_lib){
        case ENC_X265:
            wrapper->enc_close = libx265_enc_close;
            wrapper->enc_frame = libx265_enc_frame;
            wrapper->enc_init  = libx265_enc_init;
            break;
        case ENC_SVT:
            wrapper->enc_close = svt_enc_close;
            wrapper->enc_frame = svt_enc_frame;
            wrapper->enc_init  = svt_enc_init;
            break;
        default:
            break;
    }

    pthread_mutex_init(&(wrapper->mutex), NULL);
    pthread_cond_init(&(wrapper->cond), NULL);
    wrapper->tid = malloc(wrapper->tile_num * sizeof(pthread_t));
    wrapper->tile_enc_info = malloc(wrapper->tile_num * sizeof(TileEncoderInfo));
    for(int i=0; i<wrapper->tile_num; i++){
        wrapper->tile_info[i].tBitrate = get_tile_bitrate(wrapper, i);
        wrapper->tile_info[i].tMaxrate = get_tile_maxrate(wrapper, i);
        wrapper->tile_info[i].eos = 0;
        wrapper->tile_info[i].outpkt_fifo = av_fifo_alloc( FIFO_SIZE * sizeof(AVPacket));
#ifdef FILE_DEBUG
        wrapper->tile_info[i].nGetpkt = 0;
        wrapper->tile_info[i].nSpkt = 0;
        sprintf(filename, "out_%d.265", i);
        wrapper->tile_info[i].file = fopen(filename, "wb+");
#endif
        wrapper->tile_enc_info[i].ctx      = wrapper;
        wrapper->tile_enc_info[i].tile_idx = i;

        ret = pthread_create(&wrapper->tid[i], NULL, svt_enc_tile, &(wrapper->tile_enc_info[i]));
        if(0 != ret)
        {
            av_log(avctx, AV_LOG_ERROR, "Cannot create thread!\n");
            return ret;
        }
    }

    if( NULL != ctx->api.enc_init ){
        ret = wrapper->enc_init(wrapper);
        if( 0 != ret ) return ret;
    }

    wrapper->paramTiledStream.tilesHeightCount = wrapper->tile_h;
    wrapper->paramTiledStream.tilesWidthCount  = wrapper->tile_w;
    wrapper->paramTiledStream.tilesUniformSpacing = wrapper->uniform_split;
    wrapper->paramTiledStream.frameWidth = wrapper->width;
    wrapper->paramTiledStream.frameHeight = wrapper->height;
    wrapper->paramTiledStream.pTiledBitstream = (param_oneStream_info**)malloc(wrapper->tile_h * wrapper->tile_w * sizeof(param_oneStream_info *));
    if (!wrapper->paramTiledStream.pTiledBitstream)
    {
        printf("memory alloc failed!");
        return 1;
    }

    for (int i = 0; i < wrapper->paramTiledStream.tilesHeightCount; i++)
    {
        for (int j = 0; j < wrapper->paramTiledStream.tilesWidthCount; j++)
        {
            wrapper->paramTiledStream.pTiledBitstream[i*wrapper->paramTiledStream.tilesWidthCount + j] = (param_oneStream_info*)malloc(sizeof(param_oneStream_info));
        }
    }

    wrapper->pGen = genTiledStream_Init(&(wrapper->paramTiledStream));
    if (!wrapper->pGen)
    {
        printf("the initialize failed\n");
        return 1;
    }

    return 0;
}

static int tile_encode_frame(AVCodecContext *avctx, AVPacket *pkt,
                                const AVFrame *pic, int *got_packet)
{
    TileEncoderContext *ctx = avctx->priv_data;
    if( NULL != ctx->api.enc_frame )
        ctx->api.enc_frame(&(ctx->api), pkt, pic, got_packet);

    return 0;
}

#define OFFSET(x) offsetof(TileEncoderContext, x)
#define VE AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_ENCODING_PARAM
static const AVOption options[] = {
    { "enc", "what's the encoder for each tile. so far, x265=1, svt=2.", OFFSET(enc_lib), AV_OPT_TYPE_INT, { .i64 = 2 }, 0, 3, VE },
    { "tile_mode", "specify how to divide the tiles of the picture: 1 fixed size tiles; 2. grid layout, 3x3, 4x4.", OFFSET(tile_mode), AV_OPT_TYPE_INT, { .i64 = 2 }, 0, 3, VE },
    { "tiles_gw", "horizontal grid number of tiles; available when tile is divided via grid layout .", OFFSET(tiles_gw), AV_OPT_TYPE_INT, { .i64 = 1 }, 0, INT_MAX, VE },
    { "tiles_gh", "vertical grid number of tiles; available when tile is divided via grid layout .", OFFSET(tiles_gh), AV_OPT_TYPE_INT, { .i64 = 1 }, 0, INT_MAX, VE },
    { "tiles_fixw", "horizontal width of tiles; available when tile is divided via fixed size.", OFFSET(fix_tiles_w), AV_OPT_TYPE_INT, { .i64 = 512 }, 0, INT_MAX, VE },
    { "tiles_fixh", "vertical height of tiles; available when tile is divided via fixed size.", OFFSET(fix_tiles_h), AV_OPT_TYPE_INT, { .i64 = 512 }, 0, INT_MAX, VE },
    { "params", "Set parameters as a comma-separated list of key=value pairs.", OFFSET(params), AV_OPT_TYPE_STRING, { .str = NULL }, 0, 0, VE },
    { NULL },
};

static const AVClass class = {
    .class_name = "hevc_tile_encoder",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

static const AVCodecDefault defaults[] = {
    { "b", "0" },
    { NULL },
};

AVCodec ff_hevc_tile_encoder = {
    .name             = "hevc_tile_encoder",
    .long_name        = NULL_IF_CONFIG_SMALL("distribute tile H.265 / HEVC"),
    .type             = AVMEDIA_TYPE_VIDEO,
    .id               = AV_CODEC_ID_HEVC,
    .capabilities     = AV_CODEC_CAP_DELAY,
    .pix_fmts         = (const enum AVPixelFormat[]){ AV_PIX_FMT_YUV420P,
                                                    AV_PIX_FMT_YUV420P10,
                                                    AV_PIX_FMT_NONE },

    .priv_class       = &class,
    .priv_data_size   = sizeof(TileEncoderContext),
    .defaults         = defaults,

    .init             = tile_encode_init,
    .encode2          = tile_encode_frame,
    .close            = tile_encode_close,

    .caps_internal    = FF_CODEC_CAP_INIT_THREADSAFE | FF_CODEC_CAP_INIT_CLEANUP,

    .wrapper_name     = "hevc_tile_encoder",
};
