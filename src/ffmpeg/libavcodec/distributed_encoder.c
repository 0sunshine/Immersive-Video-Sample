/*
* Scalable Video Technology for distributed encoder library plugin
*
* Copyright (c) 2019 Intel Corporation
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
* License along with this program; if not, write to the Free Software
* Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
*/
#include <sys/time.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "DistributedEncoderAPI.h"
#include "error_code.h"
#include "common_data.h"

#include "libavutil/common.h"
#include "libavutil/frame.h"
#include "libavutil/opt.h"

#include "internal.h"
#include "avcodec.h"

static bool glog_initialized = false;
static int min_loglevel = 2;
static int32_t orig_frame_width = 0;
static int32_t orig_frame_height = 0;
static int8_t hor_scale_factor = 0;
static int8_t ver_scale_factor = 0;

//#define ENABLE_DE_FRAME_LOCK
#define ALIGN64(X) ((uint32_t)(((uint32_t)(X))+63) & (uint32_t)(~ (uint32_t)63))

static void de_log_callback(LogLevel log_level, const char* file_name, uint64_t line_num, const char* fmt, ...)
{
    va_list vl;
    va_start(vl, fmt);

    switch (log_level)
    {
        case LOG_INFO:
        {
            if(min_loglevel == 0)
            {
                av_vlog(NULL, AV_LOG_INFO, fmt, vl);
            }
            break;
        }
        case LOG_WARNING:
        {
            if(min_loglevel <= 1)
            {
                av_vlog(NULL, AV_LOG_WARNING, fmt, vl);
            }
            break;
        }
        case LOG_ERROR:
        {
            if(min_loglevel <= 2)
            {
                av_vlog(NULL, AV_LOG_ERROR, fmt, vl);
            }
            break;
        }
        case LOG_FATAL:
        {
            if(min_loglevel <= 3)
            {
                av_vlog(NULL, AV_LOG_FATAL, fmt, vl);
            }
            break;
        }
        default:
        {
            av_log(NULL, AV_LOG_ERROR, "Invalid log level !");
            break;
        }
    }
    va_end(vl);
}

#ifdef ENABLE_DE_FRAME_LOCK
typedef struct DeFrameList
{
    AVFrame            *frame;
    struct DeFrameList *next;
}DeFrameList;
#endif

typedef struct DEContext {
    const AVClass            *class;

    DistributedEncoderParam  encode_params;
    DEHandle                 handle;
    const char*              configFile;
    InputStreamType          input_type;
    int                      inputCodec;
    bool                     send_end;
    bool                     eos_flag;

    // User options.
    int                      vui_info;
    int                      hierarchical_level;
    int                      la_depth;
    int                      enc_mode;
    int                      rc_mode;
    int                      scd;
    int                      tune;
    int                      qp;
    int                      hdr;
    int                      asm_type;
    int                      forced_idr;
    int                      gpucopy;
    int                      aud;
    int                      profile;
    int                      tier;
    int                      level;
    int                      gop_pred_structure;
    int                      base_layer_switch_mode;
    int                      tile_row;
    int                      tile_column;
    uint64_t                 frame_number;
    bool                     in_parallel;
    bool                     external_log_flag;
    int                      min_log_level;
    const char*              proj_type;
    const char*              input_yuv_format;
#ifdef ENABLE_DE_FRAME_LOCK
    DeFrameList              *DeFrameListHead;
    DeFrameList              *DeFrameListTail;
#endif
} DEContext;

