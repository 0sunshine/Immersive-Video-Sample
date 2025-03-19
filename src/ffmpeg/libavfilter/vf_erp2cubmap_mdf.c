#include <string.h>

#include "libavutil/avassert.h"
#include "libavutil/mem.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"

#include "avfilter.h"
#include "formats.h"
#include "internal.h"
#include "scale_eval.h"
#include "video.h"
#include "vaapi_vpp.h"
#include "wrapper.h"

#define output_w 5760
#define output_h 3840
static const AVRational tb={1,90000};

typedef struct ERP2CubmapVAAPIContext {
    VAAPIVPPContext vpp_ctx;

    char *output_format_string;

    int in_width;
    int in_height;

    char *w_expr;
    char *h_expr;

    struct filter_ctx *filter;
} ERP2CubmapVAAPIContext;

static int query_formats(AVFilterContext *ctx)
{
    static const enum AVPixelFormat in_pix_fmts[]={
        AV_PIX_FMT_NV12,
        AV_PIX_FMT_VAAPI,
        AV_PIX_FMT_NONE
    };
    static const enum AVPixelFormat out_pix_fmts[]={
        AV_PIX_FMT_NV12,
        AV_PIX_FMT_VAAPI,
        AV_PIX_FMT_NONE
    };
    AVFilterFormats *in_fmts = ff_make_format_list(in_pix_fmts);
    AVFilterFormats *out_fmts = ff_make_format_list(out_pix_fmts);

    ff_formats_ref(in_fmts, &ctx->inputs[0]->out_formats);
    ff_formats_ref(out_fmts, &ctx->outputs[0]->in_formats);

    return 0;
}

static int erp2cubmpa_mdf_config_input(AVFilterLink* inlink) {
    AVFilterContext *avctx       = inlink->dst;
    VAAPIVPPContext *vpp_ctx     = avctx->priv;
    ERP2CubmapVAAPIContext *ctx  = avctx->priv;

    ctx->in_width = inlink->w;
    ctx->in_height = inlink->h;

    av_log(avctx, AV_LOG_INFO, "config input: %ux%u .\n", ctx->in_width, ctx->in_height);

    return ff_vaapi_vpp_config_input(inlink);
}

