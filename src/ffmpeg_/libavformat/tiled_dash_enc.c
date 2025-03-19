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

#include "tiled_dash_parse.h"

static int dash_init(AVFormatContext *s)
{
    GPAC_DASHContext *c = s->priv_data;
    int ret = 0, i;
    char *ptr;
    char basename[1024];
    int mode;
    int mode_u, mode_g, mode_o;

    av_strlcpy(c->dirname, s->url, sizeof(c->dirname));
    ptr = strrchr(c->dirname, '/');
    if (ptr) {
        av_strlcpy(basename, &ptr[1], sizeof(basename));
        ptr[1] = '\0';
    } else {
        c->dirname[0] = '\0';
        av_strlcpy(basename, s->url, sizeof(basename));
    }

    //av_strlcpy(c->base_url, "", sizeof(c->base_url));
    ptr = strrchr(basename, '.');
    if (ptr)
        *ptr = '\0';

    c->streams = av_mallocz(sizeof(*c->streams) * s->nb_streams);
    if (!c->streams)
        return AVERROR(ENOMEM);

    c->nb_streams = s->nb_streams;
    for (i = 0; i < s->nb_streams; i++) {
        DashOutStream *os = &c->streams[i];
        AVStream *st = s->streams[i];
        os->stream_index = i + 1;
        os->bit_rate = s->streams[i]->codecpar->bit_rate;
        os->frame_rate = (double)st->avg_frame_rate.num / (double)st->avg_frame_rate.den;
        if (!os->bit_rate) {
            int level = s->strict_std_compliance >= FF_COMPLIANCE_STRICT ?
                        AV_LOG_ERROR : AV_LOG_WARNING;
            av_log(s, level, "No bit rate set for stream %d\n", i);
            if (s->strict_std_compliance >= FF_COMPLIANCE_STRICT)
                return AVERROR(EINVAL);
        }

        //ff_dash_fill_tmpl_params(os->initfile, sizeof(os->initfile), c->init_seg_name, i, 0, os->bit_rate, 0);
        snprintf(os->out_name, sizeof(os->out_name), "%s%d", c->out_name, os->stream_index);
        snprintf(os->dir_name, sizeof(os->dir_name), "%s", c->dirname);

        mode_u = 7;
        mode_g = 7;
        mode_o = 7;
        mode = mode_u * 64 + mode_g * 8 + mode_o;
        if (access(&(os->dir_name[0]), 0) == 0)
        {
            av_log(s, AV_LOG_DEBUG, "Folder %s has existed\n", os->dir_name);
            if (access(&(os->dir_name[0]), mode) != 0)
            {
                if (chmod(&(os->dir_name[0]), mode) != 0)
                {
                    av_log(s, AV_LOG_ERROR, "Failed to change write mode for folder %s\n", os->dir_name);
                    return AVERROR(EINVAL);
                }
            }
        }
        else
        {
            av_log(s, AV_LOG_DEBUG, "Create folder %s\n", os->dir_name);
            if (mkdir(&(os->dir_name[0]), mode) != 0)
            {
                av_log(s, AV_LOG_ERROR, "Failed to create folder %s\n", os->dir_name);
                return AVERROR(EINVAL);
            }
        }

        //snprintf(os->stream_name, sizeof(os->stream_name), "%s%s", c->dirname, c->out_name);

        av_log(s, AV_LOG_VERBOSE, "stream %d output filename prefix: %s%s\n", i, os->dir_name, os->out_name);

        os->initialized = 0;
        //os->packets_written = 0;
        //os->total_pkt_size = 0;
        os->vstream_idx = i;
        os->fmt_ctx = s;
        os->codec_ctx = s->streams[i]->codecpar;
        os->bit_rate = s->streams[i]->codecpar->bit_rate;
        os->split_tile = c->split_tile;
        os->timescale = st->time_base;

        if(NULL != s->streams[i]->codecpar->extradata){
            ret = dash_probe_extra_data(os, s->streams[i]->codecpar->extradata, 
                                           s->streams[i]->codecpar->extradata_size);
            if(ret != 0){
                av_freep(&c->streams);
                return ret;
            }
                
            ret = dash_init_output_stream(c, os);
                
            if(!ret) os->initialized = 1;
        }
        
        switch(st->codecpar->codec_type) {
        case AVMEDIA_TYPE_VIDEO:
            c->has_video = 1;
            break;
        case AVMEDIA_TYPE_AUDIO:
            c->has_audio = 1;
            break;
        default:
            break;
        }
    }

    if (!c->has_video && c->seg_duration <= 0) {
        av_log(s, AV_LOG_WARNING, "no video stream and no seg duration set\n");
        return AVERROR(EINVAL);
    }
    return 0;
}