static int set_enc_params(AVCodecContext *avctx, DistributedEncoderParam *DEparams)
{
    DEContext *deCxt = avctx->priv_data;

    memset(DEparams, 0, sizeof(DistributedEncoderParam));
    EncoderParam params;
    memset(&params, 0, sizeof(EncoderParam));
    params.format = PixelColor_YUV420;
    if (deCxt->input_yuv_format)
    {
        if(0 == strncmp(deCxt->input_yuv_format, "p010", 4))
        {
            params.input_yuv_format = InputYUVFormat_P010;
            params.bit_depth = 10;
        }
        else if (0 == strncmp(deCxt->input_yuv_format, "v210", 4))
        {
            params.input_yuv_format = InputYUVFormat_V210;
            params.bit_depth = 10;
        }
        else
        {
            av_log(avctx, AV_LOG_ERROR,
                "Input YUV format not supported: %s \n", deCxt->input_yuv_format);
            return -1;
        }
    }
    else
    {
        params.input_yuv_format = InputYUVFormat_NONE;
        params.bit_depth = 8;
    }
    params.vui_info = deCxt->vui_info;
    params.hierarchical_level = deCxt->hierarchical_level;
    if(avctx->gop_size > 0) {
        params.intra_period = avctx->gop_size - 1;
    }
    params.la_depth = deCxt->la_depth;
    params.enc_mode = deCxt->enc_mode;
    params.rc_mode = deCxt->rc_mode;
    params.scd = deCxt->scd;
    params.tune = deCxt->tune;
    params.qp = deCxt->qp;
    params.profile = deCxt->profile;
    params.pred_structure = deCxt->gop_pred_structure;
    params.base_layer_switch_mode = deCxt->base_layer_switch_mode;
    params.bit_rate = avctx->bit_rate;
    params.intra_refresh_type = (deCxt->forced_idr > 0) ? 1 : 0;
    params.tier = deCxt->tier;
    params.level = deCxt->level;
    params.aud = deCxt->aud;
    params.asm_type = deCxt->asm_type;
    if(avctx->framerate.num > 0 && avctx->framerate.den > 0) {
        params.framerate_num = avctx->framerate.num;
        params.framerate_den = avctx->framerate.den * avctx->ticks_per_frame;
    }
    else
    {
        params.framerate_num = avctx->time_base.den;
        params.framerate_den = avctx->time_base.num * avctx->ticks_per_frame;
    }

    params.deblocking_enable = 0;
    params.sao_enable = 0;
    // tile related setting
    params.tile_columnCnt = deCxt->tile_column;
    params.tile_rowCnt = deCxt->tile_row;
    params.frame_width = avctx->width;
    params.frame_height = avctx->height;
    params.target_socket = -1;
    params.native_mode = false;
    params.MCTS_enable = (params.tile_columnCnt * params.tile_rowCnt) > 1 ? 1 : 0;

    if(deCxt->in_parallel && params.MCTS_enable)
        params.in_parallel = true;
    else
        params.in_parallel = false;

    params.gpucopy = deCxt->gpucopy;

    memcpy(&(DEparams->encoderParams), &params, sizeof(EncoderParam));

    DEparams->type = ResourceBalanced;

    StreamInfo sInfo;
    memset(&sInfo, 0, sizeof(StreamInfo));
    sInfo.frameWidth = avctx->width;
    sInfo.frameHeight = avctx->height;
    sInfo.tileUniformSpacing = true;
    sInfo.tileColumn = deCxt->tile_column;
    sInfo.tileRow = deCxt->tile_row;

    uint32_t aligned_frame_width = ALIGN64(sInfo.frameWidth);
    uint32_t aligned_frame_height = ALIGN64(sInfo.frameHeight);
    if(sInfo.frameWidth % 64)
    {
        av_log(avctx, AV_LOG_WARNING,
                "Frame width %d can't be divided by 64(LCU_SIZE) \n", sInfo.frameWidth);
        av_log(avctx, AV_LOG_WARNING,
                "Frame width %d will be aligned to %d for tile split\n", sInfo.frameWidth, aligned_frame_width);
    }
    if(sInfo.frameHeight % 64)
    {
        av_log(avctx, AV_LOG_WARNING,
                "Frame height %d can't be divided by 64(LCU_SIZE) \n", sInfo.frameHeight);
        av_log(avctx, AV_LOG_WARNING,
                "Frame height %d will be aligned to %d for tile split\n", sInfo.frameHeight, aligned_frame_height);
    }
    if((aligned_frame_width / 64) % sInfo.tileColumn)
    {
        av_log(avctx, AV_LOG_ERROR,
                "Can not form 64-divisible tile column: (%d / 64) %% %d \n", aligned_frame_width, sInfo.tileColumn);
        return -1;
    }
    if((aligned_frame_height / 64) % sInfo.tileRow)
    {
        av_log(avctx, AV_LOG_ERROR,
                "Can not form 64-divisible tile row: (%d / 64) %% %d \n", aligned_frame_height, sInfo.tileRow);
        return -1;
    }

    sInfo.tileOverlapped = 0;
    sInfo.overlapWidth = 0;
    sInfo.overlapHeight = 0;
    sInfo.streamType = deCxt->input_type;
    memcpy(&(DEparams->streamInfo), &sInfo, sizeof(StreamInfo));

    ProjectionInfo          projInfo;
    memset(&projInfo, 0, sizeof(ProjectionInfo));
    projInfo.enable = true;
    if(0 == strncmp(deCxt->proj_type, "ERP", 3))
    {
        projInfo.type = E_EQUIRECT_PROJECTION;
    }
    else if (0 == strncmp(deCxt->proj_type, "Cube", 4))
    {
        projInfo.type = E_CUBEMAP_PROJECTION;
    }
    else if (0 == strncmp(deCxt->proj_type, "Planar", 6))
    {
        projInfo.enable = false;
    }
    else
    {
        av_log(avctx, AV_LOG_ERROR,
                "Invalid input source projection type %s \n", deCxt->proj_type);
        return -1;
    }
    memcpy(&(DEparams->suppleEnhanceInfo.projInfo), &projInfo, sizeof(ProjectionInfo));

    CodecAppOption codecOption;
    memset(&codecOption, 0, sizeof(CodecAppOption));

    if(deCxt->input_type == encoded)
    {
        if(deCxt->inputCodec == 0) //HEVC
        {
            codecOption.decOption.decType = DecoderType_openHEVC;
            ohOption *oh = (ohOption*)malloc(sizeof(ohOption));
            if(!oh)
                return AVERROR(EINVAL);
            oh->threadCount = 16;
            oh->threadType = 4;
            codecOption.decOption.decSetting = (void*)oh;
        }
        else if(deCxt->inputCodec == 1) //AVC
        {
            codecOption.decOption.decType = DecoderType_ffmpeg;
            ffmpegOption * fo = (ffmpegOption*)malloc(sizeof(ffmpegOption));
            if(!fo)
            {
                return AVERROR(EINVAL);
            }
            fo->codecID = CodecID_H264;
            codecOption.decOption.decSetting = (void*)fo;
        }
        else
            return AVERROR(EINVAL);
    }

    DEparams->glogInitialized = glog_initialized;
    codecOption.minLogLevel = deCxt->min_log_level;
    min_loglevel = deCxt->min_log_level;
    if(deCxt->external_log_flag)
    {
        codecOption.logFunction = (void*)(de_log_callback);
    }
    else
    {
        codecOption.logFunction = NULL;
    }

    memcpy(&(DEparams->codecOption), &codecOption, sizeof(CodecAppOption));

    return 0;
}

