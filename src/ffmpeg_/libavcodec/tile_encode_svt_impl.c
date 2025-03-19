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
#include <sys/time.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>

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
#include "libavutil/common.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"

#include "tile_encoder.h"
#include "EbErrorCodes.h"
#include "EbTime.h"
#include "EbApi.h"

#include <float.h>

typedef struct SvtEncoder {
    EB_H265_ENC_CONFIGURATION           enc_params;
    EB_COMPONENTTYPE                    *svt_handle;
    EB_BUFFERHEADERTYPE                 *in_buf;
    EB_BUFFERHEADERTYPE                 *out_buf;
    int                                  raw_size;
} SvtEncoder;

typedef struct SvtParams {
    int vui_info;
    int hierarchical_level;
    int intra_period;
    int la_depth;
    int intra_ref_type;
    int enc_mode;
    int rc_mode;
    int scd;
    int tune;
    int qp;
    int profile;
    int base_layer_switch_mode;
    int bit_rate;
    int intra_refresh_type;
}SvtParams;

typedef struct SvtContext {
    SvtEncoder  *svt_enc;
    SvtParams   svt_param;
    int         eos_flag;
    int         i;
} SvtContext;

static int error_mapping(int val)
{
    if (val == EB_ErrorInsufficientResources)
        return AVERROR(ENOMEM);
    if ((val == EB_ErrorUndefined) || (val == EB_ErrorInvalidComponent) ||
        (val == EB_ErrorBadParameter))
        return AVERROR(EINVAL);
    return AVERROR_EXTERNAL;
}

static void free_buffer(SvtEncoder *svt_enc)
{
    if (svt_enc->in_buf) {
        EB_H265_ENC_INPUT *in_data = (EB_H265_ENC_INPUT* )svt_enc->in_buf->pBuffer;
        if (in_data) {
            av_freep(&in_data);
        }
        av_freep(&svt_enc->in_buf);
    }
    av_freep(&svt_enc->out_buf);
}

static EB_ERRORTYPE alloc_buffer(EB_H265_ENC_CONFIGURATION *config, SvtEncoder *svt_enc)
{
    EB_ERRORTYPE       ret       = EB_ErrorNone;

    const int    pack_mode_10bit   = (config->encoderBitDepth > 8) && (config->compressedTenBitFormat == 0) ? 1 : 0;
    const size_t luma_size_8bit    = config->sourceWidth * config->sourceHeight * (1 << pack_mode_10bit);
    const size_t luma_size_10bit   = (config->encoderBitDepth > 8 && pack_mode_10bit == 0) ? luma_size_8bit : 0;

    svt_enc->raw_size = (luma_size_8bit + luma_size_10bit) * 3 / 2;

    // allocate buffer for in and out
    svt_enc->in_buf           = av_mallocz(sizeof(EB_BUFFERHEADERTYPE));
    svt_enc->out_buf          = av_mallocz(sizeof(EB_BUFFERHEADERTYPE));
    if (!svt_enc->in_buf || !svt_enc->out_buf)
        goto failed;

    svt_enc->in_buf->pBuffer  = av_mallocz(sizeof(EB_H265_ENC_INPUT));
    if (!svt_enc->in_buf->pBuffer)
        goto failed;

    svt_enc->in_buf->nSize        = sizeof(EB_BUFFERHEADERTYPE);
    svt_enc->in_buf->pAppPrivate  = NULL;
    svt_enc->out_buf->nSize       = sizeof(EB_BUFFERHEADERTYPE);
    svt_enc->out_buf->nAllocLen   = svt_enc->raw_size;
    svt_enc->out_buf->pAppPrivate = NULL;

    return ret;

failed:
    free_buffer(svt_enc);
    return AVERROR(ENOMEM);


}