static void dash_free(AVFormatContext *s)
{
    GPAC_DASHContext *c = s->priv_data;
    int i;

    if (!c->streams)
        return;
    for (i = 0; i < s->nb_streams; i++) {
        DashOutStream *os = &c->streams[i];
        dash_free_output_stream(os);
    }
    av_freep(&c->streams);
}

static int dash_write_header(AVFormatContext *s)
{
    //GPAC_DASHContext *c = s->priv_data;
    //int i, ret = 0;
    //for (i = 0; i < s->nb_streams; i++) {
    //    DashOutStream *os = &c->streams[i];
        //TOBE: write header for each out stream. 
    //}
    return 0;//ret;
}

static int dash_write_packet(AVFormatContext *s, AVPacket *pkt)
{
    GPAC_DASHContext *c = s->priv_data;
    AVStream *st = s->streams[pkt->stream_index];
    DashOutStream *os = &c->streams[pkt->stream_index];
    //int64_t seg_end_duration, elapsed_duration;
    int ret = 0;

    if(!os->initialized){
        //if(!s->streams[pkt->stream_index]->codecpar->extradata) return 0;
        
        ret = dash_probe_extra_data(os, pkt->data, pkt->size);
        //ret = dash_probe_extra_data(os, s->streams[pkt->stream_index]->codecpar->extradata, 
        //                       s->streams[pkt->stream_index]->codecpar->extradata_size);
        
        if(ret != 0) return ret;
        
        ret = dash_init_output_stream(c, os);
        if(ret != 0){
            av_log(s, AV_LOG_ERROR, "Output stream initialized failed: %d\n", pkt->stream_index );
            return ret;
        }
        os->initialized = 1;
    }
    
    ///write mpd in streaming mode
    if( c->streaming ){
        ret = dash_update_mpd(c, 0);
        if(ret != 0){
            av_log(s, AV_LOG_ERROR, "Failed to update mpd in streaming mode\n" );
            return ret;
        }
    }

    // Fill in a heuristic guess of the packet duration, if none is available.
    // The mp4 muxer will do something similar (for the last packet in a fragment)
    // if nothing is set (setting it for the other packets doesn't hurt).
    // By setting a nonzero duration here, we can be sure that the mp4 muxer won't
    // invoke its heuristic (this doesn't have to be identical to that algorithm),
    // so that we know the exact timestamps of fragments.
    if (!pkt->duration && os->last_dts != AV_NOPTS_VALUE)
        pkt->duration = pkt->dts - os->last_dts;
    os->last_dts = pkt->dts;

    // If forcing the stream to start at 0, the mp4 muxer will set the start
    // timestamps to 0. Do the same here, to avoid mismatches in duration/timestamps.
    if (os->first_pts == AV_NOPTS_VALUE &&
        s->avoid_negative_ts == AVFMT_AVOID_NEG_TS_MAKE_ZERO) {
        pkt->pts -= pkt->dts;
        pkt->dts  = 0;
    }

    if (os->first_pts == AV_NOPTS_VALUE)
        os->first_pts = pkt->pts;

    if (!c->availability_start_time[0])
        format_date_now(c->availability_start_time,
                        sizeof(c->availability_start_time));
    
    if (!os->availability_time_offset && pkt->duration) {
        int64_t frame_duration = av_rescale_q(pkt->duration, st->time_base,
                                              AV_TIME_BASE_Q);
         os->availability_time_offset = ((double) c->seg_duration -
                                         frame_duration) / AV_TIME_BASE;
    }

    ret = dash_write_segment(c, os, pkt );
    if(ret){
        av_log(s, AV_LOG_ERROR, "Failed to write segment for stream %d\n", pkt->stream_index );
        return ret;
    }

    return ret;
}

static int dash_write_trailer(AVFormatContext *s)
{
    GPAC_DASHContext *c = s->priv_data;
    int ret = 0;
    DashOutStream *os = NULL;
    
    for (int i = 0; i < s->nb_streams; i++) {
        os = &c->streams[i];
        os->total_frames = os->nb_frames;
        av_log(s, AV_LOG_DEBUG, "Total_frames %d \n", os->total_frames);
        if (os->total_frames % os->frame_per_fragment != 0)
        {
            dash_end_output_stream(os);
        }
    }

    if (c->streaming)
    {
        ret = dash_update_mpd(c, 1);
        if(ret ){
            av_log(s, AV_LOG_ERROR, "Failed to write mpd \n" );
            return ret;
        }
    }
    else
    {
        dash_write_mpd(c, 1);
    } 
    /*
    if (s->nb_streams > 0) {
        DashOutStream *os = &c->streams[0];
        // If no segments have been written so far, try to do a crude
        // guess of the segment duration
        if (!c->last_duration)
            c->last_duration = av_rescale_q(os->last_pts - os->start_pts,
                                            s->streams[0]->time_base,
                                            AV_TIME_BASE_Q);
        c->total_duration = av_rescale_q(os->last_pts - os->first_pts,
                                         s->streams[0]->time_base,
                                         AV_TIME_BASE_Q);
    }
    //dash_flush(s, 1, -1);
    */
    return 0;
}

