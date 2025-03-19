/*
 * Intel tile Dash muxer
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

#include <unistd.h>
#include <sys/stat.h>

#include "libavutil/avassert.h"
#include "libavutil/avutil.h"
#include "libavutil/avstring.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/mathematics.h"
#include "libavutil/opt.h"
#include "libavutil/rational.h"
#include "libavutil/time_internal.h"

#include "avformat.h"
#include "avio_internal.h"

#include "360SCVPAPI.h"
#include "VROmafPackingAPI.h"
#include "common_data.h"

static uint32_t min_loglevel = 2;

typedef struct {
    int         streamIdx;
    FrameBSInfo *frameBSInfo;
} BufferedFrame;

typedef struct {
    const AVClass  *class;  /* Class for private options. */
    Handler        handler;
    InitialInfo    *initInfo;
    int            inStreamsNum;

    const char     *proj_type;
    const char     *face_file;
    int            viewport_w;
    int            viewport_h;
    float          viewport_yaw;
    float          viewport_pitch;
    float          viewport_fov_hor;
    float          viewport_fov_ver;
    int            window_size;
    int            extra_window_size;
    int            has_extractor;
    const char     *packingPluginPath;
    const char     *packingPluginName;
    bool           fixedPackedPicRes;
    const char     *videoPluginPath;
    const char     *videoPluginName;
    const char     *audioPluginPath;
    const char     *audioPluginName;
    bool           cmafEnabled;
    const char     *chunkInfoType;
    int64_t        chunkDur;
    const char     *segWriterPluginPath;
    const char     *segWriterPluginName;
    const char     *mpdWriterPluginPath;
    const char     *mpdWriterPluginName;
    int            need_buffered_frames;
    uint16_t       extractors_per_thread;
    int64_t        seg_duration;
    int            remove_at_exit;
    int            use_template;
    int            use_timeline;
    char           dirname[1024];
    const char     *out_name;
    const char     *base_url;
    const char     *utc_timing_url;
    int            is_live;
    int64_t        target_latency;
    int64_t        min_latency;
    int64_t        max_latency;
    int            split_tile;
    int64_t        frameNum;
    BufferedFrame  bufferedFrames[1024];
    int            bufferedFramesNum;
    bool           need_external_log;
    int            min_log_level;
    bool           first_audio_input;
} OMAFContext;

static uint8_t convert_face_index(char *face_name)
{
    if (0 == strncmp(face_name, "PY", 2))
        return 0;
    else if (0 == strncmp(face_name, "PX", 2))
        return 1;
    else if (0 == strncmp(face_name, "NY", 2))
        return 2;
    else if (0 == strncmp(face_name, "NZ", 2))
        return 3;
    else if (0 == strncmp(face_name, "NX", 2))
        return 4;
    else if (0 == strncmp(face_name, "PZ", 2))
        return 5;
    else
        return 255;
}

static E_TransformType convert_transform_type(char *transform_name)
{
    if (0 == strncmp(transform_name, "NO_TRANSFORM", 12))
        return NO_TRANSFORM;
    else if (0 == strncmp(transform_name, "MIRRORING_HORIZONTALLY", 22))
        return MIRRORING_HORIZONTALLY;
    else if (0 == strncmp(transform_name, "ROTATION_180_ANTICLOCKWISE", 26))
        return ROTATION_180_ANTICLOCKWISE;
    else if (0 == strncmp(transform_name, "ROTATION_180_ANTICLOCKWISE_AFTER_MIRRORING_HOR", 46))
        return ROTATION_180_ANTICLOCKWISE_AFTER_MIRRORING_HOR;
    else if (0 == strncmp(transform_name, "ROTATION_90_ANTICLOCKWISE_BEFORE_MIRRORING_HOR", 46))
        return ROTATION_90_ANTICLOCKWISE_BEFORE_MIRRORING_HOR;
    else if (0 == strncmp(transform_name, "ROTATION_90_ANTICLOCKWISE", 25))
        return ROTATION_90_ANTICLOCKWISE;
    else if (0 == strncmp(transform_name, "ROTATION_270_ANTICLOCKWISE_BEFORE_MIRRORING_HOR", 47))
        return ROTATION_270_ANTICLOCKWISE_BEFORE_MIRRORING_HOR;
    else if (0 == strncmp(transform_name, "ROTATION_270_ANTICLOCKWISE", 26))
        return ROTATION_270_ANTICLOCKWISE;
    else
        return NO_TRANSFORM;
}

static void ffmpeg_log_callback(LogLevel log_level, const char* file_name, uint64_t line_num, const char* fmt, ...)
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

