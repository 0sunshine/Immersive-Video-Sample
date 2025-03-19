/*
 * Copyright (c) 2022, Intel Corporation
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * * Redistributions of source code must retain the above copyright notice, this
 *   list of conditions and the following disclaimer.
 * * Redistributions in binary form must reproduce the above copyright notice,
 *   this list of conditions and the following disclaimer in the documentation
 *   and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

#ifndef DATA_DEF_H
#define DATA_DEF_H

typedef struct _ENCODING_CFG {
    int32_t      width;
    int32_t      height;
    int32_t      gopSize;
    int32_t      bitRate;
    int32_t      frameRate;

    char         inputType[1024];
    char         inputCodec[1024];
    char         cfgFile[1024];

    char         vuiInfo[1024];
    char         hierarchicalLevel[1024];
    char         laDepth[1024];
    char         encMode[1024];
    char         rcMode[1024];
    char         scDetection[1024];
    char         tune[1024];
    char         qp[1024];
    char         hdr[1024];
    char         asmType[1024];
    char         forcedIdr[1024];
    char         gpuCopy[1024];
    char         aud[1024];
    char         profile[1024];
    char         level[1024];
    char         predStructure[1024];
    char         baseLayerSwitchMode[1024];
    char         tileRows[1024];
    char         tileColumns[1024];
    char         frameNum[1024];
    char         inParallel[1024];
    char         needExternalLog[1024];
    char         minLogLevel[1024];
    char         projType[1024];

    char         outFileName[1024];
} EncodingCfg;

typedef struct _OUTPUT_PARAM {
    uint32_t         out_videos_num;
    EncodingCfg      videos_encoding_cfg[1024];
} OutputParam;

#endif