static av_cold int de_init(AVCodecContext *avctx)
{
    DEContext   *deCxt = avctx->priv_data;

    deCxt->eos_flag = false;
    deCxt->send_end = false;
    deCxt->frame_number = 0;
#ifdef ENABLE_DE_FRAME_LOCK
    deCxt->DeFrameListHead = NULL;
    deCxt->DeFrameListTail = NULL;
#endif

    int ret = set_enc_params(avctx, &(deCxt->encode_params));
    if(ret)
        return ret;

    ret = DistributedEncoder_ParseConfigFile(deCxt->configFile, &(deCxt->encode_params));
    if (ret != DE_STATUS_SUCCESS)
    {
        return AVERROR(EINVAL);
    }

    deCxt->handle = DistributedEncoder_Init(&(deCxt->encode_params));
    if(!deCxt->handle)
    {
        return AVERROR(EINVAL);
    }
    else
    {
        glog_initialized = true;
    }

    return 0;
}

int counter = 0;
static int prepare_input_frame(AVCodecContext *avctx, bool isEncoded, InputFrame** inputFrame, const AVFrame *frame)
{
    DEContext  *deCxt = avctx->priv_data;
    DistributedEncoderParam enc_params = deCxt->encode_params;
    InputFrame* inFrame = *inputFrame;
    int data_num = isEncoded ? 1 : 3;

    if (deCxt->input_yuv_format && 0 == strncmp(deCxt->input_yuv_format, "p010", 4))
    {
        // Load P010 Plane
        inFrame->data[0] = frame->data[0];
        inFrame->copysize[0] = frame->linesize[0] * frame->height;
        inFrame->stride[0] = frame->linesize[0];

        inFrame->data[1] = frame->data[1];
        inFrame->copysize[1] = frame->linesize[1] * frame->height;
        inFrame->stride[1] = frame->linesize[1];
    }
    else if (deCxt->input_yuv_format && 0 == strncmp(deCxt->input_yuv_format, "v210", 4))
    {
        // Load V210 Plane
        inFrame->data[0] = frame->data[0];
        inFrame->copysize[0] = frame->linesize[0] * frame->height;
        inFrame->stride[0] = frame->linesize[0];
    }
    else
    {
        for(int i = 0; i < data_num; i++)
        {
            int factor = i == 0 ? 1 : 2;
            //int copy_size = deCxt->input_type == encoded ? frame->linesize[i] : (frame->linesize[i] * enc_params.streamInfo.frameHeight / factor);
            int copy_size = deCxt->input_type == encoded ? frame->linesize[i] : (frame->linesize[i] * frame->height / factor);

            if(isEncoded)
            {
                inFrame->data[i] = (char*)malloc(sizeof(char*) * copy_size);
                memcpy(inFrame->data[i], frame->data[i], copy_size);
            }
            else
            {
                inFrame->data[i] = frame->data[i];
            }
            inFrame->copysize[i] = copy_size;
            inFrame->stride[i] = frame->linesize[i];
        }
    }

    av_log(avctx, AV_LOG_DEBUG, "prepare_input_frame %d y 0x%lx u 0x%lx v 0x%lx\n", counter++,
           (unsigned long)frame->data[0], (unsigned long)frame->data[1], (unsigned long)frame->data[2]);

    *inputFrame = inFrame;
    return 0;
}