static int omaf_init(AVFormatContext *s)
{
    OMAFContext *c = s->priv_data;
    int ret = 0;

    c->frameNum = -1;

    c->initInfo = (InitialInfo*)malloc(sizeof(InitialInfo));
    if (!(c->initInfo))
    {
        av_log(s, AV_LOG_ERROR, "Failed to malloc memory for initial information \n");
        return AVERROR(ENOMEM);
    }
    InitialInfo *initInfo = c->initInfo;
    memset(initInfo, 0, sizeof(InitialInfo));

    initInfo->bsNumVideo = 0;
    initInfo->bsNumAudio = 0;

    for (int i = 0; i < s->nb_streams; i++)
    {
        AVStream *st = s->streams[i];
        switch(st->codecpar->codec_type)
        {
            case AVMEDIA_TYPE_VIDEO:
                initInfo->bsNumVideo++;
                break;
            case AVMEDIA_TYPE_AUDIO:
                initInfo->bsNumAudio++;
                break;
            default:
                break;
        }
    }

    if (initInfo->bsNumVideo > 2)
    {
        c->has_extractor = 0;
    }

    initInfo->videoProcessPluginPath = c->videoPluginPath;
    initInfo->videoProcessPluginName = c->videoPluginName;

    if (initInfo->bsNumAudio)
    {
        if ((0 == strncmp(c->audioPluginPath, "NULL", 4)) ||
            (0 == strncmp(c->audioPluginName, "NULL", 4)))
        {
            av_log(s, AV_LOG_ERROR, "No audio stream process plugin is set but there is indeed audio stream input !\n");
            return AVERROR_INVALIDDATA;
        }

        initInfo->audioProcessPluginPath = c->audioPluginPath;
        initInfo->audioProcessPluginName = c->audioPluginName;
    }

    if (c->has_extractor)
    {
        initInfo->packingPluginPath = c->packingPluginPath;
        if (initInfo->bsNumVideo == 1)
        {
            initInfo->packingPluginName = "SingleVideoPacking";
        }
        else if (initInfo->bsNumVideo == 2)
        {
            initInfo->packingPluginName = c->packingPluginName;
        }
        else
        {
            av_log(s, AV_LOG_ERROR, "Not correct video streams number for VR OMAF Packing \n");
            return AVERROR(EINVAL);
        }
        initInfo->fixedPackedPicRes = c->fixedPackedPicRes;
    }
    else
    {
        initInfo->packingPluginPath = NULL;
        initInfo->packingPluginName = NULL;
        initInfo->fixedPackedPicRes = false;
    }

    initInfo->cmafEnabled = c->cmafEnabled;
    printf("CMAF enabled %d \n", initInfo->cmafEnabled);
    initInfo->segWriterPluginPath = c->segWriterPluginPath;
    initInfo->segWriterPluginName = c->segWriterPluginName;
    if (initInfo->cmafEnabled)
    {
        if (0 == strcmp(initInfo->segWriterPluginName, "SegmentWriter"))
        {
            av_log(s, AV_LOG_ERROR, "Plugin SegmentWriter can't generate CMAF segments !\n");
            return AVERROR(EINVAL);
        }
    }

    initInfo->mpdWriterPluginPath = c->mpdWriterPluginPath;
    initInfo->mpdWriterPluginName = c->mpdWriterPluginName;

    min_loglevel = c->min_log_level;
    if (c->need_external_log)
    {
        initInfo->logFunction = (void*)(ffmpeg_log_callback);
    }
    else
    {
        initInfo->logFunction = NULL;
    }

    initInfo->bsBuffers = (BSBuffer*)malloc(sizeof(BSBuffer) * (initInfo->bsNumVideo + initInfo->bsNumAudio));
    if (!(initInfo->bsBuffers))
    {
        av_log(s, AV_LOG_ERROR, "Failed to malloc memory for video bitstream buffer \n");
        return AVERROR(ENOMEM);
    }
    memset(initInfo->bsBuffers, 0, sizeof(BSBuffer) * (initInfo->bsNumVideo + initInfo->bsNumAudio));
    initInfo->viewportInfo = (ViewportInformation*)malloc(sizeof(ViewportInformation));
    if (!(initInfo->viewportInfo))
    {
        av_log(s, AV_LOG_ERROR, "Failed to malloc memory for viewport information \n");
        return AVERROR(ENOMEM);
    }
    memset(initInfo->viewportInfo, 0, sizeof(ViewportInformation));
    initInfo->viewportInfo->viewportWidth = c->viewport_w;
    initInfo->viewportInfo->viewportHeight = c->viewport_h;
    initInfo->viewportInfo->viewportPitch = c->viewport_pitch;
    initInfo->viewportInfo->viewportYaw = c->viewport_yaw;
    initInfo->viewportInfo->horizontalFOVAngle = c->viewport_fov_hor;
    initInfo->viewportInfo->verticalFOVAngle = c->viewport_fov_ver;
    initInfo->viewportInfo->outGeoType = E_SVIDEO_VIEWPORT;
    if (0 == strncmp(c->proj_type, "ERP", 3))
    {
        initInfo->viewportInfo->inGeoType = E_SVIDEO_EQUIRECT;
    }
    else if (0 == strncmp(c->proj_type, "Cube", 4))
    {
        initInfo->viewportInfo->inGeoType = E_SVIDEO_CUBEMAP;
    }
    else if (0 == strncmp(c->proj_type, "Planar", 6))
    {
        initInfo->viewportInfo->inGeoType = E_SVIDEO_PLANAR;
    }

    initInfo->segmentationInfo = (SegmentationInfo*)malloc(sizeof(SegmentationInfo));
    if (!(initInfo->segmentationInfo))
    {
        av_log(s, AV_LOG_ERROR, "Failed to malloc memory for segmentation information \n");
        return AVERROR(ENOMEM);
    }
    memset(initInfo->segmentationInfo, 0, sizeof(SegmentationInfo));
    initInfo->segmentationInfo->windowSize = c->window_size;
    initInfo->segmentationInfo->extraWindowSize = c->extra_window_size;
    initInfo->segmentationInfo->needBufedFrames = c->need_buffered_frames;
    initInfo->segmentationInfo->extractorTracksPerSegThread = c->extractors_per_thread;
    initInfo->segmentationInfo->segDuration = c->seg_duration / 1000000;
    initInfo->segmentationInfo->removeAtExit = c->remove_at_exit;
    initInfo->segmentationInfo->useTemplate = c->use_template;
    initInfo->segmentationInfo->useTimeline = c->use_timeline;

    initInfo->segmentationInfo->dirName = (char*)malloc(1024 * sizeof(char));
    av_strlcpy(initInfo->segmentationInfo->dirName, s->url, sizeof(c->dirname));
    initInfo->segmentationInfo->outName = c->out_name;
    initInfo->segmentationInfo->baseUrl = c->base_url;
    initInfo->segmentationInfo->utcTimingUrl = c->utc_timing_url;
    initInfo->segmentationInfo->isLive = c->is_live;
    initInfo->segmentationInfo->splitTile = c->split_tile;
    initInfo->segmentationInfo->hasMainAS = true;
    initInfo->segmentationInfo->chunkDuration = c->chunkDur;
    //printf("initInfo->segmentationInfo->chunkDuration %ld \n", initInfo->segmentationInfo->chunkDuration);
    if (initInfo->cmafEnabled && !(initInfo->segmentationInfo->chunkDuration))
    {
        av_log(s, AV_LOG_ERROR, "Chunk duration can't be zero when CMAF ENABLED !\n");
        return AVERROR(EINVAL);
    }
    if (initInfo->cmafEnabled && ((initInfo->segmentationInfo->segDuration * 1000) % (initInfo->segmentationInfo->chunkDuration)))
    {
        av_log(s, AV_LOG_ERROR, "Segment duration can't be divided by chunk duration !\n");
        return AVERROR(EINVAL);
    }

    if (initInfo->segmentationInfo->isLive)
    {
        initInfo->segmentationInfo->targetLatency = c->target_latency;
        initInfo->segmentationInfo->minLatency = c->min_latency;
        initInfo->segmentationInfo->maxLatency = c->max_latency;
    }

    if (initInfo->cmafEnabled)
    {
        if (0 == strncmp(c->chunkInfoType, "sidx_only", 9))
        {
            initInfo->segmentationInfo->chunkInfoType = E_CHUNKINFO_SIDX_ONLY;
        }
        else if (0 == strncmp(c->chunkInfoType, "cloc_only", 9))
        {
            initInfo->segmentationInfo->chunkInfoType = E_CHUNKINFO_CLOC_ONLY;
        }
        else if (0 == strncmp(c->chunkInfoType, "sidx_cloc", 9))
        {
            initInfo->segmentationInfo->chunkInfoType = E_CHUNKINFO_SIDX_AND_CLOC;
        }
        else
        {
            initInfo->segmentationInfo->chunkInfoType = E_NO_CHUNKINFO;
            av_log(s, AV_LOG_INFO, "No chunk info type set or invalid set, then set to default value!\n");
        }
    }

    if (0 == strncmp(c->proj_type, "ERP", 3))
    {
        initInfo->projType = E_SVIDEO_EQUIRECT;
        initInfo->cubeMapInfo = NULL;
    }
    else if (0 == strncmp(c->proj_type, "Cube", 4))
    {
        initInfo->projType = E_SVIDEO_CUBEMAP;
    }
    else if (0 == strncmp(c->proj_type, "Planar", 6))
    {
        initInfo->projType = E_SVIDEO_PLANAR;
    }

    if (initInfo->projType == E_SVIDEO_CUBEMAP)
    {
        if (!(c->face_file))
        {
            av_log(s, AV_LOG_ERROR,
                    "face_file should not be null when input source is from Cubemap projection! \n");
            return AVERROR(EINVAL);
        }

        initInfo->cubeMapInfo = (InputCubeMapInfo*)malloc(sizeof(InputCubeMapInfo));
        memset(initInfo->cubeMapInfo, 0, sizeof(InputCubeMapInfo));
        FILE *fp = fopen(c->face_file, "r");
        if (!fp)
        {
            av_log(s, AV_LOG_ERROR,
                    "Failed to open cubemap face file !\n");
            return AVERROR(ENOMEM);
        }
        char face_name[128] = { 0 };
        char transform_name[128] = { 0 };
        fscanf(fp, "%s %s", face_name, transform_name);
        initInfo->cubeMapInfo->face0MapInfo.mappedStandardFaceId = convert_face_index(face_name);
        initInfo->cubeMapInfo->face0MapInfo.transformType = convert_transform_type(transform_name);

        memset(face_name, 0, 128);
        memset(transform_name, 0, 128);
        fscanf(fp, "%s %s", face_name, transform_name);
        initInfo->cubeMapInfo->face1MapInfo.mappedStandardFaceId = convert_face_index(face_name);
        initInfo->cubeMapInfo->face1MapInfo.transformType = convert_transform_type(transform_name);

        memset(face_name, 0, 128);
        memset(transform_name, 0, 128);
        fscanf(fp, "%s %s", face_name, transform_name);
        initInfo->cubeMapInfo->face2MapInfo.mappedStandardFaceId = convert_face_index(face_name);
        initInfo->cubeMapInfo->face2MapInfo.transformType = convert_transform_type(transform_name);

        memset(face_name, 0, 128);
        memset(transform_name, 0, 128);
        fscanf(fp, "%s %s", face_name, transform_name);
        initInfo->cubeMapInfo->face3MapInfo.mappedStandardFaceId = convert_face_index(face_name);
        initInfo->cubeMapInfo->face3MapInfo.transformType = convert_transform_type(transform_name);

        memset(face_name, 0, 128);
        memset(transform_name, 0, 128);
        fscanf(fp, "%s %s", face_name, transform_name);
        initInfo->cubeMapInfo->face4MapInfo.mappedStandardFaceId = convert_face_index(face_name);
        initInfo->cubeMapInfo->face4MapInfo.transformType = convert_transform_type(transform_name);

        memset(face_name, 0, 128);
        memset(transform_name, 0, 128);
        fscanf(fp, "%s %s", face_name, transform_name);
        initInfo->cubeMapInfo->face5MapInfo.mappedStandardFaceId = convert_face_index(face_name);
        initInfo->cubeMapInfo->face5MapInfo.transformType = convert_transform_type(transform_name);

        fclose(fp);
        fp = NULL;
    }
    memset(c->bufferedFrames, 0, 1024 * sizeof(BufferedFrame));
    c->bufferedFramesNum = 0;
    c->inStreamsNum = 0;
    c->handler = NULL;

    return 0;
}

