/*
 * OpenHEVC video Decoder
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

#include <libopenhevc/openhevc.h>
#include "avcodec.h"
#include "internal.h"
#include "libavutil/imgutils.h"
#include "libavutil/opt.h"
#include "libavutil/common.h"
#include "libavutil/internal.h"

//#define DEBUG
typedef struct OpenHevcContext{
	const AVClass *class;
	OHHandle handle;
#ifdef DEBUG
	FILE *fout;
#endif
	int thread_count;
	int thread_type;
	int temporal_layer_id;
	int quality_layer_id;
}OpenHevcContext;

static av_cold int openhevc_close(AVCodecContext *ctx)
{
	OpenHevcContext *c = ctx->priv_data;
	if(c->handle){
		oh_close(c->handle);
		c->handle = NULL;
	}
#ifdef DEBUG
	if(c->fout){
		fclose(c->fout);
	}
#endif
	return 0;
}
static av_cold int openhevc_init(AVCodecContext *ctx)
{
	OpenHevcContext *c = ctx->priv_data;
	c->handle = oh_init(c->thread_count, c->thread_type);
	if(!c->handle){
		av_log(ctx,AV_LOG_ERROR,"oh_init failed\n");
		return AVERROR_EXTERNAL;
	}
	size_t extra_size_alloc;
	extra_size_alloc = ctx->extradata_size > 0 ? (ctx->extradata_size +AV_INPUT_BUFFER_PADDING_SIZE) : 0;
	if(extra_size_alloc){
		oh_extradata_cpy(c->handle, ctx->extradata, extra_size_alloc);
	}
	oh_disable_cropping(c->handle, !!(ctx->flags2 & AV_CODEC_FLAG2_IGNORE_CROP));
	oh_start(c->handle);
	oh_select_temporal_layer(c->handle,c->temporal_layer_id);
	oh_select_active_layer(c->handle,c->quality_layer_id);
	oh_select_view_layer(c->handle,c->quality_layer_id);
#ifdef DEBUG
	c->fout = fopen("output.yuv","wb");
	if(!c->fout){
		printf("open file failed !\n");
		return -1;
	}
#endif
	return 0;
}

static int openhevc_decode(AVCodecContext *ctx, void *data, int *got_frame, AVPacket *avpkt)
{
	OpenHevcContext *c = ctx->priv_data;
	AVFrame *picture = data;
	int ret;
	OHFrame openHevcFrame;

	ret = oh_decode(c->handle, avpkt->data, avpkt->size, avpkt->pts);

	av_log(ctx, AV_LOG_DEBUG, "oh_decode pts %d size %ld ret %d\n", avpkt->pts, avpkt->size, ret);

	if(ret<0){
		av_log(ctx, AV_LOG_ERROR, "failed to decode frame\n");
		return AVERROR_EXTERNAL;
	}
	if(ret){
		uint8_t *data_ptr_array[4] = {NULL};
		int stride_array[4] = {0};

		oh_output_update(c->handle, 1, &openHevcFrame);
		oh_frameinfo_update(c->handle, &openHevcFrame.frame_par);

		if(av_image_check_size(openHevcFrame.frame_par.width, openHevcFrame.frame_par.height, 0, ctx))
			return AVERROR_INVALIDDATA;
		ctx->pix_fmt = AV_PIX_FMT_YUV420P;
		ff_set_dimensions(ctx, openHevcFrame.frame_par.width, openHevcFrame.frame_par.height);

		av_log(ctx, AV_LOG_DEBUG, "oh_decode pts %d frame 0x%lx y 0x%lx u 0x%lx v 0x%lx\n",
			avpkt->pts, (unsigned long)picture,
			(unsigned long)openHevcFrame.data_y_p,
			(unsigned long)openHevcFrame.data_cb_p,
			(unsigned long)openHevcFrame.data_cr_p);

		if((ret=ff_get_buffer(ctx, picture, 0))<0)
			return ret;
		picture->sample_aspect_ratio.num = openHevcFrame.frame_par.sample_aspect_ratio.num;
		picture->sample_aspect_ratio.den = openHevcFrame.frame_par.sample_aspect_ratio.den;

		data_ptr_array[0] = (uint8_t *)openHevcFrame.data_y_p;
		data_ptr_array[1] = (uint8_t *)openHevcFrame.data_cb_p;
		data_ptr_array[2] = (uint8_t *)openHevcFrame.data_cr_p;

		stride_array[0] = openHevcFrame.frame_par.linesize_y;
		stride_array[1] = openHevcFrame.frame_par.linesize_cb;
		stride_array[2] = openHevcFrame.frame_par.linesize_cr;
#ifdef DEBUG
		if(c->fout){
		    int format = openHevcFrame.frame_par.chromat_format == OH_YUV420 ? 1 : 0;
                    fwrite( (uint8_t *)openHevcFrame.data_y_p ,  sizeof(uint8_t) , openHevcFrame.frame_par.linesize_y  * openHevcFrame.frame_par.height,  c->fout);
                    fwrite( (uint8_t *)openHevcFrame.data_cb_p , sizeof(uint8_t) , openHevcFrame.frame_par.linesize_cb * openHevcFrame.frame_par.height >> format, c->fout);
                    fwrite( (uint8_t *)openHevcFrame.data_cr_p , sizeof(uint8_t) , openHevcFrame.frame_par.linesize_cr * openHevcFrame.frame_par.height >> format, c->fout);
                }
#endif
//		av_image_copy(picture->data, picture->linesize, (uint8_t **)data_ptr_array, stride_array, ctx->pix_fmt, picture->width, picture->height);
		picture->data[0] = data_ptr_array[0];
		picture->data[1] = data_ptr_array[1];
		picture->data[2] = data_ptr_array[2];
		picture->linesize[0] = stride_array[0];
		picture->linesize[1] = stride_array[1];
		picture->linesize[2] = stride_array[2];
		picture->format = ctx->pix_fmt;

		picture->pts = avpkt->pts;
	        picture->pkt_dts = avpkt->dts;
		picture->pkt_duration = avpkt->duration;

		*got_frame = 1;
	}
	return avpkt->size;

}
static void openhevc_flush(AVCodecContext *avctx)
{
	OpenHevcContext *c = avctx->priv_data;
	oh_flush(c->handle);
}
#define OFFSET(x) offsetof(OpenHevcContext, x)
#define VE (AV_OPT_FLAG_DECODING_PARAM | AV_OPT_FLAG_VIDEO_PARAM)
static const AVOption options[] = {
	{"thread_count", "for how many threads to be executed, 1 is for default", OFFSET(thread_count), AV_OPT_TYPE_INT, {.i64 = 1}, 0, INT_MAX, VE},
	{"thread_type", "which multithreads methods to use, 1 is for default", OFFSET(thread_type), AV_OPT_TYPE_INT, {.i64 = 1}, 0, INT_MAX, VE},
	{"temporal_layer_id","temporal layer id,7 is for default",OFFSET(temporal_layer_id),AV_OPT_TYPE_INT,{.i64 = 7}, 0 , INT_MAX, VE},
	{"quality_layer_id","quality layer id,0 is for default",OFFSET(quality_layer_id),AV_OPT_TYPE_INT,{.i64 = 0}, 0 , INT_MAX, VE},
	{NULL},
};
static const AVClass openhevc_class = {
	.class_name = "libopenhevc",
	.item_name = av_default_item_name,
	.option = options,
	.version = LIBAVUTIL_VERSION_INT,
};
AVCodec ff_libopenhevc_decoder = {
	.name = "libopenhevc",
	.long_name = NULL_IF_CONFIG_SMALL("libopenhevc HEVC decoder"),
	.type = AVMEDIA_TYPE_VIDEO,
	.id = AV_CODEC_ID_HEVC,
	.priv_data_size = sizeof(OpenHevcContext),
	.priv_class = &openhevc_class,
	.init = openhevc_init,
	.flush = openhevc_flush,
	.close = openhevc_close,
	.decode = openhevc_decode,
	.capabilities = AV_CODEC_CAP_DELAY | AV_CODEC_CAP_DR1,
	.caps_internal = FF_CODEC_CAP_SETS_PKT_DTS | FF_CODEC_CAP_INIT_THREADSAFE | FF_CODEC_CAP_INIT_CLEANUP,
};