static int de_send_frame(AVCodecContext *avctx, const AVFrame *frame)
{
    DEContext  *deCxt = avctx->priv_data;
    DistributedEncoderParam enc_params = deCxt->encode_params;
    int err = 0;

    if (!frame) {
        InputFrame* lastFrame = (InputFrame*)malloc(sizeof(InputFrame));
        if(!lastFrame)
            return AVERROR(EINVAL);
        memset(lastFrame, 0, sizeof(InputFrame));
        lastFrame->data[0] = NULL;
        lastFrame->stride[0] = 0;
        lastFrame->width = orig_frame_width;//enc_params.streamInfo.frameWidth;
        lastFrame->height = orig_frame_height;//enc_params.streamInfo.frameHeight;
        lastFrame->hScaleFactor = hor_scale_factor;//1;
        lastFrame->vScaleFactor = ver_scale_factor;//1;
        lastFrame->format = enc_params.encoderParams.format;
        lastFrame->picType = PictureType_NONE;
        DistributedEncoder_Process(deCxt->handle, lastFrame);
        free(lastFrame);
        deCxt->send_end = true;
        av_log(avctx, AV_LOG_DEBUG, "Finish sending frames!!!\n");
        return 0;
    }

    InputFrame* inFrame = (InputFrame*)malloc(sizeof(InputFrame));
    if(!inFrame)
        return AVERROR(EINVAL);
    memset(inFrame, 0 , sizeof(InputFrame));

    if(deCxt->input_type != encoded && deCxt->input_type != raw)
    {
        free(inFrame);
        return AVERROR(EINVAL);
    }

    bool isEncoded = (deCxt->input_type == encoded);
    bool useSharedMem = (deCxt->input_type == raw);

#ifdef ENABLE_DE_FRAME_LOCK
    if(deCxt->input_type == raw)
    {
        DeFrameList *DeFrameListNode = (DeFrameList *)malloc(sizeof(DeFrameList));
        if(!DeFrameListNode)
        {
            return AVERROR(ENOMEM);
        }

        DeFrameListNode->next = NULL;
        DeFrameListNode->frame = av_frame_alloc();
        if(!DeFrameListNode->frame)
        {
            free(DeFrameListNode);
            return AVERROR(ENOMEM);
        }

        err = av_frame_ref(DeFrameListNode->frame, frame);
        if(err != 0)
        {
            av_frame_free(DeFrameListNode->frame);
            free(DeFrameListNode);
            return err;
        }

        if(!deCxt->DeFrameListHead && !deCxt->DeFrameListTail)
        {
            deCxt->DeFrameListTail = deCxt->DeFrameListHead = DeFrameListNode;
        }
        else if(deCxt->DeFrameListHead && deCxt->DeFrameListTail)
        {
            deCxt->DeFrameListTail->next = DeFrameListNode;
            deCxt->DeFrameListTail = DeFrameListNode;
        }
        else
        {
            av_frame_free(DeFrameListNode->frame);
            free(DeFrameListNode);
            return AVERROR(EINVAL);
        }
    }
    av_log(avctx, AV_LOG_DEBUG, "de_send_frame frame 0x%lx\n", (unsigned long)deCxt->DeFrameListTail->frame);
#endif

    prepare_input_frame(avctx, isEncoded, &inFrame, frame);

    inFrame->useSharedMem = useSharedMem;
    inFrame->width = frame->width;//enc_params.streamInfo.frameWidth;
    inFrame->height = frame->height;//enc_params.streamInfo.frameHeight ;
    inFrame->hScaleFactor = (uint8_t)((uint32_t)(frame->width) / (uint32_t)(enc_params.streamInfo.frameWidth));
    inFrame->vScaleFactor = (uint8_t)((uint32_t)(frame->height) / (uint32_t)(enc_params.streamInfo.frameHeight));
    if (!orig_frame_width || !orig_frame_height || !hor_scale_factor || !ver_scale_factor)
    {
        orig_frame_width = frame->width;
        orig_frame_height = frame->height;
        hor_scale_factor = inFrame->hScaleFactor;
        ver_scale_factor = inFrame->vScaleFactor;
    }
    inFrame->format = enc_params.encoderParams.format;
    switch (frame->pict_type) {
    case AV_PICTURE_TYPE_I:
        inFrame->picType = PictureType_I;
        break;
    case AV_PICTURE_TYPE_P:
        inFrame->picType = PictureType_P;
        break;
    case AV_PICTURE_TYPE_B:
        inFrame->picType = PictureType_B;
        break;
    default:
        inFrame->picType = PictureType_NONE;
        break;
    }
    DistributedEncoder_Process(deCxt->handle, inFrame);
    if(inFrame)
        free(inFrame);
    return 0;
}