static EB_ERRORTYPE config_enc_params(EncoderWrapper* wrapper, int tile_idx, EB_H265_ENC_CONFIGURATION  *param )
{
    AVCodecContext *avctx = wrapper->avctx;
    SvtContext *q       = (SvtContext *)wrapper->tile_info[tile_idx].enc_ctx;
    SvtEncoder *svt_enc = q->svt_enc;
    EB_ERRORTYPE    ret = EB_ErrorNone;
    int         tenBits = 0;

    param->sourceWidth     = wrapper->tile_info[tile_idx].tWidth;
    param->sourceHeight    = wrapper->tile_info[tile_idx].tHeight;

    if (avctx->pix_fmt == AV_PIX_FMT_YUV420P10LE) {
        av_log(avctx, AV_LOG_DEBUG , "Encoder 10 bits depth input\n");
        param->compressedTenBitFormat = 0;
        tenBits = 1;
    }

    // Update param from options
    param->hierarchicalLevels     = q->svt_param.hierarchical_level;
    param->encMode                = q->svt_param.enc_mode;
    param->intraRefreshType       = q->svt_param.intra_ref_type;
    param->profile                = q->svt_param.profile;
    param->rateControlMode        = q->svt_param.rc_mode;
    param->sceneChangeDetection   = q->svt_param.scd;
    param->tune                   = q->svt_param.tune;
    param->baseLayerSwitchMode    = q->svt_param.base_layer_switch_mode;

    param->targetBitRate          = q->svt_param.bit_rate;
    param->frameRateNumerator     = avctx->time_base.den;
    param->frameRateDenominator   = avctx->time_base.num * avctx->ticks_per_frame;
    // Need to disable deblock filter to disable loop_filter_across_slices_enable_flag
    param->disableDlfFlag         = 1;
    param->enableSaoFlag          = 0;
    // Make encoded bitstream has I/P frame only
    param->intraPeriodLength      = q->svt_param.intra_period;
    param->qp                     = q->svt_param.qp;
    param->intraRefreshType       = q->svt_param.intra_refresh_type;

    if (q->svt_param.vui_info)
        param->videoUsabilityInfo = q->svt_param.vui_info;
    if (q->svt_param.la_depth != -1)
        param->lookAheadDistance  = q->svt_param.la_depth;

    if (tenBits == 1) {
        param->encoderBitDepth        = 10;
        param->profile                = 2;
    }

    ret = alloc_buffer(param, svt_enc);

    return ret;
}

static int eb_enc_init(EncoderWrapper* wrapper, int tile_idx)
{
    SvtContext* ctx = wrapper->tile_info[tile_idx].enc_ctx;

    EB_ERRORTYPE ret = EB_ErrorNone;
    SvtEncoder* svt_enc = NULL;

    ctx->svt_enc  = av_mallocz(sizeof(*ctx->svt_enc));
    if (!ctx->svt_enc)
        return AVERROR(ENOMEM);

    svt_enc = ctx->svt_enc;

    ctx->eos_flag = 0;

    ret = EbInitHandle(&svt_enc->svt_handle, ctx, &svt_enc->enc_params);
    if (ret != EB_ErrorNone)
        goto failed_init;

    ret = config_enc_params( wrapper, tile_idx, &svt_enc->enc_params);
    if (ret != EB_ErrorNone)
        goto failed_init;

    ret = EbH265EncSetParameter(svt_enc->svt_handle, &svt_enc->enc_params);
    if (ret != EB_ErrorNone)
        goto failed_init;

    ret = EbInitEncoder(svt_enc->svt_handle);
    if (ret != EB_ErrorNone)
        goto failed_init;

    return ret;

failed_init:
    return error_mapping(ret);
}

static void read_in_data(EB_H265_ENC_CONFIGURATION *config, const AVFrame* frame, EB_BUFFERHEADERTYPE *headerPtr)
{
    unsigned int is16bit = config->encoderBitDepth > 8;
    unsigned long long lumaReadSize = (unsigned long long)config->sourceWidth * config->sourceHeight<< is16bit;
    EB_H265_ENC_INPUT *in_data = (EB_H265_ENC_INPUT*)headerPtr->pBuffer;


    // support yuv420p and yuv420p010
    in_data->luma = frame->data[0];
    in_data->cb   = frame->data[1];
    in_data->cr   = frame->data[2];

	// stride info
    in_data->yStride  = frame->linesize[0] >> is16bit;
    in_data->cbStride = frame->linesize[1] >> is16bit;
    in_data->crStride = frame->linesize[2] >> is16bit;

    headerPtr->nFilledLen   += lumaReadSize * 3/2u;

}

