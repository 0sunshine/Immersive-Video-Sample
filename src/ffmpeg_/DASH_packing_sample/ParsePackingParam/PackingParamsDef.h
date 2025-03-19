#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

#ifndef DATA_DEF_H
#define DATA_DEF_H

typedef struct _PACKING_IN_PARAM {
    char     proj_type[1024];
    char     face_file[1024];
    char     viewport_w[1024];
    char     viewport_h[1024];
    char     viewport_yaw[1024];
    char     viewport_pitch[1024];
    char     viewport_fov_hor[1024];
    char     viewport_fov_ver[1024];
    char     window_size[1024];
    char     extra_window_size[1024];
    char     split_tile[1024];
    char     seg_duration[1024];
    char     is_live[1024];
    int32_t  live_mode;
    char     base_url[1024];
    char     out_name[1024];
    char     need_buffered_frames[1024];
    char     extractors_per_thread[1024];
    char     has_extractor[1024];
    char     packingPluginPath[1024];
    char     packingPluginName[1024];
    char     videoPluginPath[1024];
    char     videoPluginName[1024];
    char     audioPluginPath[1024];
    char     audioPluginName[1024];
    char     fixedPackedPicRes[1024];
    char     cmafEnabled[1024];
    char     chunkDur[1024];
    char     segWriterPluginPath[1024];
    char     segWriterPluginName[1024];
    char     mpdWriterPluginPath[1024];
    char     mpdWriterPluginName[1024];
    char     target_latency[1024];
    char     min_latency[1024];
    char     max_latency[1024];
    char     need_external_log[1024];
    char     min_log_level[1024];
} PACKING_IN_PARAM;

typedef struct _VIDEO_IN_PARAM {
    const char     *video_file_name;
    int32_t        width;
    int32_t        height;
    uint64_t       bit_rate;
    uint16_t       frame_rate;
    uint32_t       gop_size;
    const char     *frame_len_name;
} VIDEO_IN_PARAM;

typedef struct _DASH_IN_PARAM {
    PACKING_IN_PARAM packing_param;
    uint32_t         in_videos_num;
    VIDEO_IN_PARAM   videos_param[1024];
    uint64_t         packing_frames_num;
} DASH_IN_PARAM;

#endif