int rcounter=0;
static int de_receive_packet(AVCodecContext *avctx, AVPacket *pkt)
{
    DEContext  *deCxt = avctx->priv_data;
    int ret = 0;

    if (deCxt->eos_flag)
    {
        av_log(avctx, AV_LOG_ERROR, "EOS reached!!\n");
        return AVERROR_EOF;
    }

    char *data = NULL;
    uint64_t size = 0;
    int64_t pts = 0, dts = 0;
    bool eos = false;

    DistributedEncoder_GetPacket(deCxt->handle, &data, &size, &pts, &dts, &eos);
    if(!data && !size && !eos && !deCxt->send_end)
    {
        return AVERROR(EAGAIN);
        // *got_packet = 0;
        // return 0;
    }

    if(!data && !size && deCxt->send_end)
    {
        while(!data)
        {
            DistributedEncoder_GetPacket(deCxt->handle, &data, &size, &pts, &dts, &eos);
            usleep(5000);
        }
    }

#ifdef ENABLE_DE_FRAME_LOCK
    if(deCxt->input_type == raw)
    {
        DeFrameList *DeFrameListNode = NULL;

        if(deCxt->DeFrameListHead && deCxt->DeFrameListTail)
        {
            DeFrameListNode = deCxt->DeFrameListHead;

            if(deCxt->DeFrameListHead == deCxt->DeFrameListTail)
            {
                deCxt->DeFrameListHead = NULL;
                deCxt->DeFrameListTail = NULL;
            }
            else
            {
                deCxt->DeFrameListHead = deCxt->DeFrameListHead->next;
            }
        }
        else
        {
            return AVERROR(EINVAL);
        }

        av_log(avctx, AV_LOG_DEBUG, "de_receive_packet %d frame 0x%lx y 0x%lx u 0x%lx v 0x%lx\n",
               rcounter++,
               (unsigned long)DeFrameListNode->frame,
               (unsigned long)DeFrameListNode->frame->data[0],
               (unsigned long)DeFrameListNode->frame->data[1],
               (unsigned long)DeFrameListNode->frame->data[2]);

        av_frame_free(&DeFrameListNode->frame);
        free(DeFrameListNode);
    }
#endif

    if ((ret = ff_alloc_packet2(avctx, pkt, size, 0)) < 0) {
        av_log(avctx, AV_LOG_ERROR, "Failed to allocate output packet.\n");
        free(data);
        data = NULL;
        return ret;
    }

    if(!data && !size)
    {
        return AVERROR(EAGAIN);
    }
    memcpy(pkt->data, data, size);

    pkt->size = size;
    pkt->pts  = pts;
    pkt->dts = dts;

    int gop = deCxt->encode_params.encoderParams.intra_period + 1;
    if(pkt->pts % gop == 0)
    {
        pkt->flags |= AV_PKT_FLAG_KEY;
    }

    if (deCxt->frame_number == 0)
    {
        Headers* header = (Headers*)malloc(sizeof(Headers));
        if(!header)
        {
             av_log(avctx, AV_LOG_ERROR, "Failed to create header for output .\n");
             return AVERROR(ENOMEM);
        }

        ret = DistributedEncoder_GetParam(deCxt->handle, Param_Header, &header);

        if(ret == DE_STATUS_SUCCESS)
        {
            pkt->side_data = (AVPacketSideData*)malloc(sizeof(AVPacketSideData));
            if(!pkt->side_data)
            {
                free(header);
                return AVERROR(EINVAL);
            }
            pkt->side_data->size = header->headerSize;
            pkt->side_data->data = av_malloc(pkt->side_data->size + AV_INPUT_BUFFER_PADDING_SIZE);
            if (!(pkt->side_data->data))
            {
                av_log(avctx, AV_LOG_ERROR,
                    "Cannot allocate HEVC header of size %d. \n", pkt->side_data->size);
                free(header);
                return AVERROR(ENOMEM);
            }
            memcpy(pkt->side_data->data, header->headerData, pkt->side_data->size);

            free(header);
            header = NULL;

            pkt->side_data->type = AV_PKT_DATA_NEW_EXTRADATA;
            pkt->side_data_elems = 1;
        }
        else
        {
            av_log(avctx, AV_LOG_ERROR, "Failed to get bitstream header.\n");
        }
    }

    //*got_packet = 1;

    deCxt->frame_number++;

    if (eos)
    {
        deCxt->eos_flag = true;
    }

    if(data)
    {
        free(data);
        data = NULL;
    }

    return 0;
}