static int eb_send_frame(EncoderWrapper* wrapper, int tile_idx, const AVFrame *frame)
{
    SvtContext *q       = (SvtContext *)wrapper->tile_info[tile_idx].enc_ctx;
    SvtEncoder           *svt_enc = q->svt_enc;
    EB_BUFFERHEADERTYPE  *headerPtr = svt_enc->in_buf;

    AVFrame* tile_pic = NULL;
    int                  ret = 0;

    if (!frame) {
        EB_BUFFERHEADERTYPE headerPtrLast;
        headerPtrLast.nAllocLen = 0;
        headerPtrLast.nFilledLen = 0;
        headerPtrLast.nTickCount = 0;
        headerPtrLast.pAppPrivate = NULL;
        //headerPtrLast.nOffset = 0;
        //headerPtrLast.nTimeStamp = 0;
        headerPtrLast.nFlags = EB_BUFFERFLAG_EOS;
        headerPtrLast.pBuffer = NULL;
        EbH265EncSendPicture(svt_enc->svt_handle, &headerPtrLast);
        av_log(wrapper->avctx, AV_LOG_DEBUG, "========tile id = %d NULL frame!!!\n", tile_idx);
        q->eos_flag = 1;
        av_log(wrapper->avctx, AV_LOG_ERROR, "Finish sending frames!!!\n");
        return ret;
    }
    get_tile_frame_nocopy(wrapper, tile_idx, frame, &tile_pic);
    av_log(wrapper->avctx, AV_LOG_DEBUG, "------tile id = %d start frame address: y=%p, u=%p, v=%p!!!\n",
                                          tile_idx, tile_pic->data[0], tile_pic->data[1], tile_pic->data[2]);

    read_in_data(&svt_enc->enc_params, tile_pic, headerPtr);

    //headerPtr->nOffset    = 0;
    headerPtr->nFlags     = 0;
    headerPtr->pAppPrivate = NULL;
    headerPtr->pts        = frame->pts;
    //headerPtr->nFlags     = 0;
    //headerPtr->nTimeStamp = 0;
    //headerPtr->pAppPrivate = NULL;
    headerPtr->sliceType  = INVALID_SLICE;
    q->i += 1;
    av_log(wrapper->avctx, AV_LOG_DEBUG, "tile id = %d start to send frame, times = %d!!!\n", tile_idx, q->i);

    EbH265EncSendPicture(svt_enc->svt_handle, headerPtr);

    if(NULL!= tile_pic) av_frame_free(&tile_pic);
    return ret;
}

static int eb_receive_packet(EncoderWrapper* wrapper, int tile_idx, AVPacket *pkt)
{
    SvtContext *q        = (SvtContext *)wrapper->tile_info[tile_idx].enc_ctx;
    SvtEncoder  *svt_enc = q->svt_enc;
    EB_BUFFERHEADERTYPE   *headerPtr = svt_enc->out_buf;
    EB_ERRORTYPE          stream_status = EB_ErrorNone;

    int ret = 0;

    //if ((ret = ff_alloc_packet2(wrapper->avctx, pkt, svt_enc->raw_size, 0)) < 0){
    //    av_log(wrapper->avctx, AV_LOG_ERROR, "tile id = %d ff_alloc_packet2 ret = %d!!!\n", tile_idx, ret);
    //    return ret;
    //}
    pkt->data = malloc(svt_enc->raw_size);
    pkt->size = svt_enc->raw_size;

    headerPtr->pBuffer = pkt->data;
    stream_status = EbH265GetPacket(svt_enc->svt_handle, headerPtr, q->eos_flag);
    if ((stream_status == EB_NoErrorEmptyQueue)){
        av_log(wrapper->avctx, AV_LOG_DEBUG, "tile id = %d stream_status == EB_NoErrorEmptyQueue!!!\n", tile_idx);
        return AVERROR(EAGAIN);
    }
    pkt->size = headerPtr->nFilledLen;
    pkt->pts = headerPtr->pts;
    pkt->dts = headerPtr->dts;
    ret = (headerPtr->nFlags & EB_BUFFERFLAG_EOS) ? AVERROR_EOF : 0;

    av_log(wrapper->avctx, AV_LOG_DEBUG, "tile id = %d ret = %d!!!\n", tile_idx, ret);
    return ret;
}

static av_cold int eb_enc_close(EncoderWrapper* wrapper, int tile_idx)
{
    SvtContext *q         = (SvtContext *)wrapper->tile_info[tile_idx].enc_ctx;
    SvtEncoder *svt_enc   = q->svt_enc;

    EbDeinitEncoder(svt_enc->svt_handle);
    EbDeinitHandle(svt_enc->svt_handle);

    free_buffer(svt_enc);
    av_freep(&svt_enc);

    return 0;
}

///encode each tile with SVT
int svt_enc_close(void* ctx)
{
    EncoderWrapper* wrapper = (EncoderWrapper*)ctx;
    SvtContext* svt_ctx = NULL;

    for(int i=0; i<wrapper->tile_num; i++){
        svt_ctx = (SvtContext*)wrapper->tile_info[i].enc_ctx;
        if( NULL != svt_ctx){
            eb_enc_close(wrapper, i);
            free(svt_ctx);
        }
        wrapper->tile_info[i].enc_ctx = NULL;
    }

    return 0;
}

