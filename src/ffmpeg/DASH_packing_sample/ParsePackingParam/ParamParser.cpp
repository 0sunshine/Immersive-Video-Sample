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

    DASH_IN_PARAM *dash_params = (DASH_IN_PARAM *)param_pointer;
    if (NULL == dash_params)
    {
        printf("NULL pointer for params !\n");

        delete doc;
        doc = NULL;

        return -1;
    }

    XMLElement *root = doc->RootElement();
    XMLElement *params = root->FirstChildElement("OptionsList");
    XMLElement *option = params->FirstChildElement();
    const XMLAttribute *attr1 = option->FirstAttribute();
    const char* proj_type = attr1->Value();
    strncpy(dash_params->packing_param.proj_type, proj_type, 1024);

    option = option->NextSiblingElement();
    const XMLAttribute *attr2 = option->FirstAttribute();
    const char* face_file = attr2->Value();
    strncpy(dash_params->packing_param.face_file, face_file, 1024);// = attr2->Value();

    option = option->NextSiblingElement();
    const XMLAttribute *attr3 = option->FirstAttribute();
    const char* viewport_w = attr3->Value();
    strncpy(dash_params->packing_param.viewport_w, viewport_w, 1024);// = attr3->Value();

    option = option->NextSiblingElement();
    const XMLAttribute *attr4 = option->FirstAttribute();
    const char* viewport_h = attr4->Value();
    strncpy(dash_params->packing_param.viewport_h, viewport_h, 1024);// = attr4->Value();

    option = option->NextSiblingElement();
    const XMLAttribute *attr5 = option->FirstAttribute();
    const char* viewport_yaw = attr5->Value();
    strncpy(dash_params->packing_param.viewport_yaw, viewport_yaw, 1024);// = attr5->Value();

    option = option->NextSiblingElement();
    const XMLAttribute *attr6 = option->FirstAttribute();
    const char* viewport_pitch = attr6->Value();
    strncpy(dash_params->packing_param.viewport_pitch, viewport_pitch, 1024);// = attr6->Value();

    option = option->NextSiblingElement();
    const XMLAttribute *attr7 = option->FirstAttribute();
    const char* viewport_fov_hor = attr7->Value();
    strncpy(dash_params->packing_param.viewport_fov_hor, viewport_fov_hor, 1024);// = attr7->Value();

    option = option->NextSiblingElement();
    const XMLAttribute *attr8 = option->FirstAttribute();
    const char* viewport_fov_ver = attr8->Value();
    strncpy(dash_params->packing_param.viewport_fov_ver, viewport_fov_ver, 1024);// = attr8->Value();

    option = option->NextSiblingElement();
    const XMLAttribute *attr9 = option->FirstAttribute();
    const char* window_size = attr9->Value();
    strncpy(dash_params->packing_param.window_size, window_size, 1024);// = attr9->Value();

    option = option->NextSiblingElement();
    const XMLAttribute *attr10 = option->FirstAttribute();
    const char* extra_window_size = attr10->Value();
    strncpy(dash_params->packing_param.extra_window_size, extra_window_size, 1024);// = attr10->Value();

    option = option->NextSiblingElement();
    const XMLAttribute *attr11 = option->FirstAttribute();
    const char* split_tile = attr11->Value();
    strncpy(dash_params->packing_param.split_tile, split_tile, 1024);// = attr11->Value();

    option = option->NextSiblingElement();
    const XMLAttribute *attr12 = option->FirstAttribute();
    const char* seg_duration = attr12->Value();
    strncpy(dash_params->packing_param.seg_duration, seg_duration, 1024);// = attr12->Value();

    option = option->NextSiblingElement();
    const XMLAttribute *attr13 = option->FirstAttribute();
    const char* is_live = attr13->Value();
    strncpy(dash_params->packing_param.is_live, is_live, 1024);// = attr13->Value();
    dash_params->packing_param.live_mode = attr13->IntValue();

    option = option->NextSiblingElement();
    const XMLAttribute *attr14 = option->FirstAttribute();
    const char* base_url = attr14->Value();
    strncpy(dash_params->packing_param.base_url, base_url, 1024);// = attr14->Value();

    option = option->NextSiblingElement();
    const XMLAttribute *attr15 = option->FirstAttribute();
    const char* out_name = attr15->Value();
    strncpy(dash_params->packing_param.out_name, out_name, 1024);// = attr15->Value();

    option = option->NextSiblingElement();
    const XMLAttribute *attr16 = option->FirstAttribute();
    const char* need_buffered_frames = attr16->Value();
    strncpy(dash_params->packing_param.need_buffered_frames, need_buffered_frames, 1024);// = attr16->Value();

    option = option->NextSiblingElement();
    const XMLAttribute *attr17 = option->FirstAttribute();
    const char* packingPluginPath = attr17->Value();
    strncpy(dash_params->packing_param.packingPluginPath, packingPluginPath, 1024);// = attr17->Value();

    option = option->NextSiblingElement();
    const XMLAttribute *attr18 = option->FirstAttribute();
    const char* packingPluginName = attr18->Value();
    strncpy(dash_params->packing_param.packingPluginName, packingPluginName, 1024);// = attr18->Value();

    option = option->NextSiblingElement();
    const XMLAttribute *attr19 = option->FirstAttribute();
    const char* videoPluginPath = attr19->Value();
    strncpy(dash_params->packing_param.videoPluginPath, videoPluginPath, 1024);// = attr19->Value();

    option = option->NextSiblingElement();
    const XMLAttribute *attr20 = option->FirstAttribute();
    const char* videoPluginName = attr20->Value();
    strncpy(dash_params->packing_param.videoPluginName, videoPluginName, 1024);// = attr20->Value();

    option = option->NextSiblingElement();
    const XMLAttribute *attr21 = option->FirstAttribute();
    const char* audioPluginPath = attr21->Value();
    strncpy(dash_params->packing_param.audioPluginPath, audioPluginPath, 1024);// = attr21->Value();

    option = option->NextSiblingElement();
    const XMLAttribute *attr22 = option->FirstAttribute();
    const char* audioPluginName = attr22->Value();
    strncpy(dash_params->packing_param.audioPluginName, audioPluginName, 1024);// = attr22->Value();

    option = option->NextSiblingElement();
    const XMLAttribute *attr23 = option->FirstAttribute();
    const char* has_extractor = attr23->Value();
    strncpy(dash_params->packing_param.has_extractor, has_extractor, 1024);// = attr23->Value();

    option = option->NextSiblingElement();
    const XMLAttribute *attr24 = option->FirstAttribute();
    const char* extractors_per_thread = attr24->Value();
    strncpy(dash_params->packing_param.extractors_per_thread, extractors_per_thread, 1024);// = attr24->Value();

    option = option->NextSiblingElement();
    const XMLAttribute *attr25 = option->FirstAttribute();
    const char* fixedPackedPicRes = attr25->Value();
    strncpy(dash_params->packing_param.fixedPackedPicRes, fixedPackedPicRes, 1024);// = attr25->Value();

    option = option->NextSiblingElement();
    const XMLAttribute *attr26 = option->FirstAttribute();
    const char* cmafEnabled = attr26->Value();
    strncpy(dash_params->packing_param.cmafEnabled, cmafEnabled, 1024);// = attr26->Value();

    option = option->NextSiblingElement();
    const XMLAttribute *attr27 = option->FirstAttribute();
    const char* chunkDur = attr27->Value();
    strncpy(dash_params->packing_param.chunkDur, chunkDur, 1024);// = attr27->Value();

    option = option->NextSiblingElement();
    const XMLAttribute *attr28 = option->FirstAttribute();
    const char* segWriterPluginPath = attr28->Value();
    strncpy(dash_params->packing_param.segWriterPluginPath, segWriterPluginPath, 1024);//  = attr28->Value();

    option = option->NextSiblingElement();
    const XMLAttribute *attr29 = option->FirstAttribute();
    const char* segWriterPluginName = attr29->Value();
    strncpy(dash_params->packing_param.segWriterPluginName, segWriterPluginName, 1024);// = attr29->Value();

    option = option->NextSiblingElement();
    const XMLAttribute *attr30 = option->FirstAttribute();
    const char* mpdWriterPluginPath = attr30->Value();
    strncpy(dash_params->packing_param.mpdWriterPluginPath, mpdWriterPluginPath, 1024);// = attr30->Value();

    option = option->NextSiblingElement();
    const XMLAttribute *attr31 = option->FirstAttribute();
    const char* mpdWriterPluginName = attr31->Value();
    strncpy(dash_params->packing_param.mpdWriterPluginName, mpdWriterPluginName, 1024);//  = attr31->Value();

    option = option->NextSiblingElement();
    const XMLAttribute *attr32 = option->FirstAttribute();
    const char* target_latency = attr32->Value();
    strncpy(dash_params->packing_param.target_latency, target_latency, 1024);// = attr32->Value();

    option = option->NextSiblingElement();
    const XMLAttribute *attr33 = option->FirstAttribute();
    const char* min_latency = attr33->Value();
    strncpy(dash_params->packing_param.min_latency, min_latency, 1024);// = attr33->Value();

    option = option->NextSiblingElement();
    const XMLAttribute *attr34 = option->FirstAttribute();
    const char* max_latency = attr34->Value();
    strncpy(dash_params->packing_param.max_latency, max_latency, 1024);// = attr34->Value();

    option = option->NextSiblingElement();
    const XMLAttribute *attr35 = option->FirstAttribute();
    const char* need_external_log = attr35->Value();
    strncpy(dash_params->packing_param.need_external_log, need_external_log, 1024);// = attr35->Value();

    option = option->NextSiblingElement();
    const XMLAttribute *attr36 = option->FirstAttribute();
    const char* min_log_level = attr36->Value();
    strncpy(dash_params->packing_param.min_log_level, min_log_level, 1024);// = attr36->Value();

    option = option->NextSiblingElement();
    const XMLAttribute *attr37 = option->FirstAttribute();
    dash_params->packing_frames_num = attr37->Int64Value();

    option = option->NextSiblingElement();
    const XMLAttribute *attr38 = option->FirstAttribute();
    dash_params->in_videos_num = attr38->UnsignedValue();

    //High resolution video
    XMLElement *video1 = root->FirstChildElement("Video1");
    XMLElement *video_ele = video1->FirstChildElement();
    const XMLAttribute *video1_attr1 = video_ele->FirstAttribute();
    dash_params->videos_param[0].video_file_name = video1_attr1->Value();

    video_ele = video_ele->NextSiblingElement();
    const XMLAttribute *video1_attr2 = video_ele->FirstAttribute();
    dash_params->videos_param[0].width = video1_attr2->IntValue();

    video_ele = video_ele->NextSiblingElement();
    const XMLAttribute *video1_attr3 = video_ele->FirstAttribute();
    dash_params->videos_param[0].height = video1_attr3->IntValue();

    video_ele = video_ele->NextSiblingElement();
    const XMLAttribute *video1_attr4 = video_ele->FirstAttribute();
    dash_params->videos_param[0].bit_rate = video1_attr4->Int64Value();

    video_ele = video_ele->NextSiblingElement();
    const XMLAttribute *video1_attr5 = video_ele->FirstAttribute();
    dash_params->videos_param[0].frame_rate = video1_attr5->UnsignedValue();

    video_ele = video_ele->NextSiblingElement();
    const XMLAttribute *video1_attr6 = video_ele->FirstAttribute();
    dash_params->videos_param[0].gop_size = video1_attr6->UnsignedValue();

    video_ele = video_ele->NextSiblingElement();
    const XMLAttribute *video1_attr7 = video_ele->FirstAttribute();
    dash_params->videos_param[0].frame_len_name = video1_attr7->Value();

    //Low resolution video
    XMLElement *video2 = root->FirstChildElement("Video2");
    video_ele = video2->FirstChildElement();
    const XMLAttribute *video2_attr1 = video_ele->FirstAttribute();
    dash_params->videos_param[1].video_file_name = video2_attr1->Value();

    video_ele = video_ele->NextSiblingElement();
    const XMLAttribute *video2_attr2 = video_ele->FirstAttribute();
    dash_params->videos_param[1].width = video2_attr2->IntValue();

    video_ele = video_ele->NextSiblingElement();
    const XMLAttribute *video2_attr3 = video_ele->FirstAttribute();
    dash_params->videos_param[1].height = video2_attr3->IntValue();

    video_ele = video_ele->NextSiblingElement();
    const XMLAttribute *video2_attr4 = video_ele->FirstAttribute();
    dash_params->videos_param[1].bit_rate = video2_attr4->Int64Value();

    video_ele = video_ele->NextSiblingElement();
    const XMLAttribute *video2_attr5 = video_ele->FirstAttribute();
    dash_params->videos_param[1].frame_rate = video2_attr5->UnsignedValue();

    video_ele = video_ele->NextSiblingElement();
    const XMLAttribute *video2_attr6 = video_ele->FirstAttribute();
    dash_params->videos_param[1].gop_size = video2_attr6->UnsignedValue();

    video_ele = video_ele->NextSiblingElement();
    const XMLAttribute *video2_attr7 = video_ele->FirstAttribute();
    dash_params->videos_param[1].frame_len_name = video2_attr7->Value();

    delete doc;
    doc = NULL;

    return 0;
}