static int de_encode_frame(AVCodecContext *avctx, AVPacket *pkt, const AVFrame *pic, int *got_packet)
{
    int ret = 0;

    DEContext   *deCxt = avctx->priv_data;
    if(deCxt->eos_flag == true)
    {
        *got_packet = 0;
        return 0;
    }

    ret = de_send_frame(avctx, pic);

    //ret = de_receive_packet(avctx, pkt, got_packet);

    if(!(*got_packet))
    {
        usleep(900000);
    }

    return ret;
}

static av_cold int de_close(AVCodecContext *avctx)
{
    DEContext *deCxt = avctx->priv_data;
    void *setting = NULL;

    if (deCxt) {
#ifdef ENABLE_DE_FRAME_LOCK
        if(deCxt->input_type == raw)
        {
            while(deCxt->DeFrameListHead)
            {
                DeFrameList *DeFrameListNode = deCxt->DeFrameListHead;

                av_frame_free(&DeFrameListNode->frame);
                deCxt->DeFrameListHead = deCxt->DeFrameListHead->next;

                free(DeFrameListNode);
            }
            deCxt->DeFrameListTail = NULL;
        }
#endif

        if (deCxt->handle) {
            DistributedEncoder_Destroy(deCxt->handle);
            deCxt->handle = NULL;
        }

        setting = deCxt->encode_params.codecOption.encOption.encSetting;
        if(setting)
        {
            free(setting);
            setting = NULL;
        }

        setting = deCxt->encode_params.codecOption.decOption.decSetting;
        if(setting)
        {
            free(setting);
            setting = NULL;
        }

        deCxt = NULL;
    }

    return 0;
}

