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

#include "ParamParser.h"
#include "tinyxml2.h"

using namespace tinyxml2;

int32_t ParamParser::Parse(char *config_file, void *param_pointer)
{
    int32_t ret = 0;

    if (NULL == config_file)
    {
        printf("Configuration file in NULL !\n");
        return -1;
    }

    XMLDocument *doc = new XMLDocument();
    if (NULL == doc)
    {
        printf("Failed to create XML document !\n");
        return -1;
    }

    ret = doc->LoadFile(config_file);
    if (ret)
    {
        printf("Failed to load input configuration file, error %d !\n", ret);

        delete doc;
        doc = NULL;

        return -1;
    }

    OutputParam *out_params = (OutputParam *)param_pointer;
    if (NULL == out_params)
    {
        printf("NULL pointer for params !\n");

        delete doc;
        doc = NULL;

        return -1;
    }

    XMLElement *root = NULL;
    root = doc->RootElement();
    if (!root)
    {
        printf("NULL root pointer !\n");

        delete doc;
        doc = NULL;

        return -1;
    }

    char elementName[1024] = { 0 };
    uint32_t outIdx = 0;
    for ( ; outIdx < 1024; outIdx++)
    {
        memset(elementName, 0, 1024);
        snprintf(elementName, 1024, "Output%d", (outIdx + 1));

        XMLElement *params = NULL;
        params = root->FirstChildElement(elementName);
        if (!params)
            break;

        XMLElement *option = params->FirstChildElement();
        const XMLAttribute *attr1 = option->FirstAttribute();
        //const char* width = attr1->Value();
        //strncpy(out_params->videos_encoding_cfg[outIdx].width, width, 1024);
        out_params->videos_encoding_cfg[outIdx].width = attr1->IntValue();

        option = option->NextSiblingElement();
        const XMLAttribute *attr2 = option->FirstAttribute();
        //const char* height = attr2->Value();
        //strncpy(out_params->videos_encoding_cfg[outIdx].height, height, 1024);
        out_params->videos_encoding_cfg[outIdx].height = attr2->IntValue();

        option = option->NextSiblingElement();
        const XMLAttribute *attr3 = option->FirstAttribute();
        //const char* gop_size = attr3->Value();
        //strncpy(out_params->videos_encoding_cfg[outIdx].gopSize, gop_size, 1024);
        out_params->videos_encoding_cfg[outIdx].gopSize = attr3->IntValue();

        option = option->NextSiblingElement();
        const XMLAttribute *attr4 = option->FirstAttribute();
        //const char* bit_rate = attr4->Value();
        //strncpy(out_params->videos_encoding_cfg[outIdx].bitRate, bit_rate, 1024);
        out_params->videos_encoding_cfg[outIdx].bitRate = attr4->IntValue();

        option = option->NextSiblingElement();
        const XMLAttribute *attr5 = option->FirstAttribute();
        //const char* frame_rate = attr5->Value();
        //strncpy(out_params->videos_encoding_cfg[outIdx].frameRate, frame_rate, 1024);
        out_params->videos_encoding_cfg[outIdx].frameRate = attr5->IntValue();

        option = option->NextSiblingElement();
        const XMLAttribute *attr6 = option->FirstAttribute();
        const char* input_type = attr6->Value();
        strncpy(out_params->videos_encoding_cfg[outIdx].inputType, input_type, 1024);

        option = option->NextSiblingElement();
        const XMLAttribute *attr7 = option->FirstAttribute();
        const char* input_codec = attr7->Value();
        strncpy(out_params->videos_encoding_cfg[outIdx].inputCodec, input_codec, 1024);

        option = option->NextSiblingElement();
        const XMLAttribute *attr8 = option->FirstAttribute();
        const char* config_file = attr8->Value();
        strncpy(out_params->videos_encoding_cfg[outIdx].cfgFile, config_file, 1024);

        option = option->NextSiblingElement();
        const XMLAttribute *attr9 = option->FirstAttribute();
        const char* vui_info = attr9->Value();
        strncpy(out_params->videos_encoding_cfg[outIdx].vuiInfo, vui_info, 1024);

        option = option->NextSiblingElement();
        const XMLAttribute *attr10 = option->FirstAttribute();
        const char* hierarchical_level = attr10->Value();
        strncpy(out_params->videos_encoding_cfg[outIdx].hierarchicalLevel, hierarchical_level, 1024);

        option = option->NextSiblingElement();
        const XMLAttribute *attr11 = option->FirstAttribute();
        const char* la_depth = attr11->Value();
        strncpy(out_params->videos_encoding_cfg[outIdx].laDepth, la_depth, 1024);

        option = option->NextSiblingElement();
        const XMLAttribute *attr12 = option->FirstAttribute();
        const char* preset = attr12->Value();
        strncpy(out_params->videos_encoding_cfg[outIdx].encMode, preset, 1024);

        option = option->NextSiblingElement();
        const XMLAttribute *attr13 = option->FirstAttribute();
        const char* rc_mode = attr13->Value();
        strncpy(out_params->videos_encoding_cfg[outIdx].rcMode, rc_mode, 1024);

        option = option->NextSiblingElement();
        const XMLAttribute *attr14 = option->FirstAttribute();
        const char* sc_detection = attr14->Value();
        strncpy(out_params->videos_encoding_cfg[outIdx].scDetection, sc_detection, 1024);

        option = option->NextSiblingElement();
        const XMLAttribute *attr15 = option->FirstAttribute();
        const char* tune = attr15->Value();
        strncpy(out_params->videos_encoding_cfg[outIdx].tune, tune, 1024);

        option = option->NextSiblingElement();
        const XMLAttribute *attr16 = option->FirstAttribute();
        const char* qp = attr16->Value();
        strncpy(out_params->videos_encoding_cfg[outIdx].qp, qp, 1024);

        option = option->NextSiblingElement();
        const XMLAttribute *attr17 = option->FirstAttribute();
        const char* hdr = attr17->Value();
        strncpy(out_params->videos_encoding_cfg[outIdx].hdr, hdr, 1024);

        option = option->NextSiblingElement();
        const XMLAttribute *attr18 = option->FirstAttribute();
        const char* asm_type = attr18->Value();
        strncpy(out_params->videos_encoding_cfg[outIdx].asmType, asm_type, 1024);

        option = option->NextSiblingElement();
        const XMLAttribute *attr19 = option->FirstAttribute();
        const char* forced_idr = attr19->Value();
        strncpy(out_params->videos_encoding_cfg[outIdx].forcedIdr, forced_idr, 1024);

        option = option->NextSiblingElement();
        const XMLAttribute *attr20 = option->FirstAttribute();
        const char* gpu_copy = attr20->Value();
        strncpy(out_params->videos_encoding_cfg[outIdx].gpuCopy, gpu_copy, 1024);

        option = option->NextSiblingElement();
        const XMLAttribute *attr21 = option->FirstAttribute();
        const char* aud = attr21->Value();
        strncpy(out_params->videos_encoding_cfg[outIdx].aud, aud, 1024);

        option = option->NextSiblingElement();
        const XMLAttribute *attr22 = option->FirstAttribute();
        const char* profile = attr22->Value();
        strncpy(out_params->videos_encoding_cfg[outIdx].profile, profile, 1024);

        option = option->NextSiblingElement();
        const XMLAttribute *attr23 = option->FirstAttribute();
        const char* level = attr23->Value();
        strncpy(out_params->videos_encoding_cfg[outIdx].level, level, 1024);

        option = option->NextSiblingElement();
        const XMLAttribute *attr24 = option->FirstAttribute();
        const char* pred_structure = attr24->Value();
        strncpy(out_params->videos_encoding_cfg[outIdx].predStructure, pred_structure, 1024);

        option = option->NextSiblingElement();
        const XMLAttribute *attr25 = option->FirstAttribute();
        const char* base_layer_switch_mode = attr25->Value();
        strncpy(out_params->videos_encoding_cfg[outIdx].baseLayerSwitchMode, base_layer_switch_mode, 1024);

        option = option->NextSiblingElement();
        const XMLAttribute *attr26 = option->FirstAttribute();
        const char* tile_rows = attr26->Value();
        strncpy(out_params->videos_encoding_cfg[outIdx].tileRows, tile_rows, 1024);

        option = option->NextSiblingElement();
        const XMLAttribute *attr27 = option->FirstAttribute();
        const char* tile_columns = attr27->Value();
        strncpy(out_params->videos_encoding_cfg[outIdx].tileColumns, tile_columns, 1024);

        option = option->NextSiblingElement();
        const XMLAttribute *attr28 = option->FirstAttribute();
        const char* frame_num = attr28->Value();
        strncpy(out_params->videos_encoding_cfg[outIdx].frameNum, frame_num, 1024);

        option = option->NextSiblingElement();
        const XMLAttribute *attr29 = option->FirstAttribute();
        const char* in_parallel = attr29->Value();
        strncpy(out_params->videos_encoding_cfg[outIdx].inParallel, in_parallel, 1024);

        option = option->NextSiblingElement();
        const XMLAttribute *attr30 = option->FirstAttribute();
        const char* need_external_log = attr30->Value();
        strncpy(out_params->videos_encoding_cfg[outIdx].needExternalLog, need_external_log, 1024);

        option = option->NextSiblingElement();
        const XMLAttribute *attr31 = option->FirstAttribute();
        const char* min_log_level = attr31->Value();
        strncpy(out_params->videos_encoding_cfg[outIdx].minLogLevel, min_log_level, 1024);

        option = option->NextSiblingElement();
        const XMLAttribute *attr32 = option->FirstAttribute();
        const char* proj_type = attr32->Value();
        strncpy(out_params->videos_encoding_cfg[outIdx].projType, proj_type, 1024);

        option = option->NextSiblingElement();
        const XMLAttribute *attr33 = option->FirstAttribute();
        const char* out_file_name = attr33->Value();
        strncpy(out_params->videos_encoding_cfg[outIdx].outFileName, out_file_name, 1024);
    }

    out_params->out_videos_num = outIdx;
    printf("There are total %u video outputs !\n", out_params->out_videos_num);

    delete doc;
    doc = NULL;

    return 0;
}
