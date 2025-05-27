/*
 * Copyright (c) 2020, Intel Corporation
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

 *
 */

#include "AudioDecoder.h"

#ifndef AVIT_LOG_TAG
#define AVIT_LOG_TAG "avit log >>>>>>>>"
#endif

VCD_NS_BEGIN

//目前没使用
typedef struct AudioParams
{
    int freq;
    int channels;
    int64_t channel_layout;
    enum AVSampleFormat fmt;
    int frame_size;
    int bytes_per_sec;
} AudioParams;

AudioOutputer::AudioOutputer()
{
    m_agraph = NULL;
    m_in_audio_filter = NULL;
    m_out_audio_filter = NULL;
    mAudioDev = 0;
}

AudioOutputer::~AudioOutputer()
{
    UnInitialize();
}

bool AudioOutputer::Initialize(int32_t sample_rate, AVSampleFormat sample_fmt, int32_t channels, int64_t channel_layout)
{
    if(!InitFilter(sample_rate, sample_fmt, channels, channel_layout) )
    {
        LOG(ERROR) << AVIT_LOG_TAG << "InitFilter failed" << std::endl;
        return false;
    }

    if (!InitSDL())
    {
        LOG(ERROR) << AVIT_LOG_TAG << "InitSDL failed" << std::endl;
        return false;
    }

    return true;
}

void AudioOutputer::UnInitialize()
{
    UnInitSDL();

    UnInitFilter();

    AVFrame *frame = PopFrame();
    while (frame)
    {
        av_frame_free(&frame);
        frame = PopFrame();
    }
}

void AudioOutputer::AddOriginalFrame(AVFrame *frame)
{
    LOG(ERROR) << AVIT_LOG_TAG << "AddOriginalFrame" << std::endl;

    av_buffersrc_add_frame(m_in_audio_filter, frame);

    while (av_buffersink_get_frame_flags(m_out_audio_filter, frame, 0) >= 0)
    {
        AddFrame(av_frame_clone(frame));
    }

    av_frame_free(&frame);
}


AVFrame* AudioOutputer::PopFrame()
{
    ScopeLock lock(mFrameLock);
    if (mFrames.empty())
    {
        return NULL;
    }

    AVFrame* frame = mFrames.front();
    mFrames.pop_front();

    return frame;
}

void AudioOutputer::AddFrame(AVFrame *frame)
{
    LOG(ERROR) << AVIT_LOG_TAG << "AddFrame" << std::endl;

    AVFrame *drop = NULL;

    {
        ScopeLock lock(mFrameLock);
        if (mFrames.size() > 200)
        {
            LOG(ERROR) << AVIT_LOG_TAG << "too many frames: " << mFrames.size() << ", drop front" << std::endl;
            drop = mFrames.front();
            mFrames.pop_front();
        }

        mFrames.push_back(frame);
    }

    if (drop)
    {
        av_frame_free(&drop);
    }
}

bool AudioOutputer::InitFilter(int32_t sample_rate, AVSampleFormat sample_fmt, int32_t channels, int64_t channel_layout)
{
    static const enum AVSampleFormat sample_fmts[] = {AV_SAMPLE_FMT_S16, AV_SAMPLE_FMT_NONE};
    AVFilterContext *filt_asrc = NULL, *filt_asink = NULL;

    int64_t vaild_channel_layout = channel_layout;
    if (!vaild_channel_layout)
    {
        vaild_channel_layout = av_get_default_channel_layout(channels);
    }

    m_agraph = avfilter_graph_alloc();
    if (!m_agraph)
        goto fail;
    m_agraph->nb_threads = 0;

    char asrc_args[256];
    int pos = snprintf(asrc_args, sizeof(asrc_args),
                       "sample_rate=%d:sample_fmt=%s:channels=%d:time_base=%d/%d",
                       sample_rate, av_get_sample_fmt_name(sample_fmt),channels, 1, sample_rate);

    if (vaild_channel_layout)
    {
        snprintf(asrc_args + pos, sizeof(asrc_args) - pos,
                 ":channel_layout=0x%" PRIx64, vaild_channel_layout);
    }

    if (avfilter_graph_create_filter(&filt_asrc, avfilter_get_by_name("abuffer"), "abuffer", asrc_args, NULL, m_agraph) < 0)
    {
        goto fail;
    }

    if (avfilter_graph_create_filter(&filt_asink, avfilter_get_by_name("abuffersink"), "abuffersink", NULL, NULL, m_agraph) < 0)
    {
        goto fail;
    }

    av_opt_set_int_list(filt_asink, "sample_fmts", sample_fmts, AV_SAMPLE_FMT_NONE, AV_OPT_SEARCH_CHILDREN);
    av_opt_set_int(filt_asink, "all_channel_counts", 1, AV_OPT_SEARCH_CHILDREN);

    if (ConfigFilterGraph(m_agraph, NULL, filt_asrc, filt_asink) < 0)
    {
        goto fail;
    }

    m_in_audio_filter = filt_asrc;
    m_out_audio_filter = filt_asink;

    return true;

fail:
    UnInitFilter();
    return false;
}

