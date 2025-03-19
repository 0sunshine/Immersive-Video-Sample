#include "libavutil/opt.h"
#include "libavutil/imgutils.h"
#include "libavutil/avassert.h"
#include "avfilter.h"
#include "formats.h"
#include "internal.h"
#include "video.h"
#include "360SRAPI.h"

typedef struct SuperresContext {
    const AVClass *class;
    char *split_string;
    char *resolution;
    char *model_filename;
    void *model;
    int scale_factor;
} SuperresContext;

typedef struct ThreadData {
    AVFrame *in, *out;
} ThreadData;

static void image_copy_plane(uint8_t *dst, int dst_linesize,
                         const uint8_t *src, int src_linesize,
                         int bytewidth, int height) {
    if (!dst || !src)
        return;
    av_assert0(abs(src_linesize) >= bytewidth);
    av_assert0(abs(dst_linesize) >= bytewidth);
    for (;height > 0; height--) {
        memcpy(dst, src, bytewidth);
        dst += dst_linesize;
        src += src_linesize;
    }
}

static int frame_copy_video(AVFrame *dst, const AVFrame *src) {
    int i, planes;

    if (dst->width  > src->width ||
        dst->height > src->height)
        return AVERROR(EINVAL);

    planes = av_pix_fmt_count_planes(dst->format);
    for (i = 0; i < planes; i++)
        if (!dst->data[i] || !src->data[i])
            return AVERROR(EINVAL);

    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(dst->format);
    int planes_nb = 0;
    for (i = 0; i < desc->nb_components; i++)
        planes_nb = FFMAX(planes_nb, desc->comp[i].plane + 1);

    for (i = 0; i < planes_nb; i++) {
        int h = dst->height;
        int bwidth = av_image_get_linesize(dst->format, dst->width, i);
        if (bwidth < 0) {
            av_log(NULL, AV_LOG_ERROR, "av_image_get_linesize failed\n");
            return -1;
        }
        if (i == 1 || i == 2) {
            h = AV_CEIL_RSHIFT(dst->height, desc->log2_chroma_h);
        }
        image_copy_plane(dst->data[i], dst->linesize[i],
                            src->data[i], src->linesize[i],
                            bwidth, h);
    }
    return 0;
}

//for YUV data, frame->data[0] save Y, frame->data[1] save U, frame->data[2] save V
static int frame_superres_video(AVFrame *dst, const AVFrame *src, void *model) {

    int srResult = 0;
    srResult = ProcessOneFrame(model, src, dst);

    return 0;
}

static int do_conversion(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs) {
    ThreadData *td = arg;
    AVFrame *dst = td->out;
    AVFrame *src = td->in;
    SuperresContext *superresCtx = ctx->priv;

    av_log(NULL, AV_LOG_INFO, "output avframe, w h = (%d %d), format %d \n", dst->width, dst->height, dst->format);
    frame_superres_video(dst, src, superresCtx->model);

    return 0;
}

static int filter_frame(AVFilterLink *link, AVFrame *in) {
    av_log(NULL, AV_LOG_WARNING, "### chenxf filter_frame, link %x, frame %x \n", link, in);
    AVFilterContext *avctx = link->dst;
    AVFilterLink *outlink = avctx->outputs[0];
    AVFrame *out;

    //allocate a new buffer, data is null
    out = ff_get_video_buffer(outlink, outlink->w, outlink->h);
    if (!out) {
        av_frame_free(&in);
        return AVERROR(ENOMEM);
    }

    //the new output frame, property is the same as input frame, only width/height is different
    av_frame_copy_props(out, in);
    SuperresContext *superresCtx = avctx->priv;

    ThreadData td;
    td.in = in;
    td.out = out;
    int res;
    if(res = avctx->internal->execute(avctx, do_conversion, &td, NULL, FFMIN(outlink->h, avctx->graph->nb_threads))) {
        return res;
    }

    av_frame_free(&in);

    return ff_filter_frame(outlink, out);
}

