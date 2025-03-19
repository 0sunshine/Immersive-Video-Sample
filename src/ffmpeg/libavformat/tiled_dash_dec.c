/*
 * Intel tile Dash Demuxer
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
#include "libavutil/time.h"

#include "libavutil/avassert.h"
#include "libavutil/avutil.h"
#include "libavutil/avstring.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/mathematics.h"
#include "libavutil/opt.h"
#include "libavutil/rational.h"
#include "libavutil/time_internal.h"

#include "libavcodec/avcodec.h"
#include "avformat.h"
#include "internal.h"
#include "avio_internal.h"
#include "tiled_dash_dec.h"

uint64_t frameCnt = 0;

int tiled_dash_ViewPort_update(AVFormatContext *s, bool isVertical, double move)
{
    int ret = 0;
    TiledDASHDecContext *c = s->priv_data;
    if(isVertical)
    {
        double pitch = move + c->lastPose.pitch;
        if(pitch > 90)
        {
            pitch = 90;
        }
        else if(pitch < -90)
        {
            pitch = -90;
        }

        c->pose.pitch = pitch;
    }
    else
    {
        double yaw = move + c->lastPose.yaw;
        if(yaw > 180)
        {
            yaw -= 360;
        }
        else if(yaw < -180)
        {
            yaw += 360;
        }
        c->pose.yaw = yaw;
    }

    c->lastPose.yaw = c->pose.yaw;
    c->lastPose.pitch = c->pose.pitch;

    ret = OmafAccess_ChangeViewport(c->hdl, &(c->pose));
    return ret;
}

static int tiled_dash_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    int ret = 0;
    TiledDASHDecContext *c = s->priv_data;
    frameCnt++;
    // TODO: read packet for one stream once??
    for (int streamId = 0; streamId < c->mInfo.stream_count; streamId++)
    {
    //int streamId = 0;
    //frameCnt++;

        DashStreamInfo stInfo = c->mInfo.stream_info[streamId];

        DashPacket dashPkt[5];
        memset(dashPkt, 0, 5 * sizeof(DashPacket));
        int dashPktNum = 0;

        if (stInfo.stream_type == MediaType_Video)
        {
            ret = OmafAccess_GetPacket(c->hdl, streamId, &(dashPkt[0]), &dashPktNum, &(pkt->pts), c->needHeaders, c->mClearBuf);
            if(ret != ERROR_NONE){
                //av_log(s, AV_LOG_ERROR, "OmafAccess_GetPacket get null packet\    n" );
                //av_packet_unref(pkt);
            }

            if ((frameCnt % 50) == 0)
            {
                HeadPose newPose;
                newPose.yaw = 45;
                newPose.pitch = 90;
                OmafAccess_ChangeViewport(c->hdl, &newPose);
            }
            else if ((frameCnt % 75) == 0)
            {
                OmafAccess_ChangeViewport(c->hdl, &(c->pose));
            }

            if(dashPktNum && dashPkt[0].buf && dashPkt[0].size)
            {
                int size = dashPkt[0].size;
                if (av_new_packet(pkt, size) < 0)
                    return AVERROR(ENOMEM);

                memcpy(pkt->data, dashPkt[0].buf, size);
                pkt->size = size;

                free(dashPkt[0].buf);
                dashPkt[0].buf = NULL;
                if (dashPkt[0].rwpk != NULL)
                {
                    if (dashPkt[0].rwpk->rectRegionPacking != NULL)
                    {
                        free(dashPkt[0].rwpk->rectRegionPacking);
                        dashPkt[0].rwpk->rectRegionPacking = NULL;
                    }
                    free(dashPkt[0].rwpk);
                    dashPkt[0].rwpk = NULL;
                }
                if (dashPkt[0].qtyResolution)
                {
                    free(dashPkt[0].qtyResolution);
                    dashPkt[0].qtyResolution = NULL;
                }
                if(c->needHeaders){c->needHeaders = false;}
            }

            for (int pktIdx = 1; pktIdx < dashPktNum; pktIdx++)
            {
                if (dashPkt[pktIdx].buf && dashPkt[pktIdx].size)
                {
                    free(dashPkt[pktIdx].buf);
                    dashPkt[pktIdx].buf = NULL;

                    if (dashPkt[pktIdx].rwpk != NULL)
                    {
                        if (dashPkt[pktIdx].rwpk->rectRegionPacking != NULL)
                        {
                            free(dashPkt[pktIdx].rwpk->rectRegionPacking);
                            dashPkt[pktIdx].rwpk->rectRegionPacking = NULL;
                        }
                        free(dashPkt[pktIdx].rwpk);
                        dashPkt[pktIdx].rwpk = NULL;
                    }
                    if (dashPkt[pktIdx].qtyResolution)
                    {
                        free(dashPkt[pktIdx].qtyResolution);
                        dashPkt[pktIdx].qtyResolution = NULL;
                    }
                }
            }
        }
        else if (stInfo.stream_type == MediaType_Audio)
        {
            uint64_t audio_pts = 0;
            ret = OmafAccess_GetPacket(c->hdl, streamId, &(dashPkt[0]), &dashPktNum, &(audio_pts), c->needHeaders, c->mClearBuf);
            if(ret == ERROR_NULL_PACKET)
            {
                av_log(s, AV_LOG_INFO, "OmafAccess_GetPacket get null packet\n" );
            }

            if(dashPktNum && dashPkt[0].buf && dashPkt[0].size)
            {
                FILE *audioFP = fopen("dumpedAAC.aac", "ab+");
                if (!audioFP)
                {
                    av_log(s, AV_LOG_ERROR, "Failed to open dumpedAAC.m4a !\n" );
                }
                fwrite(dashPkt[0].buf, 1, dashPkt[0].size, audioFP);
                fclose(audioFP);
                audioFP = NULL;
            }
        }
    }
    return ret;
}

static int SetupHeadSetInfo(HeadSetInfo *clientInfo)
{
    if(!clientInfo)
    {
        return -1;
    }

    clientInfo->pose = (HeadPose*)malloc(sizeof(HeadPose));
    clientInfo->pose->yaw = -90;
    clientInfo->pose->pitch = 0;
    clientInfo->viewPort_hFOV = 80;
    clientInfo->viewPort_vFOV = 80;
    clientInfo->viewPort_Width = 960;
    clientInfo->viewPort_Height = 960;

    return 0;
}

static int tiled_dash_read_header(AVFormatContext *s)
{
    int ret = 0;
    TiledDASHDecContext *c = s->priv_data;
    AVIOContext *pb = s->pb;

    c->mClearBuf = false;
    c->needHeaders = true;
    c->client = (DashStreamingClient *)malloc(sizeof(DashStreamingClient));
    memset(c->client, 0, sizeof(DashStreamingClient));
    memset(&c->client->omaf_params.proxy, 0, sizeof(OmafHttpProxy));
    memset(&c->client->omaf_params.predictor_params, 0, sizeof(OmafPredictorParams));
    c->client->omaf_params.max_decode_width = 2560;
    c->client->omaf_params.max_decode_height = 2560;
    c->client->source_type = MultiResSource;//DefaultSource;
    c->client->media_url = s->filename;
    c->client->enable_extractor = c->enable_extractor;
    if(!c->cache_path)
    {
        c->client->cache_path = "./cache";
    }
    else
    {
        char* cache_folder = "/cache";
        c->client->cache_path = malloc(strlen(c->cache_path) + strlen(cache_folder));
        strcpy(c->client->cache_path, c->cache_path);
        strcpy(c->client->cache_path + strlen(c->cache_path), cache_folder);
    }

    c->hdl = OmafAccess_Init(c->client);

    ret = SetupHeadSetInfo(&(c->HSInfo));
    ret = OmafAccess_SetupHeadSetInfo(c->hdl, &(c->HSInfo));

    ret = OmafAccess_OpenMedia(c->hdl, c->client, false, "", "");
    ret = OmafAccess_StartStreaming(c->hdl);

    c->lastPose.yaw = c->HSInfo.pose->yaw;
    c->lastPose.pitch = c->HSInfo.pose->pitch;

    ret = OmafAccess_GetMediaInfo(c->hdl, &(c->mInfo));
    printf("Media streams cnt is %d\n", c->mInfo.stream_count);
    bool hasAudio = false;
    for(int i = 0 ; i < c->mInfo.stream_count ; i++)
    {
        DashStreamInfo stInfo = c->mInfo.stream_info[i];
        if (stInfo.stream_type == MediaType_Audio)
        {
            hasAudio = true;
            break;
        }
    }

    for(int i = 0 ; i < c->mInfo.stream_count ; i++)
    {
        AVStream *st = avformat_new_stream(s, NULL);
        if (!st) {
            return AVERROR(ENOMEM);
        }
        DashStreamInfo stInfo = c->mInfo.stream_info[i];

        st->id = i;
        //codec_parameters_reset(st->codecpar);

        if(stInfo.stream_type == MediaType_Video)
        {
            st->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
            if (hasAudio)
            {
                c->n_videos = c->mInfo.stream_count - 1;
            }
            else
            {
                c->n_videos = c->mInfo.stream_count;
            }
            st->codecpar->codec_id = AV_CODEC_ID_HEVC;
        }
        else if(stInfo.stream_type == MediaType_Audio)
        {
            st->codecpar->codec_type = AVMEDIA_TYPE_AUDIO;
            c->n_audios = 1;
            st->codecpar->codec_id = AV_CODEC_ID_AAC;
        }

        //st->codecpar->codec_id = AV_CODEC_ID_HEVC;

        st->codecpar->width = stInfo.width;
        st->codecpar->height = stInfo.height;
        st->codecpar->bit_rate = stInfo.bit_rate;
        st->codecpar->codec_tag = avio_rl32(pb);

        //st->codecpar->extradata = ;
        //st->codecpar->extradata_size = ;
        st->codecpar->bits_per_coded_sample = avio_rb16(pb);

        //st->need_parsing = AVSTREAM_PARSE_FULL_RAW;

        st->internal->avctx->framerate.num = stInfo.framerate_num;
        st->internal->avctx->framerate.den = stInfo.framerate_den;
        //st->internal->avctx->framerate = stInfo.framerate_num / stInfo.framerate_den;
        //st->time_base.num = stInfo.framerate_num;
        //st->time_base.den = stInfo.framerate_den;
        avpriv_set_pts_info(st, st->pts_wrap_bits, st->time_base.num, st->time_base.den);
    }

    av_usleep(5000000);

    return ret;
}

static int tiled_dash_close(AVFormatContext *s)
{
    TiledDASHDecContext *c = s->priv_data;

    OmafAccess_CloseMedia(c->hdl);

    OmafAccess_Close(c->hdl);

    if(c->HSInfo.pose)
    {
        free(c->HSInfo.pose);
        c->HSInfo.pose = NULL;
    }

    if(c->client)
    {
        free(c->client);
        c->client = NULL;
    }

    return 0;
}

static int tiled_dash_read_seek(AVFormatContext *s, int stream_index, int64_t timestamp, int flags)
{
    TiledDASHDecContext *c = s->priv_data;

    OmafAccess_SeekMedia(c->hdl, time);

    return 0;
}

static int tiled_dash_probe(AVProbeData *p)
{
    if (!av_stristr(p->buf, "<MPD"))
        return 0;

    if (av_stristr(p->buf, "urn:mpeg:dash:schema:mpd:2011") ||
        av_stristr(p->buf, "urn:mpeg:dash:srd:2014") ){
        return AVPROBE_SCORE_MAX;
    }
    if (av_stristr(p->buf, "urn:mpeg:dash:profile:isoff-live:2011")) {
        return AVPROBE_SCORE_MAX;
    }

    return 0;
}

#define OFFSET(x) offsetof(TiledDASHDecContext, x)
#define FLAGS AV_OPT_FLAG_DECODING_PARAM
static const AVOption dash_options[] = {
    {"allowed_extensions", "List of file extensions that dash is allowed to access",
        OFFSET(allowed_extensions), AV_OPT_TYPE_STRING,
        {.str = "mpd"},
        INT_MIN, INT_MAX, FLAGS},
    {"cache_path", "the specific path of cache folder, default is /home",
        OFFSET(cache_path), AV_OPT_TYPE_STRING,
        {.str = NULL},
        INT_MIN, INT_MAX, FLAGS},
    {"enable_extractor", "whether to enable extractor track in OMAF Dash Access engine",
        OFFSET(enable_extractor), AV_OPT_TYPE_INT,
        {.i64 = 1},
        INT_MIN, INT_MAX, FLAGS},
    {NULL}
};

static const AVClass tiled_dash_dec_class = {
    .class_name = "Tiled Dash Demuxer",
    .item_name  = av_default_item_name,
    .option     = dash_options,
    .version    = LIBAVUTIL_VERSION_INT,
};

AVInputFormat ff_tile_dash_demuxer = {
    .name           = "tiled_dash_demuxer",
    .long_name      = "Demuxer for Dynamic Adaptive Streaming over HTTP",
    .priv_class     = &tiled_dash_dec_class,
    .priv_data_size = sizeof(TiledDASHDecContext),
    .read_probe     = tiled_dash_probe,
    .read_header    = tiled_dash_read_header,
    .read_packet    = tiled_dash_read_packet,
    .read_close     = tiled_dash_close,
    .read_seek      = tiled_dash_read_seek,
    .flags          = AVFMT_NO_BYTE_SEEK,
};