void AudioOutputer::GetAudioSpec(SDL_AudioSpec& wanted_spec)
{
    int wanted_nb_channels = av_buffersink_get_channels(m_out_audio_filter);
    int wanted_channel_layout = av_buffersink_get_channel_layout(m_out_audio_filter);

    if (!wanted_channel_layout || wanted_nb_channels != av_get_channel_layout_nb_channels(wanted_channel_layout))
    {
        wanted_channel_layout = av_get_default_channel_layout(wanted_nb_channels);
        wanted_channel_layout &= ~AV_CH_LAYOUT_STEREO_DOWNMIX;
    }
    wanted_nb_channels = av_get_channel_layout_nb_channels(wanted_channel_layout);

    wanted_spec.channels = wanted_nb_channels;
    wanted_spec.freq = av_buffersink_get_sample_rate(m_out_audio_filter);
    wanted_spec.format = AUDIO_S16SYS;
    wanted_spec.silence = 0;
    wanted_spec.samples = FFMAX(512, 2 << av_log2(wanted_spec.freq / 30));
    wanted_spec.callback = SDLAudioCallback;
    wanted_spec.userdata = this;
}

void AudioOutputer::UnInitFilter()
{
    avfilter_graph_free(&m_agraph);
    m_out_audio_filter = NULL;
    m_in_audio_filter = NULL;
}

void AudioOutputer::UnInitSDL()
{
    if (mAudioDev)
    {
        SDL_CloseAudioDevice(mAudioDev);
        mAudioDev = 0;
    }
}

bool AudioOutputer::InitSDL()
{
    if (SDL_Init(SDL_INIT_TIMER | SDL_INIT_AUDIO))
    {
        LOG(ERROR) << AVIT_LOG_TAG << "SDL Could not initialize SDL" << SDL_GetError();
        return false;
    }

    struct AudioParams audio_hw_params;
    SDL_AudioSpec wanted_spec, spec;

    GetAudioSpec(wanted_spec);

    mAudioDev = SDL_OpenAudioDevice(NULL, 0, &wanted_spec, &spec, SDL_AUDIO_ALLOW_FREQUENCY_CHANGE | SDL_AUDIO_ALLOW_CHANNELS_CHANGE);
    if(!mAudioDev)
    {
        LOG(ERROR) << AVIT_LOG_TAG << "SDL_OpenAudioDevice failed, channels: " << (int)wanted_spec.channels << ", freq:" << (int)wanted_spec.freq << std::endl;
        return false;
    }

    if (spec.format != AUDIO_S16SYS)
    {
        LOG(ERROR) << AVIT_LOG_TAG << "SDL advised audio format is not supported!" << std::endl;
        return false;
    }

    if (spec.channels != wanted_spec.channels)
    {
        LOG(ERROR) << AVIT_LOG_TAG << "SDL advised channel count is not supported!" << std::endl;
    }

    audio_hw_params.fmt = AV_SAMPLE_FMT_S16;
    audio_hw_params.freq = spec.freq;
    //audio_hw_params.channel_layout = wanted_channel_layout;
    audio_hw_params.channels = spec.channels;
    audio_hw_params.frame_size = av_samples_get_buffer_size(NULL, audio_hw_params.channels, 1, audio_hw_params.fmt, 1);
    audio_hw_params.bytes_per_sec = av_samples_get_buffer_size(NULL, audio_hw_params.channels, audio_hw_params.freq, audio_hw_params.fmt, 1);

    SDL_PauseAudioDevice(mAudioDev, 0);

    return true;
}