/*
static int dash_check_bitstream(struct AVFormatContext *s, const AVPacket *avpkt)
{
    GPAC_DASHContext *c = s->priv_data;
    DashOutStream *os = &c->streams[avpkt->stream_index];
    AVFormatContext *oc = s->ctx;
    if (oc->oformat->check_bitstream) {
        int ret;
        AVPacket pkt = *avpkt;
        pkt.stream_index = 0;
        ret = oc->oformat->check_bitstream(oc, &pkt);
        if (ret == 1) {
            AVStream *st = s->streams[avpkt->stream_index];
            AVStream *ost = oc->streams[0];
            //st->internal->bsfcs = ost->internal->bsfcs;
            //st->internal->nb_bsfcs = ost->internal->nb_bsfcs;
            //ost->internal->bsfcs = NULL;
            //ost->internal->nb_bsfcs = 0;
        }
        return ret;
    }
    return 1;
}
*/
#define OFFSET(x) offsetof(GPAC_DASHContext, x)
#define E AV_OPT_FLAG_ENCODING_PARAM
static const AVOption options[] = {
    { "adaptation_sets", "Adaptation sets. Syntax: id=0,streams=0,1,2 id=1,streams=3,4 and so on", OFFSET(adaptation_sets), AV_OPT_TYPE_STRING, { 0 }, 0, 0, AV_OPT_FLAG_ENCODING_PARAM },
    { "window_size", "number of segments kept in the manifest", OFFSET(window_size), AV_OPT_TYPE_INT, { .i64 = 0 }, 0, INT_MAX, E },
    { "extra_window_size", "number of segments kept outside of the manifest before removing from disk", OFFSET(extra_window_size), AV_OPT_TYPE_INT, { .i64 = 5 }, 0, INT_MAX, E },
    { "split_tile", "need split the stream to tiles if input is tile-based hevc stream", OFFSET(split_tile), AV_OPT_TYPE_INT, { .i64 = 0 }, 0, 2, E },
    { "seg_duration", "segment duration (in seconds, fractional value can be set)", OFFSET(seg_duration), AV_OPT_TYPE_DURATION, { .i64 = 5000000 }, 0, INT_MAX, E },
    { "remove_at_exit", "remove all segments when finished", OFFSET(remove_at_exit), AV_OPT_TYPE_BOOL, { .i64 = 0 }, 0, 1, E },
    { "use_template", "Use SegmentTemplate instead of SegmentList", OFFSET(use_template), AV_OPT_TYPE_BOOL, { .i64 = 1 }, 0, 1, E },
    { "use_timeline", "Use SegmentTimeline in SegmentTemplate", OFFSET(use_timeline), AV_OPT_TYPE_BOOL, { .i64 = 1 }, 0, 1, E },
    { "utc_timing_url", "URL of the page that will return the UTC timestamp in ISO format", OFFSET(utc_timing_url), AV_OPT_TYPE_STRING, { 0 }, 0, 0, E },
    { "streaming", "Enable/Disable streaming mode of output. Each frame will be moof fragment", OFFSET(streaming), AV_OPT_TYPE_BOOL, { .i64 = 0 }, 0, 1, E },
    { "base_url", "MPD BaseURL", OFFSET(base_url), AV_OPT_TYPE_STRING, { 0 }, 0, 0, E },
    { "out_name", "name prefix for all dash output files", OFFSET(out_name), AV_OPT_TYPE_STRING, {.str = "dash-stream"}, 0, 0, E },
    { NULL },
};

static const AVClass dash_class = {
    .class_name = "libgpac dash muxer",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

AVOutputFormat ff_tile_dash_muxer = {
    .name           = "tile_dash",
    .long_name      = "libgpac-based tiled DASH Muxer",
    .extensions     = "mpd",
    .priv_data_size = sizeof(GPAC_DASHContext),
    .audio_codec    = AV_CODEC_ID_AAC,
    .video_codec    = AV_CODEC_ID_HEVC,
    .flags          = AVFMT_GLOBALHEADER | AVFMT_NOFILE | AVFMT_TS_NEGATIVE,
    .init           = dash_init,
    .write_header   = dash_write_header,
    .write_packet   = dash_write_packet,
    .write_trailer  = dash_write_trailer,
    .deinit         = dash_free,
 //   .check_bitstream = dash_check_bitstream,
    .priv_class     = &dash_class,
};


