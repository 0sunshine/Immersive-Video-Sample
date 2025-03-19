/*
 * Copyright (c) 2020 Intel Corporation, all rights reserved.
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
 * libxcam wrapper functions
 */

#include <xcam/capi/xcam_handle.h>
#include "libavutil/avstring.h"
#include "libavutil/opt.h"
#include "framesync.h"
#include "internal.h"

typedef struct XCamVideoFilterBuf {
    XCamVideoBuffer buf;
    AVFrame *frame;
} XCamVideoFilterBuf;

typedef struct XCAMContext {
    const AVClass *class;

    int nb_inputs;
    int w;
    int h;
    char *fmt;
    char *name;
    int allocoutbuf;
    char *params;

    XCamHandle *handle;
    uint32_t v4l2_fmt;

    XCamVideoFilterBuf *inbufs[XCAM_MAX_INPUTS_NUM + 1];
    FFFrameSync fs;
} XCAMContext;

static void xcambuf_ref(XCamVideoBuffer *buf) {
    return;
}

static void xcambuf_unref(XCamVideoBuffer *buf) {
    return;
}

static uint8_t *xcambuf_map(XCamVideoBuffer *buf) {
    XCamVideoFilterBuf *avfilter_buf = (XCamVideoFilterBuf *)(buf);
    return avfilter_buf->frame->data[0];
}

static void xcambuf_unmap(XCamVideoBuffer *buf) {
    return;
}

static int xcambuf_get_fd(XCamVideoBuffer *buf) {
    return 1;
}

static void fill_xcambuf_from_avframe(XCamVideoFilterBuf *buf, AVFrame *frame)
{
    buf->frame = frame;
}

static void fill_avframe_from_xcambuf(AVFrame *frame, XCamVideoBuffer *buf)
{
    XCamVideoBufferPlanarInfo planar;

    uint8_t *start = xcam_video_buffer_map(buf);
    if (!start)
        return;

    for (uint32_t idx = 0; idx < buf->info.components; idx++) {
        uint8_t *src = start + buf->info.offsets[idx];
        uint8_t *dest = frame->data[idx];
        xcam_video_buffer_get_planar_info(&buf->info, &planar, idx);

        for (uint32_t h = 0; h < planar.height; h++) {
            memcpy(dest, src, frame->linesize[idx]);
            src += buf->info.strides[idx];
            dest += frame->linesize[idx];
        }
    }

    xcam_video_buffer_unmap (buf);
}

static uint32_t avfmt_to_v4l2fmt(int avfmt) {
    if (avfmt == AV_PIX_FMT_YUV420P)
        return V4L2_PIX_FMT_YUV420;
    return V4L2_PIX_FMT_NV12;
}

static int set_parameters(AVFilterContext *ctx, const AVFilterLink *inlink, const AVFilterLink *outlink)
{
    XCAMContext *s = inlink->dst->priv;

    char params[XCAM_MAX_PARAMS_LENGTH] = { 0 };
    snprintf(params, XCAM_MAX_PARAMS_LENGTH - 1, "inw=%d inh=%d outw=%d outh=%d fmt=%d allocoutbuf=%d %s",
        inlink->w, inlink->h, outlink->w, outlink->h, s->v4l2_fmt, s->allocoutbuf, s->params);

    if (xcam_handle_set_parameters(s->handle, params) != XCAM_RETURN_NO_ERROR) {
        av_log(ctx, AV_LOG_ERROR, "xcam handler set parameters failed\n");
        return AVERROR(EINVAL);
    }

    return 0;
}

static int
init_xcambuf_info(XCAMContext *s, XCamVideoBuffer *buf, AVFrame *frame)
{
    XCamReturn ret = xcam_video_buffer_info_reset(
        &buf->info, s->v4l2_fmt, frame->width, frame->height, frame->linesize[0], frame->height, 0);
    if (ret != XCAM_RETURN_NO_ERROR)
        return AVERROR(EINVAL);

    for (int i = 0; frame->linesize[i]; i++) {
        buf->info.offsets[i] = frame->data[i] - frame->data[0];
        buf->info.strides[i] = frame->linesize[i];
    }
    buf->mem_type = XCAM_MEM_TYPE_CPU;

    return 0;
}