int AudioOutputer::ConfigFilterGraph(AVFilterGraph *graph, const char *filtergraph,
                                     AVFilterContext *source_ctx, AVFilterContext *sink_ctx)
{
    int ret, i;
    int nb_filters = graph->nb_filters;
    AVFilterInOut *outputs = NULL, *inputs = NULL;

    if (filtergraph)
    {
        outputs = avfilter_inout_alloc();
        inputs = avfilter_inout_alloc();
        if (!outputs || !inputs)
        {
            ret = AVERROR(ENOMEM);
            goto fail;
        }

        outputs->name = av_strdup("in");
        outputs->filter_ctx = source_ctx;
        outputs->pad_idx = 0;
        outputs->next = NULL;

        inputs->name = av_strdup("out");
        inputs->filter_ctx = sink_ctx;
        inputs->pad_idx = 0;
        inputs->next = NULL;

        if ((ret = avfilter_graph_parse_ptr(graph, filtergraph, &inputs, &outputs, NULL)) < 0)
            goto fail;
    }
    else
    {
        if ((ret = avfilter_link(source_ctx, 0, sink_ctx, 0)) < 0)
            goto fail;
    }

    /* Reorder the filters to ensure that inputs of the custom filters are merged first */
    for (i = 0; i < graph->nb_filters - nb_filters; i++)
        FFSWAP(AVFilterContext *, graph->filters[i], graph->filters[i + nb_filters]);

    ret = avfilter_graph_config(graph, NULL);
fail:
    avfilter_inout_free(&outputs);
    avfilter_inout_free(&inputs);
    return ret;
}

void AudioOutputer::SDLAudioCallback(void *opaque, Uint8 *stream, int len)
{
    AudioOutputer *pAudioOutputer = (AudioOutputer *)opaque;

    static AVFrame *sCurrFrame = NULL;
    static int sCurrFrameReadByte = 0;

    int copy_size = 0;

    while (copy_size < len)
    {
        if (sCurrFrame)
        {
            int data_size = av_samples_get_buffer_size(NULL, sCurrFrame->channels,
                                                       sCurrFrame->nb_samples,
                                                       1, 1);

            int remain_bytes = data_size - sCurrFrameReadByte;

            LOG(ERROR) << ".............xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx-------------SDL_audio_callback remain_bytes: " << remain_bytes << ", channels: " << sCurrFrame->channels << ", format:" << sCurrFrame->format << ", size:" << sCurrFrame->linesize[0];

            int buf_size = (len - copy_size);

            if (buf_size >= remain_bytes)
            {
                memcpy(stream + copy_size, sCurrFrame->data[0] + sCurrFrameReadByte, remain_bytes);
                av_frame_unref(sCurrFrame);

                sCurrFrame = NULL;
                sCurrFrameReadByte = 0;
                copy_size += remain_bytes;

                LOG(ERROR) << ".............xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx-------------SDL_audio_callback copy: " << remain_bytes;
            }
            else
            {
                memcpy(stream + copy_size, sCurrFrame->data[0] + sCurrFrameReadByte, buf_size);
                sCurrFrameReadByte += buf_size;
                copy_size += buf_size;

                LOG(ERROR) << ".............xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx-------------SDL_audio_callback copy: " << buf_size;
            }
        }

        if (!sCurrFrame)
        {
            LOG(ERROR) << ".............xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx-------------SDL_audio_callback pop_frame";
            sCurrFrame = pAudioOutputer->PopFrame();
            if(!sCurrFrame)
            {
                usleep(1000 * 100);
            }
            sCurrFrameReadByte = 0;
        }
    }
}

AudioDecoder::AudioDecoder()
{
    mDecCtx = new AudioDecoderContext();
    mPkt = NULL;
    mIsFlushed = false;
}

AudioDecoder::~AudioDecoder()
{
    m_status = STATUS_STOPPED;
    //CloseDecoder();
    SAFE_DELETE(mDecCtx);
    if (mPkt)
    {
        av_packet_free(&mPkt);
        mPkt = NULL;
    }

    mIsFlushed = false;
}

void AudioDecoder::SetDecodeInfo(DecodeInfo& info)
{
    if (AudioCodec_AAC == info.audio_codec_type)
    {
        mDecCtx->codec_id = AV_CODEC_ID_AAC;
    }

    mDecCtx->decoder = avcodec_find_decoder(mDecCtx->codec_id);
    if (NULL == mDecCtx->decoder)
    {
        LOG(ERROR) << AVIT_LOG_TAG << "decoder find error!" << std::endl;
        return;
    }

    mDecCtx->codec_ctx = avcodec_alloc_context3(mDecCtx->decoder);
    if (NULL == mDecCtx->codec_ctx)
    {
        LOG(ERROR) << AVIT_LOG_TAG << "avcodec alloc context failed!" << std::endl;
        return;
    }

    mDecCtx->codec_ctx->sample_fmt = AV_SAMPLE_FMT_FLTP;
    mDecCtx->codec_ctx->sample_rate = info.sample_rate;
    mDecCtx->codec_ctx->channels = info.channels;
}