#define OFFSET(x) offsetof(DEContext, x)
#define VE AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_ENCODING_PARAM
static const AVOption options[] = {
    { "config_file", "configure file path for workers information", OFFSET(configFile),
      AV_OPT_TYPE_STRING, { 0 }, 0, 0, VE },

    { "proj_type", "input source projection type, ERP or Cubemap", OFFSET(proj_type),
      AV_OPT_TYPE_STRING, { .str = "ERP" }, 0, 0, VE },

    { "input_type", "input stream type, 0 - encoded, 1 - raw, default is 0", OFFSET(input_type),
      AV_OPT_TYPE_INT, { .i64 = 0 }, 0, 0xff, VE},

    { "input_codec", "input bitstream type, only work when input type is 0-encoded, 0 - HEVC, 1 - AVC, default is 0", OFFSET(inputCodec),
      AV_OPT_TYPE_INT, { .i64 = 0 }, 0, 0xff, VE},

    { "vui", "Enable vui info", OFFSET(vui_info),
      AV_OPT_TYPE_BOOL, { .i64 = 1 }, 0, 1, VE },

    { "aud", "Include AUD", OFFSET(aud),
      AV_OPT_TYPE_BOOL, { .i64 = 0 }, 0, 1, VE },

    { "hielevel", "Hierarchical prediction levels setting", OFFSET(hierarchical_level),
      AV_OPT_TYPE_INT, { .i64 = 3 }, 0, 3, VE , "hielevel"},
        { "flat",   NULL, 0, AV_OPT_TYPE_CONST, { .i64 = 0 },  INT_MIN, INT_MAX, VE, "hielevel" },
        { "2level", NULL, 0, AV_OPT_TYPE_CONST, { .i64 = 1 },  INT_MIN, INT_MAX, VE, "hielevel" },
        { "3level", NULL, 0, AV_OPT_TYPE_CONST, { .i64 = 2 },  INT_MIN, INT_MAX, VE, "hielevel" },
        { "4level", NULL, 0, AV_OPT_TYPE_CONST, { .i64 = 3 },  INT_MIN, INT_MAX, VE, "hielevel" },

    { "la_depth", "Look ahead distance [0, 256]", OFFSET(la_depth),
      AV_OPT_TYPE_INT, { .i64 = -1 }, -1, 256, VE },

    { "preset", "Encoding preset [0, 12] for SVT ([0, 10] for >= 1080p resolution) [0, 9] for all resolution and modes) [0, 6] for MSDK",
      OFFSET(enc_mode), AV_OPT_TYPE_INT, { .i64 = 9 }, 0, 12, VE },

    { "profile", "Profile setting, Main Still Picture Profile not supported", OFFSET(profile),
      AV_OPT_TYPE_INT, { .i64 = FF_PROFILE_HEVC_MAIN }, FF_PROFILE_HEVC_MAIN, FF_PROFILE_HEVC_REXT, VE, "profile"},

    { "tier", "Set tier (general_tier_flag)", OFFSET(tier),
      AV_OPT_TYPE_INT, { .i64 = 0 }, 0, 1, VE, "tier" },
        { "main", NULL, 0, AV_OPT_TYPE_CONST, { .i64 = 0 }, 0, 0, VE, "tier" },
        { "high", NULL, 0, AV_OPT_TYPE_CONST, { .i64 = 1 }, 0, 0, VE, "tier" },

    { "level", "Set level (level_idc)", OFFSET(level),
      AV_OPT_TYPE_INT, { .i64 = 0 }, 0, 0xff, VE, "level" },

    { "rc", "Bit rate control mode", OFFSET(rc_mode),
      AV_OPT_TYPE_INT, { .i64 = 0 }, 0, 1, VE , "rc"},
        { "cqp", NULL, 0, AV_OPT_TYPE_CONST, { .i64 = 0 },  INT_MIN, INT_MAX, VE, "rc" },
        { "vbr", NULL, 0, AV_OPT_TYPE_CONST, { .i64 = 1 },  INT_MIN, INT_MAX, VE, "rc" },

    { "qp", "QP value for intra frames", OFFSET(qp),
      AV_OPT_TYPE_INT, { .i64 = 32 }, 0, 51, VE },

    { "sc_detection", "Scene change detection", OFFSET(scd),
      AV_OPT_TYPE_BOOL, { .i64 = 0 }, 0, 1, VE },

    { "tune", "Quality tuning mode", OFFSET(tune), AV_OPT_TYPE_INT, { .i64 = 1 }, 0, 2, VE, "tune" },
        { "sq", "Visually optimized mode", 0,
          AV_OPT_TYPE_CONST, { .i64 = 0 },  INT_MIN, INT_MAX, VE, "tune" },
        { "oq",  "PSNR / SSIM optimized mode",  0,
          AV_OPT_TYPE_CONST, { .i64 = 1 },  INT_MIN, INT_MAX, VE, "tune" },
        { "vmaf", "VMAF optimized mode", 0,
          AV_OPT_TYPE_CONST, { .i64 = 2 },  INT_MIN, INT_MAX, VE, "tune" },

    { "pred_struct", "Prediction structure used to construct GOP", OFFSET(gop_pred_structure),
      AV_OPT_TYPE_INT, { .i64 = 0 }, 0, 2, VE, "pred_struct" },
        { "IPPP", "P is low delay P", 0, AV_OPT_TYPE_CONST, { .i64 = 0 }, INT_MIN, INT_MAX, VE, "pred_struct" },
        { "Ibbb", "b is low delay B", 1, AV_OPT_TYPE_CONST, { .i64 = 1 }, INT_MIN, INT_MAX, VE, "pred_struct" },
        { "IBBB", "B is normal bi-directional B", 2, AV_OPT_TYPE_CONST, { .i64 = 2 }, INT_MIN, INT_MAX, VE, "pred_struct" },

    { "bl_mode", "Random Access Prediction Structure type setting", OFFSET(base_layer_switch_mode),
      AV_OPT_TYPE_BOOL, { .i64 = 0 }, 0, 1, VE },

    { "forced-idr", "If forcing keyframes, force them as IDR frames.", OFFSET(forced_idr),
      AV_OPT_TYPE_BOOL,   { .i64 = 1 }, 0, 1, VE },

    { "hdr", "High dynamic range input", OFFSET(hdr),
      AV_OPT_TYPE_BOOL,   { .i64 = 0 }, 0, 1, VE },

    { "asm_type", "Assembly instruction set type [0: C Only, 1: Auto]", OFFSET(asm_type),
      AV_OPT_TYPE_BOOL,   { .i64 = 1 }, 0, 1, VE },

    { "tile_column", "Tile column count number, default is 1", OFFSET(tile_column),
      AV_OPT_TYPE_INT, { .i64 = 1 }, 0, 256, VE },

    { "tile_row", "Tile row count number, default is 1", OFFSET(tile_row),
      AV_OPT_TYPE_INT, { .i64 = 1 }, 0, 256, VE },

    { "in_parallel", "Multiple encoders running in parallel [0: Off, 1: On]", OFFSET(in_parallel),
      AV_OPT_TYPE_BOOL, { .i64 = 0 }, 0, 1, VE },

    { "gpucopy", "GPU copy trigger for MSDK (To be ignored if SVT)", OFFSET(gpucopy),
      AV_OPT_TYPE_BOOL,   { .i64 = 1 }, 0, 1, VE },

    { "external_log_flag", "whether external log callback is needed", OFFSET(external_log_flag),
      AV_OPT_TYPE_BOOL, { .i64 = 0 }, 0, 1, VE },

    { "min_log_level", "Minimal log level of output [0: INFO, 1: WARNING, 2: ERROR, 3: FATAL]", OFFSET(min_log_level),
      AV_OPT_TYPE_INT, { .i64 = 2 }, 0, 3, VE },

    { "input_yuv_format", "Input YUV format", OFFSET(input_yuv_format),
      AV_OPT_TYPE_STRING, { .str = NULL }, 0, 0, VE },

    {NULL},
};