static int erp2cubmap_mdf_config_output(AVFilterLink *outlink) {
    int ret;
    //ret = ff_vaapi_vpp_config_output(outlink);
    //if (ret < 0)
        //return ret;

    AVFilterLink *inlink         = outlink->src->inputs[0];
    AVFilterContext *avctx       = outlink->src;
    VAAPIVPPContext *vpp_ctx     = avctx->priv;
    vpp_ctx->output_width = output_w;
    vpp_ctx->output_height = output_h;
    ret = ff_vaapi_vpp_config_output(outlink);
    if (ret < 0)
       return ret;

    AVHWFramesContext *frames_ctx = vpp_ctx->input_frames;
    ERP2CubmapVAAPIContext *erp2cub_ctx  = avctx->priv;

    trans_config_t filter_Config;

    //vpp_ctx->output_width  = 5760;//atoi((const char *)erp2cub_ctx->w_expr);
    //vpp_ctx->output_height = 3840;//atoi((const char *)erp2cub_ctx->h_expr);
    if (vpp_ctx->output_width <= 0) {
        av_log(avctx, AV_LOG_INFO, "Auto set output width to %u\n", inlink->w);
        vpp_ctx->output_width = inlink->w;
    }

    if (vpp_ctx->output_height  <= 0) {
        av_log(avctx, AV_LOG_INFO, "Auto set output height to %u\n", inlink->h);
        vpp_ctx->output_height = inlink->h;
    }

    if (vpp_ctx->va_context == VA_INVALID_ID) {
        return AVERROR(EINVAL);
    } else {
        av_log(avctx, AV_LOG_INFO, "config output: va_context is %x, display is %x\n", vpp_ctx->va_context, vpp_ctx->hwctx->display);
    }

    outlink->w = output_w;
    outlink->h = output_h;

    av_log(avctx, AV_LOG_INFO, "config output: vpp_ctx %ux%u .\n", vpp_ctx->output_width, vpp_ctx->output_height);
    av_log(avctx, AV_LOG_INFO, "config output: outlink %ux%u .\n", outlink->w, outlink->h);
    av_log(avctx, AV_LOG_INFO, "config output: outlink %u, name is %s .\n", outlink->format, av_get_pix_fmt_name(outlink->format));

    create(&(erp2cub_ctx->filter),(void*)((uint64_t)(vpp_ctx->hwctx->display)));

    frames_ctx->width = outlink->w;
    frames_ctx->height = outlink->h;
    frames_ctx->sw_format = AV_PIX_FMT_NV12;
    frames_ctx->format = AV_PIX_FMT_VAAPI;
    frames_ctx->initial_pool_size = 0;

    av_log(avctx, AV_LOG_INFO, "config output: frame_ctx %ux%u .\n", frames_ctx->width, frames_ctx->height);
    av_log(avctx, AV_LOG_INFO, "config output: frame_ctx sw_format is %d, format is %d, pool size is %d .\n", frames_ctx->sw_format, frames_ctx->format, frames_ctx->initial_pool_size);
    av_log(avctx, AV_LOG_INFO, "config output: frame_ctx sw_format %s, format is %s .\n", av_get_pix_fmt_name(frames_ctx->sw_format), av_get_pix_fmt_name(frames_ctx->format));

    filter_Config.srcWidth        = erp2cub_ctx->in_width;//3840;
    filter_Config.srcHeight       = erp2cub_ctx->in_height;//2048;
    filter_Config.dstWidth        = output_w;//5760;//1664;//1664;
    filter_Config.dstHeight       = output_h;//3840;//1152;//1152;
    filter_Config.xCoordinateFile = "./xCooridnate.bin";
    filter_Config.yCoordinateFile = "./yCooridnate.bin";
    filter_Config.blockWidth      = 16;
    filter_Config.blockHeight     = 4;
    loadFilter(erp2cub_ctx->filter,"./Dewarp_genx.isa","dewarp");
    setConfig(erp2cub_ctx->filter,filter_Config);


    return 0;
}

static int erp2cubmap_mdf_filter_frame(AVFilterLink *inlink, AVFrame *input_frame) {
    AVFilterContext *avctx              = inlink->dst;
    AVFilterLink *outlink               = avctx->outputs[0];
    VAAPIVPPContext *vpp_ctx            = avctx->priv;
    ERP2CubmapVAAPIContext *erp2cub_ctx = avctx->priv;
    AVFrame *output_frame               = NULL;
    VASurfaceID input_surface, output_surface;
    int err;

    if (vpp_ctx->va_context == VA_INVALID_ID) {
        return AVERROR(EINVAL);
    } else {
        av_log(avctx, AV_LOG_INFO, "filter frame: va_context is %x.\n", vpp_ctx->va_context);
    }

    input_surface = (VASurfaceID)(uintptr_t)input_frame->data[3];
    av_log(avctx, AV_LOG_INFO, "Using surface %#x for input.\n",
           input_surface);

    output_frame = ff_get_video_buffer(outlink, vpp_ctx->output_width,
            vpp_ctx->output_height);
    //av_log(avctx, AV_LOG_INFO, "vpp_ctx->output_width %u, vpp_ctx->output_height %u \n", vpp_ctx->output_width, vpp_ctx->output_height);
    //av_log(avctx, AV_LOG_INFO, "output_frame has width %u and height %u \n", output_frame->width, output_frame->height);
    if (!output_frame) {
        err = AVERROR(ENOMEM);
        goto fail;
    }

    output_surface = (VASurfaceID)(uintptr_t)output_frame->data[3];
    av_log(avctx, AV_LOG_INFO, "Using surface %#x for output.\n",
            output_surface);

    updateInputOutput(erp2cub_ctx->filter, input_surface, output_surface);
    execFilter(erp2cub_ctx->filter);

    err = av_frame_copy_props(output_frame, input_frame);
    if (err < 0)
        goto fail;

    av_frame_free(&input_frame);
    av_log(avctx, AV_LOG_INFO, "Filter output: %s, %ux%u (%"PRId64").\n",
            av_get_pix_fmt_name(output_frame->format),
            output_frame->width, output_frame->height, output_frame->pts);

    return ff_filter_frame(outlink, output_frame);

fail:
    av_frame_free(&input_frame);
    av_frame_free(&output_frame);

    return err;
}