RenderStatus AudioDecoder::Initialize(int32_t id, Codec_Type codec, FrameHandler *handler, uint64_t startPts)
{
    SetStartPts(startPts);


    if (NULL == mDecCtx->decoder)
    {
        LOG(ERROR) << AVIT_LOG_TAG << "audio decoder is NULL!" << std::endl;
        return RENDER_ERROR;
    }

    if (NULL == mDecCtx->codec_ctx)
    {
        LOG(ERROR) << AVIT_LOG_TAG << "audio decoder ctx is NULL!" << std::endl;
        return RENDER_ERROR;
    }

    if (avcodec_open2(mDecCtx->codec_ctx, mDecCtx->decoder, NULL) < 0)
    {
        LOG(ERROR) << AVIT_LOG_TAG << "avcodec open failed!" << std::endl;
        return RENDER_ERROR;
    }

    if( !mAudioOutputer.Initialize(mDecCtx->codec_ctx->sample_rate, mDecCtx->codec_ctx->sample_fmt, mDecCtx->codec_ctx->channels, 0))
    {
        LOG(ERROR) << AVIT_LOG_TAG << "audio output init error!" << std::endl;
        return RENDER_ERROR;
    }

    StartThread();
    mIsFlushed = false;
    LOG(INFO) << "A new audio decoder is created!" << std::endl;
    LOG(ERROR) << ".............xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx-------------A new audio decoder is created: "<< std::endl;

    return RENDER_STATUS_OK;
}

void AudioDecoder::Run()
{
    m_status = STATUS_RUNNING;
    RenderStatus ret = RENDER_STATUS_OK;

    while (m_status != STATUS_STOPPED && m_status != STATUS_IDLE)
    {
        // when the status is set to pending
        if (m_status == STATUS_PENDING)
        {
            // flush decoder until all packets are popped.
            if (mDecCtx->get_size_of_packet() == 0 && !mIsFlushed)
            {
                ret = FlushDecoder();
                if (RENDER_STATUS_OK != ret)
                {
                    LOG(INFO) << "audio failed to flush decoder when status is pending!" << std::endl;
                }
                mIsFlushed = true;
                continue;
            }
        }
        if (0 == mDecCtx->get_size_of_packet())
        {
            usleep(1000);
            continue;
        }
        AVPacket *pkt_info = mDecCtx->pop_packet();

        LOG(ERROR) << ".............xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx-------------pop_packet: " << pkt_info << std::endl;

        if (NULL == pkt_info)
        {
            LOG(INFO) << "possible error since null packet has been pushed to queue" << std::endl;
            continue;
        }

        LOG(INFO) << "Now packet pts is " << pkt_info->pts << endl;

        //todo
        // if (pkt_info->bEOS)
        // {
        //     continue;
        // }

        ret = DecodeFrame(pkt_info);
        if (RENDER_STATUS_OK != ret)
        {
            LOG(INFO) << "Audio failed to decoder one frame" << std::endl;
        }

        av_packet_unref(pkt_info);

        SAFE_DELETE(pkt_info);
    }
}

RenderStatus AudioDecoder::SendPacket(DashPacket *packet)
{
    mPkt = av_packet_alloc();

    int size = packet->size;
    if (av_new_packet(mPkt, size) < 0)
    {
        av_packet_free(&mPkt);
        return RENDER_ERROR;
    }

    memcpy_s(mPkt->data, size, packet->buf, size);
    mPkt->size = packet->size;

    SAFE_FREE(packet->buf);
    SAFE_DELETE(packet->rwpk);

    LOG(ERROR) << ".............xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx-------------push_packet: " << mPkt << ", size: " << size << std::endl;
    mDecCtx->push_packet(mPkt);
    mPkt = NULL;
}

RenderStatus AudioDecoder::FlushDecoder()
{
    return DecodeFrame(NULL);
}

RenderStatus AudioDecoder::DecodeFrame(AVPacket *pkt)
{
    int32_t ret = 0;
    ret = avcodec_send_packet(mDecCtx->codec_ctx, pkt);
    if (ret < 0)
    {
        LOG(ERROR) << ".............xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx-------------audio Send packet failed! ret: " << ret << ", size:" << pkt->size << ", data:" << (void *)(pkt->data) << endl;
        return RENDER_DECODE_FAIL;
    }

    while (ret >= 0)
    {
        AVFrame *av_frame = av_frame_alloc();
        if (NULL == av_frame)
        {
            LOG(ERROR) << "alloc av frame failed in flushing frame!" << endl;
            return RENDER_DECODE_FAIL;
        }
        ret = avcodec_receive_frame(mDecCtx->codec_ctx, av_frame);
        if (ret < 0 || ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
        {
            LOG(INFO) << "Receive frame failed!" << std::endl;
            av_frame_free(&av_frame);
            return RENDER_DECODE_FAIL;
        }

        mAudioOutputer.AddOriginalFrame(av_frame);
    }

    return RENDER_STATUS_OK;
}

VCD_NS_END