static int xcam_execute(FFFrameSync *fs)
{
    AVFilterContext *ctx = fs->parent;
    XCAMContext *s = fs->opaque;
    AVFilterLink *outlink;
    AVFrame *outframe, *frame;
    XCamVideoBuffer *outbuf = NULL;

    XCamVideoFilterBuf **inbufs = s->inbufs;
    for (int i = 0; i < ctx->nb_inputs; i++) {
        int error = ff_framesync_get_frame(&s->fs, i, &frame, 0);
        if (error < 0)
            return error;
        if (init_xcambuf_info(s, &inbufs[i]->buf, frame) != 0)
            return AVERROR(EINVAL);
        fill_xcambuf_from_avframe(inbufs[i], frame);
    }

    if (xcam_handle_execute(s->handle, (XCamVideoBuffer **)inbufs, &outbuf) != XCAM_RETURN_NO_ERROR) {
        av_log(ctx, AV_LOG_ERROR, "execute xcam handler failed\n");
        return AVERROR(EINVAL);
    }

    outlink = ctx->outputs[0];
    if (!(outframe = ff_get_video_buffer(outlink, outlink->w, outlink->h))) {
        av_frame_free(&frame);
        return AVERROR(ENOMEM);
    }
    av_frame_copy_props(outframe, frame);

    fill_avframe_from_xcambuf(outframe, outbuf);
    xcam_video_buffer_unref(outbuf);

    return ff_filter_frame(outlink, outframe);
}

static int xcam_query_formats(AVFilterContext *ctx)
{
    XCAMContext *s = ctx->priv;
    AVFilterFormats *formats = NULL;

    static const enum AVPixelFormat nv12_fmts[] = {AV_PIX_FMT_NV12, AV_PIX_FMT_NONE};
    static const enum AVPixelFormat yuv420_fmts[] = {AV_PIX_FMT_YUV420P, AV_PIX_FMT_NONE};
    static const enum AVPixelFormat auto_fmts[] = {AV_PIX_FMT_NV12, AV_PIX_FMT_YUV420P, AV_PIX_FMT_NONE};

    const enum AVPixelFormat *pix_fmts = NULL;
    if (!av_strcasecmp(s->fmt, "nv12"))
        pix_fmts = nv12_fmts;
    else if (!av_strcasecmp(s->fmt, "yuv420"))
        pix_fmts = yuv420_fmts;
    else
        pix_fmts = auto_fmts;

    if (!(formats = ff_make_format_list(pix_fmts)))
        return AVERROR(ENOMEM);

    return ff_set_common_formats(ctx, formats);
}

static int xcam_config_output(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    XCAMContext *s = ctx->priv;
    AVFilterLink *inlink = ctx->inputs[0];
    int ret = 0;

    s->v4l2_fmt = avfmt_to_v4l2fmt(inlink->format);
    if (s->w && s->h) {
        outlink->w = s->w;
        outlink->h = s->h;
    } else {
        outlink->w = inlink->w;
        outlink->h = inlink->h;
    }

    set_parameters(ctx, inlink, outlink);
    if (xcam_handle_init(s->handle) != XCAM_RETURN_NO_ERROR) {
        av_log(ctx, AV_LOG_ERROR, "init xcam handler failed\n");
        return AVERROR(EINVAL);
    }

    if ((ret = ff_framesync_init(&s->fs, ctx, ctx->nb_inputs)) < 0)
        return ret;
    s->fs.opaque = s;
    s->fs.on_event = xcam_execute;
    for (int i = 0; i < ctx->nb_inputs; i++) {
        FFFrameSyncIn *in = &s->fs.in[i];
        in->time_base = ctx->inputs[i]->time_base;
        in->sync      = 1;
        in->before    = EXT_STOP;
        in->after     = EXT_STOP;
    }
    ret = ff_framesync_configure(&s->fs);
    outlink->time_base = s->fs.time_base;

    return ret;
}

static av_cold int xcam_init(AVFilterContext *ctx)
{
    XCAMContext *s = ctx->priv;
    int ret = 0;

    s->handle = xcam_create_handle(s->name);
    if (!s->handle) {
        av_log(ctx, AV_LOG_ERROR, "create xcam handler failed\n");
        return AVERROR(EINVAL);
    }

    for (int i = 0; i < s->nb_inputs; i++) {
        s->inbufs[i] = av_mallocz_array(1, sizeof(*s->inbufs[i]));
        if (!s->inbufs[i])
            return AVERROR(ENOMEM);
        s->inbufs[i]->buf.ref = xcambuf_ref;
        s->inbufs[i]->buf.unref = xcambuf_unref;
        s->inbufs[i]->buf.map = xcambuf_map;
        s->inbufs[i]->buf.unmap = xcambuf_unmap;
        s->inbufs[i]->buf.get_fd = xcambuf_get_fd;
    }

    for (int i = 0; i < s->nb_inputs; i++) {
        AVFilterPad pad = { .type = AVMEDIA_TYPE_VIDEO };
        pad.name = av_asprintf("input%d", i);
        if (!pad.name)
            return AVERROR(ENOMEM);

        if ((ret = ff_insert_inpad(ctx, i, &pad)) < 0) {
            av_freep(&pad.name);
            return ret;
        }
    }

    return 0;
}