static void omaf_free(AVFormatContext *s)
{
    OMAFContext *c = s->priv_data;

    VROmafPackingClose(c->handler);

    if (c->initInfo->bsBuffers)
    {
        for (int i = 0; i < (c->initInfo->bsNumVideo + c->initInfo->bsNumAudio); i++)
        {
            if (c->initInfo->bsBuffers[i].data)
            {
                free(c->initInfo->bsBuffers[i].data);
                c->initInfo->bsBuffers[i].data = NULL;
            }
        }
        free(c->initInfo->bsBuffers);
        c->initInfo->bsBuffers = NULL;
    }

    if(c->initInfo->viewportInfo)
    {
        free(c->initInfo->viewportInfo);
        c->initInfo->viewportInfo = NULL;
    }

    if(c->initInfo->segmentationInfo)
    {
        if (c->initInfo->segmentationInfo->dirName)
        {
            free(c->initInfo->segmentationInfo->dirName);
            c->initInfo->segmentationInfo->dirName = NULL;
        }

        free(c->initInfo->segmentationInfo);
        c->initInfo->segmentationInfo = NULL;
    }

    if (c->initInfo->cubeMapInfo)
    {
        free(c->initInfo->cubeMapInfo);
        c->initInfo->cubeMapInfo = NULL;
    }

    if(c->initInfo)
    {
        free(c->initInfo);
        c->initInfo = NULL;
    }

    return;
}

