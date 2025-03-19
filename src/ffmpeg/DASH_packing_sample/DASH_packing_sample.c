#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <math.h>
#include <libavutil/avassert.h>
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>
#include <libavutil/mathematics.h>
#include <libavutil/timestamp.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
#include "./ParsePackingParam/PackingParamsDef.h"
#include "./ParsePackingParam/ParsePackingParamAPI.h"

#define H265_NAL_TYPE_VPS 32
#define H265_NAL_TYPE_SPS 33
#define H265_NAL_TYPE_PPS 34
#define H265_NAL_TYPE_PREFIX_SEI 39

extern int ff_alloc_packet2(AVCodecContext *avctx, AVPacket *avpkt, int64_t size, int64_t min_size);

int32_t find_next_start_code(const uint8_t *buf, const uint8_t *next_avc, int32_t *startCodesNum)
{
    int32_t i = 0;
    int32_t nalu_size = 0;
    if (buf + 3 > next_avc)
        return -1;
    while ((buf + i + 3) <= next_avc) {
        if (buf[i] == 0 && buf[i + 1] == 0 && buf[i + 2] == 1)
        {
            if (i > 0)
            {
                if (buf[i - 1] == 0)
                {
                    *startCodesNum = 4;
                }
                else
                {
                    *startCodesNum = 3;
                }
            }
            else
            {
                *startCodesNum = 3;
            }
            break;
        }
        i++;
    }
    if ((buf + i + 3) > next_avc)
    {
        return -1;
    }
    i = 0;
    int32_t extraZeroNum = 0;
    const uint8_t *temp = buf + (*startCodesNum);
    while ((temp + i + 3) <= next_avc) {
        if (temp[i] == 0 && temp[i + 1] == 0 && temp[i + 2] == 1)
        {
            if (i > 0)
            {
                if (temp[i - 1] == 0)
                {
                    extraZeroNum = 1;
                }
                else
                {
                    extraZeroNum = 0;
                }
            }
            break;
        }
        i++;
    }
    nalu_size = i - extraZeroNum + (*startCodesNum);
    return nalu_size;
}

int32_t ReadH265Head(uint8_t *frameData, int32_t frameDataLen, uint8_t *headData, int32_t *headLen)
{
    uint8_t *currentFrameData = frameData;
    uint8_t *frameDataEnd = frameData + frameDataLen;
    int32_t offset = 0;
    int32_t NALType = 0;
    *headLen = 0;
    int32_t i = 0;
    while (1) {
        int32_t startCodesNum = 0;
        offset = find_next_start_code(currentFrameData, frameDataEnd, &startCodesNum);
        if (offset < 0)
            break;
        NALType = (currentFrameData[startCodesNum] & 0x7E) >> 1;
        if (NALType == H265_NAL_TYPE_VPS || NALType == H265_NAL_TYPE_SPS || NALType == H265_NAL_TYPE_PPS) {
            memcpy(headData + i, frameData + i, offset);
            i += offset;
        }
        else if (NALType == H265_NAL_TYPE_PREFIX_SEI) {
            memcpy(headData + i, frameData + i, offset);
            i += offset;
        }
        currentFrameData += offset;
    }
    *headLen = i;
    return 0;
}

typedef struct _DASH_CONTEXT {
    AVFormatContext *formatContext;
    AVStream* input_streams[1024];
    AVCodecContext *codecContext;
    AVCodec *codec;
    int32_t PTS;
    int32_t GOPSize;
    int32_t frameCount;
} DASH_CONTEXT;