static av_cold int erp2cubmap_mdf_init(AVFilterContext *avctx) {
    VAAPIVPPContext *vpp_ctx    = avctx->priv;
    ERP2CubmapVAAPIContext *ctx = avctx->priv;

    ff_vaapi_vpp_ctx_init(avctx);
    vpp_ctx->pipeline_uninit = ff_vaapi_vpp_pipeline_uninit;

    if (ctx->output_format_string) {
        vpp_ctx->output_format = av_get_pix_fmt(ctx->output_format_string);
        if (vpp_ctx->output_format == AV_PIX_FMT_NONE) {
            av_log(avctx, AV_LOG_ERROR, "Invalid output format.\n");
            return AVERROR(EINVAL);
        }
    } else {
        // Use the input format once that is configured.
        vpp_ctx->output_format = AV_PIX_FMT_NONE;
    }

    ctx->filter = NULL;
    return 0;
}

static void erp2cubmap_mdf_uninit(AVFilterContext *avctx) {
    VAAPIVPPContext *vpp_ctx    = avctx->priv;
    ERP2CubmapVAAPIContext *ctx = avctx->priv;

    destroyInstance(ctx->filter);

    ff_vaapi_vpp_ctx_uninit(avctx);
}

#define OFFSET(x) offsetof(ERP2CubmapVAAPIContext, x)
#define FLAGS (AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM)
static const AVOption erp2cubmap_mdf_options[] = {
    { "w", "Output video width",
        OFFSET(w_expr), AV_OPT_TYPE_STRING, {.str = "iw"}, .flags = FLAGS },
    { "h", "Output video height",
        OFFSET(h_expr), AV_OPT_TYPE_STRING, {.str = "ih"}, .flags = FLAGS },

    {NULL}
};

//AVFILTER_DEFINE_CLASS(erp2cubmap_mdf);
static const AVClass erp2cubmap_mdf_class = {
    .class_name = "erp2cubmap_mdf",
    .item_name  = av_default_item_name,
    .option     = erp2cubmap_mdf_options,
    .version    = LIBAVUTIL_VERSION_INT,
    .category   = AV_CLASS_CATEGORY_FILTER,
};

static const AVFilterPad erp2cubmap_mdf_inputs[]={
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .filter_frame = &erp2cubmap_mdf_filter_frame,
        .config_props = &erp2cubmpa_mdf_config_input,
    },
    { NULL }
};

static const AVFilterPad erp2cubmap_mdf_outputs[]={
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .config_props = &erp2cubmap_mdf_config_output,
    },
    { NULL }
};

AVFilter ff_vf_erp2cubmap_mdf = {
    .name          = "erp2cubmap_mdf",
    .description   = "filter: 360 projection remap from ERP to cubemap with Intel GPU acceleration",
    .priv_size     = sizeof(ERP2CubmapVAAPIContext),
    .init          = &erp2cubmap_mdf_init,
    .uninit        = &erp2cubmap_mdf_uninit,
    .query_formats = &ff_vaapi_vpp_query_formats,
    .inputs        = &erp2cubmap_mdf_inputs,
    .outputs       = &erp2cubmap_mdf_outputs,
    .priv_class    = &erp2cubmap_mdf_class,
    .flags_internal = FF_FILTER_FLAG_HWFRAME_AWARE,
};