static int omaf_write_header(AVFormatContext *s)
{
    return 0;
}

static int omaf_write_packet(AVFormatContext *s, AVPacket *pkt)
{
    OMAFContext *c = s->priv_data;
    int ret = 0;

    if(!c->handler)
    {
        int i = pkt->stream_index;
        AVStream *st = s->streams[i];

        if (((st->codecpar->codec_id == AV_CODEC_ID_HEVC) || (st->codecpar->codec_id == AV_CODEC_ID_H264)) && (pkt->pts == 0))
        {
            c->initInfo->bsBuffers[i].dataSize = pkt->side_data->size;

            c->initInfo->bsBuffers[i].data = (uint8_t*)malloc(c->initInfo->bsBuffers[i].dataSize * sizeof(uint8_t));
            if (!(c->initInfo->bsBuffers[i].data))
            {
                av_log(s, AV_LOG_ERROR, "Failed to malloc memory for holding bitstream header data \n");
                return -1;
            }
            memcpy(c->initInfo->bsBuffers[i].data, pkt->side_data->data, c->initInfo->bsBuffers[i].dataSize);

        }
        else if ((st->codecpar->codec_id == AV_CODEC_ID_AAC) && !c->first_audio_input)
        {
            c->initInfo->bsBuffers[i].dataSize = pkt->size;

            c->initInfo->bsBuffers[i].data = (uint8_t*)malloc(c->initInfo->bsBuffers[i].dataSize * sizeof(uint8_t));
            if (!(c->initInfo->bsBuffers[i].data))
            {
                av_log(s, AV_LOG_ERROR, "Failed to malloc memory for holding bitstream header data \n");
                return -1;
            }
            memcpy(c->initInfo->bsBuffers[i].data, pkt->data, c->initInfo->bsBuffers[i].dataSize);
        }

        if (st->codecpar->codec_id == AV_CODEC_ID_H264)
        {
            c->initInfo->bsBuffers[i].codecId = CODEC_ID_H264;
        }
        else if (st->codecpar->codec_id == AV_CODEC_ID_HEVC)
        {
            c->initInfo->bsBuffers[i].codecId = CODEC_ID_H265;
        }
        else if (st->codecpar->codec_id == AV_CODEC_ID_AAC)
        {
            c->initInfo->bsBuffers[i].codecId = CODEC_ID_AAC;
        }

        c->initInfo->bsBuffers[i].bitRate = st->codecpar->bit_rate;
        c->initInfo->bsBuffers[i].frameRate.num = st->avg_frame_rate.num;
        c->initInfo->bsBuffers[i].frameRate.den = st->avg_frame_rate.den;
        c->initInfo->bsBuffers[i].mediaType = -1;
        switch(st->codecpar->codec_type)
        {
            case AVMEDIA_TYPE_VIDEO:
                c->initInfo->bsBuffers[i].mediaType = VIDEOTYPE;
                break;
            case AVMEDIA_TYPE_AUDIO:
                c->initInfo->bsBuffers[i].mediaType = AUDIOTYPE;
                c->initInfo->bsBuffers[i].audioObjType = st->codecpar->profile ;
                c->initInfo->bsBuffers[i].sampleRate = st->codecpar->sample_rate;
                c->initInfo->bsBuffers[i].channelNum = st->codecpar->channels;
                break;
            case AVMEDIA_TYPE_SUBTITLE:
                c->initInfo->bsBuffers[i].mediaType = SUBTITLETYPE;
                break;
            default:
                break;
        }

        if (((st->codecpar->codec_id == AV_CODEC_ID_HEVC) || (st->codecpar->codec_id == AV_CODEC_ID_H264)) && (pkt->pts == 0))
        {
            c->inStreamsNum++;
        }
        else if ((st->codecpar->codec_id == AV_CODEC_ID_AAC) && !c->first_audio_input)
        {
            c->inStreamsNum++;
            c->first_audio_input = true;
        }

        FrameBSInfo* frameInfo = (FrameBSInfo*)malloc(sizeof(FrameBSInfo));
        memset(frameInfo, 0, sizeof(FrameBSInfo));
        if (((st->codecpar->codec_id == AV_CODEC_ID_HEVC) || (st->codecpar->codec_id == AV_CODEC_ID_H264)) && (pkt->pts == 0))
        {
            frameInfo->dataSize = pkt->size - pkt->side_data->size;
        }
        else if (((st->codecpar->codec_id == AV_CODEC_ID_HEVC) || (st->codecpar->codec_id == AV_CODEC_ID_H264)) && (pkt->pts != 0))
        {
            frameInfo->dataSize = pkt->size;
        }
        else if (st->codecpar->codec_id == AV_CODEC_ID_AAC)
        {
            frameInfo->dataSize = pkt->size;
        }

        frameInfo->data = (uint8_t*)malloc(frameInfo->dataSize * sizeof(uint8_t));
        if (!(frameInfo->data))
        {
            av_log(s, AV_LOG_ERROR, "Failed to malloc memory for buffered frame data \n");
            return -1;
        }

        if (((st->codecpar->codec_id == AV_CODEC_ID_HEVC) || (st->codecpar->codec_id == AV_CODEC_ID_H264)) && (pkt->pts == 0))
        {
            memcpy(frameInfo->data, pkt->data + pkt->side_data->size, frameInfo->dataSize);
            frameInfo->isKeyFrame = (pkt->flags & AV_PKT_FLAG_KEY);
        }
        else if (((st->codecpar->codec_id == AV_CODEC_ID_HEVC) || (st->codecpar->codec_id == AV_CODEC_ID_H264)) && (pkt->pts != 0))
        {
            memcpy(frameInfo->data, pkt->data, frameInfo->dataSize);
            frameInfo->isKeyFrame = (pkt->flags & AV_PKT_FLAG_KEY);
        }
        else if (st->codecpar->codec_id == AV_CODEC_ID_AAC)
        {
            memcpy(frameInfo->data, pkt->data, frameInfo->dataSize);
            frameInfo->isKeyFrame = true;
        }
        frameInfo->pts = pkt->pts;

        c->bufferedFrames[c->bufferedFramesNum].streamIdx = pkt->stream_index;
        c->bufferedFrames[c->bufferedFramesNum].frameBSInfo = frameInfo;
        c->bufferedFramesNum++;

        if (((st->codecpar->codec_id == AV_CODEC_ID_HEVC) || (st->codecpar->codec_id == AV_CODEC_ID_H264)) && (pkt->pts == 0))
        {
            free(pkt->side_data->data);
            pkt->side_data->data = NULL;
            pkt->side_data->size = 0;
            free(pkt->side_data);
            pkt->side_data = NULL;
            pkt->side_data_elems = 0;
        }

        if (c->inStreamsNum == (c->initInfo->bsNumVideo + c->initInfo->bsNumAudio))
        {
            c->handler = VROmafPackingInit(c->initInfo);
            if (!(c->handler))
            {
                av_log(s, AV_LOG_ERROR, "Failed to create VR Omaf Packing handler \n");
                return -1;
            }
            c->frameNum++;
        }
    }
    else
    {
        if (c->bufferedFramesNum > 0)
        {
            for (int i = 0; i < c->bufferedFramesNum; i++)
            {
                ret = VROmafPackingWriteSegment(c->handler, c->bufferedFrames[i].streamIdx, c->bufferedFrames[i].frameBSInfo);
                if (ret != 0)
                {
                    av_log(s, AV_LOG_ERROR, "Failed to write segment.\n" );
                    return ret;
                }

                free(c->bufferedFrames[i].frameBSInfo->data);
                c->bufferedFrames[i].frameBSInfo->data = NULL;
                c->bufferedFrames[i].frameBSInfo->dataSize = 0;
                free(c->bufferedFrames[i].frameBSInfo);
                c->bufferedFrames[i].frameBSInfo = NULL;
            }
            c->bufferedFramesNum = 0;
            c->frameNum++;
        }

        FrameBSInfo* frameInfo = (FrameBSInfo*)malloc(sizeof(FrameBSInfo));

        frameInfo->data = pkt->data;
        frameInfo->dataSize = pkt->size;

        frameInfo->isKeyFrame = (pkt->flags & AV_PKT_FLAG_KEY);

        frameInfo->pts = pkt->pts;

        ret = VROmafPackingWriteSegment(c->handler, pkt->stream_index, frameInfo);
        if(ret !=0 )
        {
            av_log(s, AV_LOG_ERROR, "Failed to write segment.\n" );
        }

        free(frameInfo);
        frameInfo = NULL;
        c->frameNum++;
    }

    return ret;
}

