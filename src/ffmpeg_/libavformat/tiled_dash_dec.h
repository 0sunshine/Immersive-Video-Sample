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

#ifndef TILE_DASH_DEC_H
#define TILE_DASH_DEC_H

#include <stdbool.h>

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

#include "OmafDashAccessApi.h"

typedef struct{
    const AVClass *class;
    char allowed_extensions[256];
    char* cache_path;

    int n_videos;

    int n_audios;

    DashStreamingClient *client;
    DashMediaInfo       mInfo;
    Handler             hdl;
    HeadSetInfo         HSInfo;
    HeadPose            pose;
    HeadPose            lastPose;
    bool                mClearBuf;
    bool                needHeaders;
    int                 enable_extractor;
}TiledDASHDecContext;

int tiled_dash_ViewPort_update(AVFormatContext *s, bool isVertical, double move);

#endif