static av_cold int config_output(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    SuperresContext *superresCtx = ctx->priv;

    outlink->w = ctx->inputs[0]->w * superresCtx->scale_factor;
    outlink->h = ctx->inputs[0]->h * superresCtx->scale_factor;
    av_log(ctx, AV_LOG_INFO, "configure output, w h = (%d %d), format %d \n", outlink->w, outlink->h, outlink->format);

    return 0;
}

static av_cold int init(AVFilterContext *ctx)
{
    av_log(NULL, AV_LOG_DEBUG, "init \n");
    SuperresContext *superresCtx = ctx->priv;
    if (!superresCtx->model_filename) {
        av_log(ctx, AV_LOG_ERROR, "model file was not specified\n");
        return AVERROR(EIO);
    }
    if (!superresCtx->split_string) {
        av_log(ctx, AV_LOG_ERROR, "split string was not specified\n");
        return AVERROR(EIO);
    }
    if (!superresCtx->resolution) {
        av_log(ctx, AV_LOG_ERROR, "resolution string was not specified\n");
        return AVERROR(EIO);
    }

    SuperresParam srParam;
    memset(&srParam, 0, sizeof(SuperresParam));
    srParam.model_filename = superresCtx->model_filename;
    srParam.split_string = superresCtx->split_string;
    srParam.resolution = superresCtx->resolution;
    superresCtx->model = SuperresModelInit(&srParam);

    return 0;
}

static av_cold void uninit(AVFilterContext *ctx)
{
    av_log(NULL, AV_LOG_DEBUG, "uninit \n");
}

static int query_formats(AVFilterContext *ctx)
{
    static const enum AVPixelFormat pix_fmts[] = {
        AV_PIX_FMT_YUV420P,
        AV_PIX_FMT_NONE
    };
    AVFilterFormats *fmts_list = ff_make_format_list(pix_fmts);
    if (!fmts_list)
        return AVERROR(ENOMEM);
    return ff_set_common_formats(ctx, fmts_list);
}


#define OFFSET(x) offsetof(SuperresContext, x)
#define FLAGS AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_FILTERING_PARAM

static const AVOption superres_options[] = {
    { "model",        "path to torch script file",    OFFSET(model_filename), AV_OPT_TYPE_STRING, {.str=NULL}, 0, 0, FLAGS },
    { "resolution",   "string of input resolution",   OFFSET(resolution),     AV_OPT_TYPE_STRING, {.str=NULL}, 0, 0, FLAGS },
    { "split_string", "string of input split",        OFFSET(split_string),   AV_OPT_TYPE_STRING, {.str=NULL}, 0, 0, FLAGS },
    { "scale_factor", "scale factor for SRCNN model", OFFSET(scale_factor),   AV_OPT_TYPE_INT,    {.i64 = 2},  2, 4, FLAGS },
    { NULL }
};

AVFILTER_DEFINE_CLASS(superres);
// static const AVClass superres_class = {
//     .class_name       = "superres",
//     .item_name        = av_default_item_name,
//     .option           = superres_options,
//     .version          = LIBAVUTIL_VERSION_INT,
//     .category         = AV_CLASS_CATEGORY_FILTER,
// };

static const AVFilterPad superres_inputs[] = {
    {
        .name         = "superres_inputpad",
        .type         = AVMEDIA_TYPE_VIDEO,
        // .config_props = config_props,
        .filter_frame = filter_frame,
    },
    { NULL }
};

static const AVFilterPad superres_outputs[] = {
    {
        .name = "superres_outputpad",
        .type = AVMEDIA_TYPE_VIDEO,
        .config_props = config_output,
    },
    { NULL }
};

AVFilter ff_vf_superres = {
    .name          = "superres",
    .description   = NULL_IF_CONFIG_SMALL("super resolution filter"),
    .priv_size     = sizeof(SuperresContext),
    .priv_class    = &superres_class,
    .init          = init,
    .uninit        = uninit,
    .query_formats = query_formats,
    .inputs        = superres_inputs,
    .outputs       = superres_outputs,
};