static int omaf_write_trailer(AVFormatContext *s)
{
    OMAFContext *c = s->priv_data;
    int ret = 0;

    ret = VROmafPackingEndStreams(c->handler);
    if(ret)
    {
        av_log(s, AV_LOG_ERROR, "Failed to write end mpd \n" );
        return ret;
    }

    return 0;
}

#define OFFSET(x) offsetof(OMAFContext, x)
#define E AV_OPT_FLAG_ENCODING_PARAM
static const AVOption options[] = {
    { "packing_proj_type", "input source projection type, ERP or Cubemap", OFFSET(proj_type), AV_OPT_TYPE_STRING, { .str = "ERP" }, 0, 0, E },
    { "cubemap_face_file", "configure input cubemap face relation to face layout defined in OMAF for cube-3x2", OFFSET(face_file), AV_OPT_TYPE_STRING, { 0 }, 0, 0, E },
    { "viewport_w", "set viewport width", OFFSET(viewport_w), AV_OPT_TYPE_INT, { .i64 = 1024 }, 0, INT_MAX, E },
    { "viewport_h", "set viewport height", OFFSET(viewport_h), AV_OPT_TYPE_INT, { .i64 = 1024 }, 0, INT_MAX, E },
    { "viewport_yaw", "set viewport yaw angle, which is the angle around y axis", OFFSET(viewport_yaw), AV_OPT_TYPE_FLOAT, { .dbl = 90 }, 0, 180, E },
    { "viewport_pitch", "set viewport pitch angle, which is the angle around x axis", OFFSET(viewport_pitch), AV_OPT_TYPE_FLOAT, { .dbl = 0 }, 0, 100, E },
    { "viewport_fov_hor", "set horizontal angle of field of view (FOV)", OFFSET(viewport_fov_hor), AV_OPT_TYPE_FLOAT, { .dbl = 80 }, 0, 180, E },
    { "viewport_fov_ver", "set vertical angle of field of view (FOV)", OFFSET(viewport_fov_ver), AV_OPT_TYPE_FLOAT, { .dbl = 80 }, 0, 100, E },
    { "window_size", "number of segments kept in the manifest", OFFSET(window_size), AV_OPT_TYPE_INT, { .i64 = 5 }, 0, INT_MAX, E },
    { "extra_window_size", "number of segments kept outside of the manifest before removing from disk", OFFSET(extra_window_size), AV_OPT_TYPE_INT, { .i64 = 15 }, 0, INT_MAX, E },
    { "split_tile", "need split the stream to tiles if input is tile-based hevc stream", OFFSET(split_tile), AV_OPT_TYPE_INT, { .i64 = 0 }, 0, 2, E },
    { "seg_duration", "segment duration (in seconds, fractional value can be set)", OFFSET(seg_duration), AV_OPT_TYPE_DURATION, { .i64 = 5000000 }, 0, INT_MAX, E },
    { "remove_at_exit", "remove all segments when finished", OFFSET(remove_at_exit), AV_OPT_TYPE_BOOL, { .i64 = 0 }, 0, 1, E },
    { "use_template", "Use SegmentTemplate instead of SegmentList", OFFSET(use_template), AV_OPT_TYPE_BOOL, { .i64 = 1 }, 0, 1, E },
    { "use_timeline", "Use SegmentTimeline in SegmentTemplate", OFFSET(use_timeline), AV_OPT_TYPE_BOOL, { .i64 = 1 }, 0, 1, E },
    { "utc_timing_url", "URL of the page that will return the UTC timestamp in ISO format", OFFSET(utc_timing_url), AV_OPT_TYPE_STRING, { 0 }, 0, 0, E },
    { "is_live", "Enable/Disable streaming mode of output. Each frame will be moof fragment", OFFSET(is_live), AV_OPT_TYPE_BOOL, { .i64 = 0 }, 0, 1, E },
    { "base_url", "MPD BaseURL", OFFSET(base_url), AV_OPT_TYPE_STRING, { 0 }, 0, 0, E },
    { "out_name", "name prefix for all dash output files", OFFSET(out_name), AV_OPT_TYPE_STRING, {.str = "dash-stream"}, 0, 0, E },
    { "need_buffered_frames", "needed buffered frames number before packing starts", OFFSET(need_buffered_frames), AV_OPT_TYPE_INT, { .i64 = 15 }, 0, INT_MAX, E },
    { "extractors_per_thread", "extractor tracks per segmentation thread", OFFSET(extractors_per_thread), AV_OPT_TYPE_INT, { .i64 = 0 }, 0, INT_MAX, E },
    { "has_extractor", "Enable/Disable OMAF extractor tracks", OFFSET(has_extractor), AV_OPT_TYPE_INT, { .i64 = 1 }, 0, INT_MAX, E },
    { "packing_plugin_path", "OMAF Packing plugin path", OFFSET(packingPluginPath), AV_OPT_TYPE_STRING, {.str = "/usr/local/lib"}, 0, 0, E },
    { "packing_plugin_name", "OMAF Packing plugin name", OFFSET(packingPluginName), AV_OPT_TYPE_STRING, {.str = "HighResPlusFullLowResPacking"}, 0, 0, E },
    { "video_plugin_path", "Video stream process plugin path", OFFSET(videoPluginPath), AV_OPT_TYPE_STRING, {.str = "/usr/local/lib"}, 0, 0, E },
    { "video_plugin_name", "Video stream process plugin name", OFFSET(videoPluginName), AV_OPT_TYPE_STRING, {.str = "HevcVideoStreamProcess"}, 0, 0, E },
    { "audio_plugin_path", "Audio stream process plugin path", OFFSET(audioPluginPath), AV_OPT_TYPE_STRING, {.str = "NULL"}, 0, 0, E },
    { "audio_plugin_name", "Audio stream process plugin name", OFFSET(audioPluginName), AV_OPT_TYPE_STRING, {.str = "NULL"}, 0, 0, E },
    { "fixed_extractors_res", "whether extractor track needs the fixed resolution", OFFSET(fixedPackedPicRes), AV_OPT_TYPE_BOOL, { .i64 = 0 }, 0, 1, E },
    { "cmaf_enabled", "whether to enable CMAF segments", OFFSET(cmafEnabled), AV_OPT_TYPE_BOOL, { .i64 = 0 }, 0, 1, E },
    { "chunk_info_type", "which chunk info type (sidx_only/cloc_only/sidx_and_cloc) is enabled", OFFSET(chunkInfoType), AV_OPT_TYPE_STRING, { .str = "none" }, 0, 0, E },
    { "chunk_dur", "CMAF chunk duration in millisecond", OFFSET(chunkDur), AV_OPT_TYPE_INT, { .i64 = 0 }, 0, INT_MAX, E },
    { "seg_writer_path", "Segment writer plugin path", OFFSET(segWriterPluginPath), AV_OPT_TYPE_STRING, {.str = "/usr/local/lib"}, 0, 0, E },
    { "seg_writer_name", "Segment writer plugin name", OFFSET(segWriterPluginName), AV_OPT_TYPE_STRING, {.str = "SegmentWriter"}, 0, 0, E },
    { "mpd_writer_path", "MPD file writer plugin path", OFFSET(mpdWriterPluginPath), AV_OPT_TYPE_STRING, {.str = "/usr/local/lib"}, 0, 0, E },
    { "mpd_writer_name", "MPD file writer plugin name", OFFSET(mpdWriterPluginName), AV_OPT_TYPE_STRING, {.str = "MPDWriter"}, 0, 0, E },
    { "target_latency", "Target end to end latency in live streaming in millisecond", OFFSET(target_latency), AV_OPT_TYPE_INT, { .i64 = 3500 }, 0, INT_MAX, E },
    { "min_latency", "Minimum end to end latency in live streaming in millisecond", OFFSET(min_latency), AV_OPT_TYPE_INT, { .i64 = 2000 }, 0, INT_MAX, E },
    { "max_latency", "Maximum end to end latency in live streaming in millisecond", OFFSET(max_latency), AV_OPT_TYPE_INT, { .i64 = 10000 }, 0, INT_MAX, E },
    { "need_external_log", "whether external log callback is needed", OFFSET(need_external_log), AV_OPT_TYPE_BOOL, { .i64 = 0 }, 0, 1, E },
    { "min_log_level", "Minimal log level of output [0: INFO, 1: WARNING, 2: ERROR, 3: FATAL]", OFFSET(min_log_level), AV_OPT_TYPE_INT, { .i64 = 2 }, 0, 3, E },
    { NULL },
};

static const AVClass omaf_class = {
    .class_name = "OMAF Compliance muxer",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

AVOutputFormat ff_omaf_packing_muxer = {
    .name           = "omaf_packing",
    .long_name      = "VR OMAF Compliance Muxer",
    .extensions     = "mpd",
    .priv_data_size = sizeof(OMAFContext),
    .audio_codec    = AV_CODEC_ID_AAC,
    .video_codec    = AV_CODEC_ID_HEVC,
    .flags          = AVFMT_GLOBALHEADER | AVFMT_NOFILE | AVFMT_TS_NEGATIVE,
    .init           = omaf_init,
    .write_header   = omaf_write_header,
    .write_packet   = omaf_write_packet,
    .write_trailer  = omaf_write_trailer,
    .deinit         = omaf_free,
    .priv_class     = &omaf_class,
};


