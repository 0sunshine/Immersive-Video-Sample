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

#ifndef TILE_DASH_PARSE_H
#define TILE_DASH_PARSE_H

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

#include "gpac/constants.h"
#include "gpac/internal/mpd.h"
#include "gpac/media_tools.h"
#include "gpac/isomedia.h"
//#include "gpac/isom_tools.h"
#include "gpac/internal/media_dev.h"

#define MAX_TILE 1024

typedef struct
{
	u32 tx, ty, tw, th;
	u32 data_offset;
	u32 nb_nalus_in_sample;
	Bool all_intra;
} HEVCTileImport;

typedef struct {

#ifndef GPAC_DISABLE_ISOM
	GF_ISOFile        *isof;
	GF_ISOSample      *sample;
#endif
	u32               iso_track_ID;
        u32               iso_track;
        int               dash_track_ID;
	/* Variables that encoder needs to encode data */
	uint8_t           *vbuf;
	int               vbuf_size;
        int               encoded_frame_size;
        int64_t           cur_pts;
        int               cur_keyframe;
	u32               seg_marker;
	int               gdr;
        u32               nb_segments;
        int               segment_index;
        int64_t           frame_dur;
	char              rep_id[64];
        int               dependency_id;
        HEVCTileImport    tile;
        int               bit_rate;
        char              seg_init_name[1024];
        char              seg_media_name[1024];
        char              seg_media_name_tmpl[1024];
        int               timescale;
        int               use_source_timing;
        int               sample_count;
        int               iso_created;
} VideoOutput;

typedef struct DashOutStream {
    AVFormatContext       *fmt_ctx;
    AVCodecParameters     *codec_ctx;
    HEVCState             hevc_state;
    GF_HEVCConfig         hevc_cfg;
    GF_AVCConfig          avc_cfg;
    int                   initialized;
    int                   stream_index;
    //int                   packets_written;
    //int                   total_pkt_size;
    int                   total_frames;
    int                   nb_frames;
    int                   bit_rate;
    VideoOutput           *video_out[MAX_TILE];
    int                   nb_tiles;
    int                   max_width;
    int                   max_height;
    double                frame_rate;
    
    int64_t               last_pts;
    int64_t               last_dts;
    int64_t               first_pts;
    int64_t               start_pts;
    int64_t               first_dts_in_fragment;
    double                availability_time_offset;
    int                   frame_per_fragment;
    int                   frame_per_segment;
    Bool                  fragment_started;
    Bool                  segment_started;
    int                   seg_dur;
    int                   frag_dur;
    int                   minimum_update_period;
    int64_t               frame_dur;
    AVRational            timescale;
    int                   vstream_idx;
    int                   split_tile;
    char                  out_name[256];
    char                  dir_name[1024];
    char                  available_start_time[1024];

} DashOutStream;

typedef struct {
    const AVClass  *class;  /* Class for private options. */
    char           *adaptation_sets;
    int            nb_as;
    int            window_size;
    int            extra_window_size;
    int64_t        seg_duration;
    int            remove_at_exit;
    int            use_template;
    int            use_timeline;
    DashOutStream  *streams;
    int            nb_streams;
    int64_t        last_duration;
    int64_t        total_duration;
    char           availability_start_time[100];
    char           dirname[1024];
    const char     *out_name;
    const char     *base_url;
    const char     *utc_timing_url;
    int            master_playlist_created;
    AVIOContext    *mpd_out;
    int            streaming;
    int            index_correction;
    int            split_tile;
    int            has_video;
    int            has_audio;
} GPAC_DASHContext;


void format_date_now(char *buf, int size);
int dash_init_output_stream(GPAC_DASHContext* ctx, DashOutStream* os);
void dash_end_output_stream(DashOutStream* os);
int dash_probe_extra_data(DashOutStream* os, char* buf, int size);
int dash_update_mpd(GPAC_DASHContext* dash_ctx, int is_final);
int dash_write_segment( GPAC_DASHContext* ctx, DashOutStream* os, AVPacket *pkt );
void dash_free_output_stream(DashOutStream* os);
void dash_write_mpd(GPAC_DASHContext *ctx, int is_final);
#endif