int32_t DASHClose(DASH_CONTEXT *inPointer)
{
    DASH_CONTEXT *dash_context = inPointer;
    if (dash_context != NULL)
    {
        if (dash_context->formatContext != NULL)
            av_write_trailer(dash_context->formatContext);

        if (dash_context->codecContext != NULL)
        {
            avcodec_close(dash_context->codecContext);
            avcodec_free_context(&(dash_context->codecContext));
            dash_context->codecContext = NULL;
        }

        if (dash_context->formatContext != NULL)
        {
            AVOutputFormat *outFormat = dash_context->formatContext->oformat;
            if (!(outFormat->flags & AVFMT_NOFILE))
                avio_closep(&(dash_context->formatContext->pb));
            avformat_free_context(dash_context->formatContext);
            dash_context->formatContext = NULL;
        }

        free(dash_context);
        dash_context = NULL;
    }
    return 0;
}

DASH_CONTEXT *DASHOpen(DASH_IN_PARAM *dash_params)
{
    int32_t ret = 0;
    DASH_CONTEXT *dash_context = (DASH_CONTEXT *)malloc(sizeof(DASH_CONTEXT));
    if (NULL == dash_context)
    {
        printf("Failed to alloc memory for DASH context !\n");
        return NULL;
    }

    uint32_t url_len = strlen(dash_params->packing_param.base_url);
    if (dash_params->packing_param.base_url[url_len - 1] != '/')
    {
        printf("Incorrect base url setting !\n");
        return NULL;
    }

    uint32_t idx = 0;
    for (idx = (url_len - 2); idx >= 0; idx--)
    {
        if (dash_params->packing_param.base_url[idx] == '/')
            break;
    }

    char folder_name[128] = { 0 };
    strncpy(folder_name, (dash_params->packing_param.base_url + idx + 1), (url_len - idx - 2));
    char fileName[256] = {0};
    snprintf(fileName, 256, "/usr/local/nginx/html/%s/", folder_name);
    AVFormatContext *formatContext = NULL;
    avformat_alloc_output_context2(&formatContext, NULL, "omaf_packing", fileName);
    if (NULL == formatContext)
    {
        printf("Failed to alloc omaf_packing context !\n");
        DASHClose(dash_context);
        return NULL;
    }

    formatContext->oformat->video_codec = AV_CODEC_ID_HEVC;
    AVOutputFormat *outFormat = formatContext->oformat;
    dash_context->formatContext = formatContext;
    AVCodec *codec = avcodec_find_encoder(outFormat->video_codec);
    if (NULL == codec)
    {
        printf("Failed to find HEVC codec !\n");
        DASHClose(dash_context);
        return NULL;
    }

    dash_context->codec = codec;
    AVCodecContext *codecContext = avcodec_alloc_context3(codec);
    if (NULL == codecContext)
    {
        printf("Failed to alloc codec context !\n");
        DASHClose(dash_context);
        return NULL;
    }

    codecContext->codec_id = outFormat->video_codec;
    codecContext->time_base.num = dash_params->videos_param[0].frame_rate;
    codecContext->time_base.den = 1;
    codecContext->framerate.num = dash_params->videos_param[0].frame_rate;
    codecContext->framerate.den = 1;
    codecContext->gop_size = dash_params->videos_param[0].gop_size;
    codecContext->pix_fmt = AV_PIX_FMT_YUV420P;
    if (formatContext->oformat->flags & AVFMT_GLOBALHEADER)
        codecContext->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

    dash_context->codecContext = codecContext;
    for (uint8_t stream_id = 1; stream_id <= dash_params->in_videos_num; stream_id++)
    {
        AVStream *input_stream = avformat_new_stream(dash_context->formatContext, NULL);
        if (NULL == input_stream)
        {
            printf("Failed to create AVStream !\n");
            DASHClose(dash_context);
            return NULL;
        }

        input_stream->id = stream_id - 1;
        input_stream->time_base.den = 1;
        input_stream->time_base.num = dash_params->videos_param[stream_id - 1].frame_rate;
        input_stream->avg_frame_rate.num = dash_params->videos_param[stream_id - 1].frame_rate;
        input_stream->avg_frame_rate.den = 1;
        codecContext->bit_rate = dash_params->videos_param[stream_id - 1].bit_rate;
        codecContext->width = dash_params->videos_param[stream_id - 1].width;
        codecContext->height = dash_params->videos_param[stream_id - 1].height;
        dash_context->input_streams[stream_id - 1] = input_stream;
        int32_t ret = 0;
        ret = avcodec_parameters_from_context(input_stream->codecpar, codecContext);
        if (ret < 0)
        {
            printf("Failed to copy the stream codec parameters !\n");
            DASHClose(dash_context);
            return NULL;
        }
    }

    av_dump_format(formatContext, 0, fileName, 1);
    av_opt_set(formatContext->priv_data, "packing_proj_type", dash_params->packing_param.proj_type, 0);
    av_opt_set(formatContext->priv_data, "cubemap_face_file", dash_params->packing_param.face_file, 0);
    av_opt_set(formatContext->priv_data, "viewport_w", dash_params->packing_param.viewport_w, 0);
    av_opt_set(formatContext->priv_data, "viewport_h", dash_params->packing_param.viewport_h, 0);
    av_opt_set(formatContext->priv_data, "viewport_yaw", dash_params->packing_param.viewport_yaw, 0);
    av_opt_set(formatContext->priv_data, "viewport_pitch", dash_params->packing_param.viewport_pitch, 0);
    av_opt_set(formatContext->priv_data, "viewport_fov_hor", dash_params->packing_param.viewport_fov_hor, 0);
    av_opt_set(formatContext->priv_data, "viewport_fov_ver", dash_params->packing_param.viewport_fov_ver, 0);
    av_opt_set(formatContext->priv_data, "window_size", dash_params->packing_param.window_size, 0);
    av_opt_set(formatContext->priv_data, "extra_window_size", dash_params->packing_param.extra_window_size, 0);
    av_opt_set(formatContext->priv_data, "split_tile", dash_params->packing_param.split_tile, 0);
    av_opt_set(formatContext->priv_data, "seg_duration", dash_params->packing_param.seg_duration, 0);
    av_opt_set(formatContext->priv_data, "is_live", dash_params->packing_param.is_live, 0);
    av_opt_set(formatContext->priv_data, "base_url", dash_params->packing_param.base_url, 0);
    av_opt_set(formatContext->priv_data, "out_name", dash_params->packing_param.out_name, 0);
    av_opt_set(formatContext->priv_data, "need_buffered_frames", dash_params->packing_param.need_buffered_frames, 0);
    av_opt_set(formatContext->priv_data, "extractors_per_thread", dash_params->packing_param.extractors_per_thread, 0);
    av_opt_set(formatContext->priv_data, "has_extractor", dash_params->packing_param.has_extractor, 0);
    av_opt_set(formatContext->priv_data, "packing_plugin_path", dash_params->packing_param.packingPluginPath, 0);
    av_opt_set(formatContext->priv_data, "packing_plugin_name", dash_params->packing_param.packingPluginName, 0);
    av_opt_set(formatContext->priv_data, "video_plugin_path", dash_params->packing_param.videoPluginPath, 0);
    av_opt_set(formatContext->priv_data, "video_plugin_name", dash_params->packing_param.videoPluginName, 0);
    av_opt_set(formatContext->priv_data, "audio_plugin_path", dash_params->packing_param.audioPluginPath, 0);
    av_opt_set(formatContext->priv_data, "audio_plugin_name", dash_params->packing_param.audioPluginName, 0);
    av_opt_set(formatContext->priv_data, "fixed_extractors_res", dash_params->packing_param.fixedPackedPicRes, 0);
    av_opt_set(formatContext->priv_data, "cmaf_enabled", dash_params->packing_param.cmafEnabled, 0);
    av_opt_set(formatContext->priv_data, "chunk_dur", dash_params->packing_param.chunkDur, 0);
    av_opt_set(formatContext->priv_data, "seg_writer_path", dash_params->packing_param.segWriterPluginPath, 0);
    av_opt_set(formatContext->priv_data, "seg_writer_name", dash_params->packing_param.segWriterPluginName, 0);
    av_opt_set(formatContext->priv_data, "mpd_writer_path", dash_params->packing_param.mpdWriterPluginPath, 0);
    av_opt_set(formatContext->priv_data, "mpd_writer_name", dash_params->packing_param.mpdWriterPluginName, 0);
    av_opt_set(formatContext->priv_data, "target_latency", dash_params->packing_param.target_latency, 0);
    av_opt_set(formatContext->priv_data, "min_latency", dash_params->packing_param.min_latency, 0);
    av_opt_set(formatContext->priv_data, "max_latency", dash_params->packing_param.max_latency, 0);
    av_opt_set(formatContext->priv_data, "need_external_log", dash_params->packing_param.need_external_log, 0);
    av_opt_set(formatContext->priv_data, "min_log_level", dash_params->packing_param.min_log_level, 0);

    if (!(outFormat->flags & AVFMT_NOFILE))
    {
        ret = avio_open(&formatContext->pb, fileName, AVIO_FLAG_WRITE);
        if (ret < 0)
        {
            printf("Failed to open '%s'!\n", fileName);
            DASHClose(dash_context);
            return NULL;
        }
    }

    ret = avformat_write_header(formatContext, NULL);
    if (ret < 0)
    {
        printf("Error occurred when opening output file !\n");
        DASHClose(dash_context);
        return NULL;
    }

    dash_context->GOPSize = dash_params->videos_param[0].gop_size;
    dash_context->frameCount = 0;
    return dash_context;
}