static const AVClass class = {
    .class_name = "distributed_encoder",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

static const AVCodecDefault de_defaults[] = {
    { "b",         "7M"    },
    { "flags",     "+cgop" },
    { "qmin",      "10"    },
    { "qmax",      "48"    },
    { "g",         "-2"    },
    { NULL },
};

AVCodec ff_distributed_encoder = {
    .name           = "distributed_encoder",
    .long_name      = NULL_IF_CONFIG_SMALL("distributed HEVC encoder"),
    .priv_data_size = sizeof(DEContext),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_HEVC,
    .init           = de_init,
    .send_frame     = de_send_frame,
    .receive_packet = de_receive_packet,
    .close          = de_close,
    .capabilities   = AV_CODEC_CAP_DELAY | AV_CODEC_CAP_AUTO_THREADS,
    .pix_fmts       = (const enum AVPixelFormat[]){ AV_PIX_FMT_YUV420P,
                                                    AV_PIX_FMT_YUV420P10,
                                                    AV_PIX_FMT_YUV422P,
                                                    AV_PIX_FMT_YUV422P10,
                                                    AV_PIX_FMT_YUV444P,
                                                    AV_PIX_FMT_YUV444P10,
                                                    AV_PIX_FMT_NONE },
    .priv_class     = &class,
    .defaults       = de_defaults,
    .caps_internal  = FF_CODEC_CAP_INIT_CLEANUP,
    .wrapper_name   = "distributed_encoder",
};