static av_cold void xcam_uninit(AVFilterContext *ctx)
{
    XCAMContext *s = ctx->priv;

    ff_framesync_uninit(&s->fs);
    for (int i = 0; i < s->nb_inputs; i++) {
        if (s->inbufs[i])
            av_freep(&s->inbufs[i]);
        if (ctx->input_pads)
            av_freep(&ctx->input_pads[i].name);
    }

    xcam_destroy_handle(s->handle);
    s->handle = NULL;
}

static int xcam_activate(AVFilterContext *ctx)
{
    XCAMContext *s = ctx->priv;
    return ff_framesync_activate(&s->fs);
}

#define OFFSET(x) offsetof(XCAMContext, x)
#define FLAGS AV_OPT_FLAG_VIDEO_PARAM|AV_OPT_FLAG_FILTERING_PARAM
#define CONST_STRING(name, help, unit) \
    { name, help, 0, AV_OPT_TYPE_CONST, { .str=name }, 0, 0, FLAGS, unit }

static const AVOption xcam_options[] = {
    { "inputs", "number of inputs", OFFSET(nb_inputs), AV_OPT_TYPE_INT, { .i64 = 1 }, 1, XCAM_MAX_INPUTS_NUM, FLAGS },
    { "w",  "output width", OFFSET(w), AV_OPT_TYPE_INT, { .i64 = 0 }, 0, INT_MAX, FLAGS },
    { "h", "output height", OFFSET(h), AV_OPT_TYPE_INT, { .i64 = 0 }, 0, INT_MAX, FLAGS },
    { "fmt", "pixel format", OFFSET(fmt), AV_OPT_TYPE_STRING, { .str = "auto" }, 0, 0, FLAGS, "fmt" },
        CONST_STRING("auto",   "automatic format negotiation", "fmt"),
        CONST_STRING("nv12",   "NV12 format",                  "fmt"),
        CONST_STRING("yuv420", "YUV420 format",                "fmt"),
    { "name",   "handler name", OFFSET(name), AV_OPT_TYPE_STRING, { .str = "stitch" }, 0, 0, FLAGS, "name" },
        CONST_STRING("3dnr",      "3d denoising",               "name"),
        CONST_STRING("waveletnr", "wavelet denoising",          "name"),
        CONST_STRING("fisheye",   "fisheye calibration",        "name"),
        CONST_STRING("defog",     "fog removal",                "name"),
        CONST_STRING("dvs",       "digital video stabilizer",   "name"),
        CONST_STRING("stitch",    "soft/GLES/Vulkan stitching", "name"),
        CONST_STRING("stitchcl",  "OpenCL stitching",           "name"),
    { "allocoutbuf",  "alloc output buffer", OFFSET(allocoutbuf), AV_OPT_TYPE_BOOL, { .i64 = 1 }, 0, 1, FLAGS },
    { "params", "private parameters for each handle, usage: params=help=1 field0=value0 field1=value1 ...",
        OFFSET(params), AV_OPT_TYPE_STRING, { .str = NULL }, 0, 0, FLAGS },
    { NULL }
};

AVFILTER_DEFINE_CLASS(xcam);

static const AVFilterPad xcam_outputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .config_props = xcam_config_output
    },
    { NULL }
};

AVFilter ff_vf_xcam = {
    .name          = "xcam",
    .description   = NULL_IF_CONFIG_SMALL("Apply image processing using libxcam"),
    .priv_size     = sizeof(XCAMContext),
    .priv_class    = &xcam_class,
    .init          = xcam_init,
    .query_formats = xcam_query_formats,
    .outputs       = xcam_outputs,
    .activate      = xcam_activate,
    .uninit        = xcam_uninit,
    .flags         = AVFILTER_FLAG_DYNAMIC_INPUTS
};

