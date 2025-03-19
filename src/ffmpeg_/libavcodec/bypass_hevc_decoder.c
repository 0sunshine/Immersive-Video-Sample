/*
 * Raw Video Decoder
 * Copyright (c) 2001 Fabrice Bellard
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

/**
 * @file
 * Raw Video Decoder
 */

#include "bypass_decoder.h"

static const AVClass bypassdec_class = {
    .class_name = "bypassdec",
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

static av_cold int hevc_bypass_init_decoder(AVCodecContext *avctx)
{
    ByPassVideoContext *context = avctx->priv_data;
    const AVPixFmtDescriptor *desc;

    if (   avctx->codec_tag == MKTAG('r','a','w',' ')
        || avctx->codec_tag == MKTAG('N','O','1','6'))
        avctx->pix_fmt = avpriv_find_pix_fmt(avpriv_pix_fmt_bps_mov,
                                      avctx->bits_per_coded_sample);
    else if (avctx->codec_tag == MKTAG('W', 'R', 'A', 'W'))
        avctx->pix_fmt = avpriv_find_pix_fmt(avpriv_pix_fmt_bps_avi,
                                      avctx->bits_per_coded_sample);
    else if (avctx->codec_tag && (avctx->codec_tag & 0xFFFFFF) != MKTAG('B','I','T', 0))
        avctx->pix_fmt = avpriv_find_pix_fmt(ff_raw_pix_fmt_tags, avctx->codec_tag);
    else if (avctx->pix_fmt == AV_PIX_FMT_NONE && avctx->bits_per_coded_sample)
        avctx->pix_fmt = avpriv_find_pix_fmt(avpriv_pix_fmt_bps_avi,
                                      avctx->bits_per_coded_sample);

    return 0;
}

static int hevc_bypass_decode(AVCodecContext *avctx, void *data, int *got_frame,
                      AVPacket *avpkt)
{
    const AVPixFmtDescriptor *desc;
    ByPassVideoContext *context       = avctx->priv_data;
    const uint8_t *buf             = avpkt->data;
    int buf_size                   = avpkt->size;
    int linesize_align             = 4;
    int stride;
    int res, len;
    int need_copy;

    AVFrame   *frame   = data;

    avctx->width = context->frameWidth;
    avctx->height = context->frameHeight;

    if (avctx->width <= 0) {
        av_log(avctx, AV_LOG_ERROR, "width is not set\n");
        return AVERROR_INVALIDDATA;
    }
    if (avctx->height <= 0) {
        av_log(avctx, AV_LOG_ERROR, "height is not set\n");
        return AVERROR_INVALIDDATA;
    }

    if (context->is_nut_mono)
        stride = avctx->width / 8 + (avctx->width & 7 ? 1 : 0);
    else if (context->is_nut_pal8)
        stride = avctx->width;
    else
        stride = avpkt->size / avctx->height;

    av_log(avctx, AV_LOG_DEBUG, "PACKET SIZE: %d, STRIDE: %d\n", avpkt->size, stride);

    avctx->pix_fmt = AV_PIX_FMT_YUV420P;

    need_copy = !avpkt->buf || context->is_1_2_4_8_bpp || context->is_yuv2 || context->is_lt_16bpp;

    frame->pict_type        = AV_PICTURE_TYPE_I;
    frame->key_frame        = 1;

    res = ff_decode_frame_props(avctx, frame);
    if (res < 0)
        return res;

    frame->pkt_pos      = avctx->internal->last_pkt_props->pos;
    frame->pkt_duration = avctx->internal->last_pkt_props->duration;

    if (context->tff >= 0) {
        frame->interlaced_frame = 1;
        frame->top_field_first  = context->tff;
    }

    if ((res = av_image_check_size(avctx->width, avctx->height, 0, avctx)) < 0)
        return res;

    if (need_copy)
        frame->buf[0] = av_buffer_alloc(FFMAX(context->frame_size, buf_size));
    else
        frame->buf[0] = av_buffer_ref(avpkt->buf);
    if (!frame->buf[0])
        return AVERROR(ENOMEM);

    if (need_copy) {
        memcpy(frame->buf[0]->data, buf, buf_size);
        buf = frame->buf[0]->data;
    }

    frame->data[0] = buf;
    frame->linesize[0] = buf_size;

    *got_frame = 1;
    return buf_size;
}

static av_cold int hevc_bypass_close_decoder(AVCodecContext *avctx)
{
    ByPassVideoContext *context = avctx->priv_data;

    return 0;
}

AVCodec ff_hevc_bypassvideo_decoder = {
    .name           = "hevcbypassdec",
    .long_name      = NULL_IF_CONFIG_SMALL("bypass video decoder"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_HEVCBYPASSVIDEO,
    .priv_data_size = sizeof(ByPassVideoContext),
    .init           = hevc_bypass_init_decoder,
    .close          = hevc_bypass_close_decoder,
    .decode         = hevc_bypass_decode,
    .priv_class     = &bypassdec_class,
    .capabilities   = AV_CODEC_CAP_PARAM_CHANGE,
};