int32_t DASHMuxing(DASH_IN_PARAM *dash_params, DASH_CONTEXT *inPointer, uint8_t *frame_data[], int32_t frame_size[])
{
    DASH_CONTEXT *dash_context = inPointer;
    int32_t ret = 0;

    for (uint8_t stream_id = 1; stream_id <= dash_params->in_videos_num; stream_id++)
    {
        AVPacket packet;
        av_new_packet(&packet, frame_size[stream_id - 1]);//(frame_size[stream_id - 1] + 1));
        if ((ret = ff_alloc_packet2(NULL, &packet, frame_size[stream_id - 1], 0)) < 0)
        {
            av_log(NULL, AV_LOG_ERROR, "Failed to allocate packet.\n");
            return ret;
        }

        if (packet.data == NULL)
        {
            printf("pkt.data is NULL\n");
            return -1;
        }

        int32_t GOPSize = dash_context->GOPSize;
        if (dash_context->frameCount % GOPSize == 0)
            packet.flags = 1;
        else
            packet.flags = 0;

        packet.dts = packet.pts = dash_context->frameCount;
        AVStream *one_stream = dash_context->input_streams[stream_id - 1];
        packet.stream_index = stream_id - 1;
        one_stream->index = stream_id - 1;
        memcpy(packet.data, frame_data[stream_id - 1], frame_size[stream_id - 1]);
        packet.size = frame_size[stream_id - 1];

        if (dash_context->frameCount == 0)
        {
            uint8_t *headerData = (uint8_t *)malloc(4096);
            if (headerData == NULL)
            {
                printf("headerData is NULL\n");
                free(packet.data);
                packet.data = NULL;
                return -1;
            }

            packet.side_data = (AVPacketSideData *)malloc(sizeof(AVPacketSideData));
            packet.side_data[0].size = 4096;
            packet.side_data[0].data = av_malloc(packet.side_data->size + AV_INPUT_BUFFER_PADDING_SIZE);
            packet.side_data[0].type = AV_PKT_DATA_NEW_EXTRADATA;
            packet.side_data_elems = 1;
            int32_t headerSize= 0;
            ReadH265Head(frame_data[stream_id - 1], frame_size[stream_id - 1], headerData, &headerSize);
            packet.side_data[0].size = headerSize;
            //printf("video stream %d has header size %d \n", stream_id, headerSize);
            memcpy(packet.side_data[0].data, headerData, headerSize);
            free(headerData);
            headerData = NULL;
        }

        av_packet_rescale_ts(&packet, dash_context->codecContext->time_base, one_stream->time_base);
        packet.stream_index = one_stream->index;
        ret = av_interleaved_write_frame(dash_context->formatContext, &packet);
        if (ret < 0) {
            printf("Failed to pack video frame: %s !\n", av_err2str(ret));
            free(packet.data);
            packet.data = NULL;
            return -1;
        }
    }
    dash_context->frameCount++;
    return 0;
}

