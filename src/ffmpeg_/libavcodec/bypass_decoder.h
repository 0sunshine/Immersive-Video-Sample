/*
 * Raw Video Decoder
 * Copyright (c) 2001 Fabrice Bellard
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
 * Raw Video Decoder
 */

#include "avcodec.h"
#include "bswapdsp.h"
#include "get_bits.h"
#include "raw.h"
#include "internal.h"
#include "libavutil/avassert.h"
#include "libavutil/buffer.h"
#include "libavutil/common.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/imgutils.h"
#include "libavutil/opt.h"

typedef struct ByPassVideoContext {
    int frame_size;  /* size of the frame in bytes */
    int flip;
    int is_1_2_4_8_bpp; // 1, 2, 4 and 8 bpp in avi/mov, 1 and 8 bpp in nut
    int is_mono;
    int is_pal8;
    int is_nut_mono;
    int is_nut_pal8;
    int is_yuv2;
    int is_lt_16bpp; // 16bpp pixfmt and bits_per_coded_sample < 16
    int tff;
    // user options
    int frameWidth;
    int frameHeight;
} ByPassVideoContext;

#define OFFSET(x) offsetof(ByPassVideoContext, x)
#define VE (AV_OPT_FLAG_DECODING_PARAM | AV_OPT_FLAG_VIDEO_PARAM)
static const AVOption options[] = {
    {"W", "input bitstream width", OFFSET(frameWidth), AV_OPT_TYPE_INT, {.i64 = 0}, 0, INT_MAX, VE},
    {"H", "input bitstream height", OFFSET(frameHeight), AV_OPT_TYPE_INT, {.i64 = 0}, 0, INT_MAX, VE},
    {NULL}
};