int svt_enc_init(void* ctx)
{
    EncoderWrapper* wrapper = (EncoderWrapper*)ctx;
    SvtContext* svt_ctx = NULL;
    int ret = 0;

    for(int i=0; i<wrapper->tile_num; i++){
        svt_ctx = malloc(sizeof(SvtContext));
        svt_ctx->svt_param.hierarchical_level = 3;
        svt_ctx->svt_param.enc_mode = 9;
        svt_ctx->svt_param.intra_ref_type = 1;
        svt_ctx->svt_param.profile = 2;
        svt_ctx->svt_param.rc_mode = 0;//0-CQP, 1-VBR
        svt_ctx->svt_param.qp = 32;
        svt_ctx->svt_param.scd = 0;
        svt_ctx->svt_param.tune = 1;
        svt_ctx->svt_param.intra_period = 5;
        svt_ctx->svt_param.base_layer_switch_mode = 0;
        svt_ctx->svt_param.vui_info = 0;
        svt_ctx->svt_param.la_depth = -1;
        svt_ctx->svt_param.bit_rate = wrapper->tile_info[i].tBitrate;
        svt_ctx->i = 0;
        svt_ctx->svt_param.intra_refresh_type = 1;//1-CRA, 2-IDR intra refresh
        wrapper->tile_info[i].enc_ctx = svt_ctx;
        ret = eb_enc_init(wrapper, i);
        if( 0 != ret ) return ret;
    }
    wrapper->initialized = 1;
    return 0;
}

int svt_enc_frame(void* ctx, AVPacket *pkt, const AVFrame *pic, int *got_packet)
{
    EncoderWrapper* wrapper = (EncoderWrapper*)ctx;
    SvtContext *q = NULL;

    int ret = 0;

    for(int i=0; i<wrapper->tile_num; i++){
        q = (SvtContext *)wrapper->tile_info[i].enc_ctx;
        if( wrapper->tile_info[i].eos ) continue;

        if(!q->eos_flag) eb_send_frame( wrapper, i, pic );
    }

    // Wake up all receive tile threads
    if(!q->eos_flag)
    {
        pthread_cond_broadcast(&(wrapper->cond));
    }
    else
    {
        // Wait until all tiles are ready
        while(0==bFifoReady(wrapper))
        {
            pthread_cond_broadcast(&(wrapper->cond));
            usleep(10000);
        }
    }

    //FIXME, suppose all encoder has the rhythm to get packet, so there is no buffer in the first time

    ret = bs_tile_stitching(wrapper, pkt);

    if( AVERROR_EOF == ret ){
        return AVERROR_EOF;
    }
    *got_packet = 1;

    if( -1 == ret ) *got_packet = 0;

    return 0;
}

int svt_enc_tile(TileEncoderInfo *tile_enc_info)
{
    int ret = 0;

    EncoderWrapper *wrapper = (EncoderWrapper*)tile_enc_info->ctx;
    int            tile_idx = tile_enc_info->tile_idx;

    while(1)
    {
        if(wrapper->initialized)
            break;
    }

    SvtContext          *q         = (SvtContext *)wrapper->tile_info[tile_idx].enc_ctx;
    SvtEncoder          *svt_enc   = q->svt_enc;
    EB_BUFFERHEADERTYPE *headerPtr = svt_enc->out_buf;

    while(!wrapper->tile_info[tile_idx].eos)
    {
        // Wait until next frame is sent
        if(!q->eos_flag)
            pthread_cond_wait(&(wrapper->cond),&(wrapper->mutex));

        AVPacket tile_pkts = {0};
        ret = eb_receive_packet(wrapper, tile_idx, &tile_pkts);
        av_log(wrapper->avctx, AV_LOG_DEBUG, "tile id = %d begin to eb_receive_packet!!!\n", tile_idx);
        if( 0 == ret || AVERROR_EOF == ret ){
            av_log(wrapper->avctx, AV_LOG_DEBUG, "**********tile id = %d eb_receive_packet got packet, packet size = %d, packet addr=%p!!!\n", tile_idx, tile_pkts.size, tile_pkts.data);
            av_fifo_generic_write( wrapper->tile_info[tile_idx].outpkt_fifo, &tile_pkts, sizeof(AVPacket), NULL);
#ifdef FILE_DEBUG
            wrapper->tile_info[tile_idx].nGetpkt += 1;
            //fwrite(tile_pkts.data, 1, tile_pkts.size, wrapper->tile_info[i].file);
#endif
            if( AVERROR_EOF == ret ){
                av_log(wrapper->avctx, AV_LOG_ERROR, "tile id = %d EOS!!!\n", tile_idx);
                wrapper->tile_info[tile_idx].eos = 1;
            }
        }else{
            av_packet_unref(&tile_pkts);
            free(tile_pkts.data);
        }

    }

    // Wait until all tiles are done
    while(AVERROR_EOF!=bFifoReady(wrapper))
    {
        pthread_cond_wait(&(wrapper->cond),&(wrapper->mutex));
        usleep(10000);
    }

    return ret;
}