int32_t PackStreams(DASH_IN_PARAM *dash_params)
{
    int32_t ret = 0;
    size_t ret1 = 0;
    uint32_t streams_num = dash_params->in_videos_num;
    FILE* streams_file[1024] = { NULL };
    FILE* frame_len_file[1024] = { NULL };

    for (uint32_t stream_id = 1; stream_id <= streams_num; stream_id++)
    {
        streams_file[stream_id - 1] = fopen(dash_params->videos_param[stream_id - 1].video_file_name, "rb");
        if (NULL == streams_file[stream_id - 1])
        {
            printf("Failed to open %s !\n", dash_params->videos_param[stream_id - 1].video_file_name);
            goto now_free;
        }

        frame_len_file[stream_id - 1] = fopen(dash_params->videos_param[stream_id - 1].frame_len_name, "r");
        if (NULL == frame_len_file[stream_id - 1])
        {
            printf("Failed to open %s !\n", dash_params->videos_param[stream_id - 1].frame_len_name);
            goto now_free;
        }
    }

    void *ctx = DASHOpen(dash_params);
    uint8_t* video_frame_data[1024] = { NULL };
    for (uint8_t stream_id = 1; stream_id <= dash_params->in_videos_num; stream_id++)
    {
        int32_t width = dash_params->videos_param[stream_id - 1].width;
        int32_t height = dash_params->videos_param[stream_id - 1].height;
        video_frame_data[stream_id - 1] = (uint8_t *)malloc(width * height * 3 / 2);
        if (NULL == video_frame_data[stream_id - 1])
        {
            printf("Failed in mallocing video frame data !\n");
            goto now_free;
        }
    }

    int32_t frame_data_size[1024] = { 0 };
    uint64_t frameNum = dash_params->packing_frames_num;;
    for (uint64_t i = 0; i < frameNum; i++)
    {
        for (uint32_t stream_id = 1; stream_id <= dash_params->in_videos_num; stream_id++)
        {
            if (dash_params->packing_param.live_mode)
            {
                int32_t frame_rate = dash_params->videos_param[0].frame_rate;
                int32_t frame_dur = (int32_t)((1000000 / frame_rate) / 2);
                frame_dur = (frame_dur / 100 - 1) * 100;
                usleep(frame_dur);
            }

            ret = fscanf(frame_len_file[stream_id - 1], "%d,", &(frame_data_size[stream_id - 1]));
            if (ret == -1)
            {
                printf("frame data size file has EOF !\n");
            }

            ret1 = fread(video_frame_data[stream_id - 1], 1, frame_data_size[stream_id - 1], streams_file[stream_id - 1]);
            if (ret1 < frame_data_size[stream_id - 1])
            {
                printf("Error in reading frame data, waiting for next judgement !\n");
            }

            if (feof(frame_len_file[stream_id - 1]) && feof(streams_file[stream_id - 1]))
            {
                fclose(frame_len_file[stream_id - 1]);
                frame_len_file[stream_id - 1] = NULL;
                fclose(streams_file[stream_id - 1]);
                streams_file[stream_id - 1] = NULL;
                streams_file[stream_id - 1] = fopen(dash_params->videos_param[stream_id - 1].video_file_name, "rb");

                if (NULL == streams_file[stream_id - 1])
                {
                    printf("Failed to open %s again !\n", dash_params->videos_param[stream_id - 1].video_file_name);
                    goto now_free;
                }
                frame_len_file[stream_id - 1] = fopen(dash_params->videos_param[stream_id - 1].frame_len_name, "r");

                if (NULL == frame_len_file[stream_id - 1])
                {
                    printf("Failed to open %s again !\n", dash_params->videos_param[stream_id - 1].frame_len_name);
                    goto now_free;
                }

                ret = fscanf(frame_len_file[stream_id - 1], "%d,", &(frame_data_size[stream_id - 1]));
                if (ret == -1)
                {
                    printf("frame data size file has EOF !\n");
                    goto now_free;
                }

                ret1 = fread(video_frame_data[stream_id - 1], 1, frame_data_size[stream_id - 1], streams_file[stream_id - 1]);
                if (ret1 < frame_data_size[stream_id - 1])
                {
                    printf("Error in reading frame data !\n");
                    goto now_free;
                }
            }
            else if (!feof(frame_len_file[stream_id - 1]) && feof(streams_file[stream_id - 1]))
            {
                printf("Incorrect EOF happens between stream file and frame size file !\n");
                goto now_free;
            }
            else if (feof(frame_len_file[stream_id - 1]) && !feof(streams_file[stream_id - 1]))
            {
                printf("Incorrect EOF happens between stream file and frame size file !\n");
                goto now_free;
            }
        }

        DASHMuxing(dash_params, ctx, video_frame_data, frame_data_size);
    }

    DASHClose(ctx);

now_free:
    for (uint32_t stream_id = 0; stream_id < dash_params->in_videos_num; stream_id++)
    {
        if (video_frame_data[stream_id])
        {
            free(video_frame_data[stream_id]);
            video_frame_data[stream_id] = NULL;
        }

        if (streams_file[stream_id])
        {
            fclose(streams_file[stream_id]);
            streams_file[stream_id] = NULL;
        }

        if (frame_len_file[stream_id])
        {
            fclose(frame_len_file[stream_id]);
            frame_len_file[stream_id] = NULL;
        }
    }
    return 0;
}

int32_t main(int32_t argc, char *argv[])
{
    DASH_IN_PARAM *dash_in_param = NULL;
    char *dash_config_file = NULL;
    int32_t ret = 0;

    if (argc < 3)
    {
        printf("Not enough input parameters !\n");
        return -1;
    }

    if (strncmp(argv[1], "-i", 2) == 0)
    {
        dash_config_file = argv[2];
        printf("Input configuration file is %s !\n", dash_config_file);
    }

    if (NULL == dash_config_file)
    {
        printf("There is no configuration file for the packing !\n");
        return -1;
    }

    dash_in_param = (DASH_IN_PARAM *)malloc(sizeof(DASH_IN_PARAM));
    if (NULL == dash_in_param)
    {
        printf("Failed to alloc memory for packing params !\n");
        return -1;
    }

    memset(dash_in_param, 0, sizeof(DASH_IN_PARAM));

    void *parser_hdl = ParsePackingParamInit();
    if (NULL == parser_hdl)
    {
        printf("Failed to create packing params parser !\n");
        free(dash_in_param);
        dash_in_param = NULL;
        return -1;
    }

    ret = ParsePackingParamParse(parser_hdl, dash_config_file, (void*)dash_in_param);
    if (ret)
    {
        printf("Error in parsing configuration file !\n");
        goto fail;
    }

    ret = PackStreams(dash_in_param);
    if (ret)
    {
        printf("Error in packing input streams !\n");
        goto fail;
    }

fail:
    if (dash_in_param)
    {
        free(dash_in_param);
        dash_in_param = NULL;
    }

    ParsePackingParamClose(parser_hdl);

    return ret;
}
