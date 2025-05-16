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

extern "C"
{
#include <SDL.h>
}

VCD_NS_BEGIN

AudioDecoder::AudioDecoder()
{
    mDecCtx = new AudioDecoderContext();
    mPkt = NULL;
    mIsFlushed = false;
    mIsInitAudioOutput = false;
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

typedef struct AudioParams
{
    int freq;
    int channels;
    int64_t channel_layout;
    enum AVSampleFormat fmt;
    int frame_size;
    int bytes_per_sec;
} AudioParams;

static inline int64_t get_valid_channel_layout(int64_t channel_layout, int channels)
{
    if (channel_layout && av_get_channel_layout_nb_channels(channel_layout) == channels)
        return channel_layout;
    else
        return 0;
}

static int configure_filtergraph(AVFilterGraph *graph, const char *filtergraph,
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

RenderStatus AudioDecoder::Initialize(int32_t id, Codec_Type codec, FrameHandler *handler, uint64_t startPts)
{
    SetStartPts(startPts);

    mDecCtx->codec_id = AV_CODEC_ID_AAC;

    mDecCtx->decoder = avcodec_find_decoder(mDecCtx->codec_id);
    if (NULL == mDecCtx->decoder)
    {
        LOG(ERROR) << "decoder find error!" << std::endl;
        return RENDER_ERROR;
    }

    mDecCtx->codec_ctx = avcodec_alloc_context3(mDecCtx->decoder);
    if (NULL == mDecCtx->codec_ctx)
    {
        LOG(ERROR) << "avcodec alloc context failed!" << std::endl;
        return RENDER_ERROR;
    }

    mDecCtx->codec_ctx->profile = 1;
    mDecCtx->codec_ctx->level = -99;
    mDecCtx->codec_ctx->sample_fmt = AV_SAMPLE_FMT_FLTP;
    mDecCtx->codec_ctx->sample_rate = 48000;
    mDecCtx->codec_ctx->channels = 2;

    if (avcodec_open2(mDecCtx->codec_ctx, mDecCtx->decoder, NULL) < 0)
    {
        LOG(ERROR) << "avcodec open failed!" << std::endl;
        return RENDER_ERROR;
    }

    {
        mDecCtx->agraph = avfilter_graph_alloc();
        mDecCtx->agraph->nb_threads = 0;

        int64_t channel_layout = get_valid_channel_layout(mDecCtx->codec_ctx->channel_layout, mDecCtx->codec_ctx->channels);

        static const enum AVSampleFormat sample_fmts[] = {AV_SAMPLE_FMT_S16, AV_SAMPLE_FMT_NONE};
        AVFilterContext *filt_asrc = NULL, *filt_asink = NULL;
        char asrc_args[256];
        int pos = snprintf(asrc_args, sizeof(asrc_args),
                        "sample_rate=%d:sample_fmt=%s:channels=%d:time_base=%d/%d",
                        mDecCtx->codec_ctx->sample_rate, av_get_sample_fmt_name(mDecCtx->codec_ctx->sample_fmt),
                        mDecCtx->codec_ctx->channels,
                        1, mDecCtx->codec_ctx->sample_rate);

        if (channel_layout)
        {
            snprintf(asrc_args + pos, sizeof(asrc_args) - pos,
                    ":channel_layout=0x%" PRIx64, channel_layout);
        }

        int ret = avfilter_graph_create_filter(&filt_asrc,
                                            avfilter_get_by_name("abuffer"), "ffplay_abuffer",
                                            asrc_args, NULL, mDecCtx->agraph);

        ret = avfilter_graph_create_filter(&filt_asink,
                                        avfilter_get_by_name("abuffersink"), "ffplay_abuffersink",
                                        NULL, NULL, mDecCtx->agraph);

        av_opt_set_int_list(filt_asink, "sample_fmts", sample_fmts, AV_SAMPLE_FMT_NONE, AV_OPT_SEARCH_CHILDREN);
        av_opt_set_int(filt_asink, "all_channel_counts", 1, AV_OPT_SEARCH_CHILDREN);
        ret = configure_filtergraph(mDecCtx->agraph, NULL, filt_asrc, filt_asink);

        mDecCtx->in_audio_filter = filt_asrc;
        mDecCtx->out_audio_filter = filt_asink;
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

static void sdl_audio_callback(void *opaque, Uint8 *stream, int len)
{
    AudioDecoderContext *pDecCtx = (AudioDecoderContext *)opaque;

    LOG(ERROR) << ".............xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx-------------SDL_audio_callback, len: " << len;

    static AVFrame* sCurrFrame = NULL;
    static int sCurrFrameReadByte = 0;

    int copy_size = 0;

    while(copy_size < len)
    {
        if(sCurrFrame)
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
            sCurrFrame = pDecCtx->pop_frame();
            sCurrFrameReadByte = 0;
        }
    }
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

        // LOG(ERROR) << ".............xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx-------------avcodec_receive_frame, nb_samples: " << av_frame->nb_samples << ", sample_rate: \
        // " << av_frame->sample_rate
        //            << ", ctx sample_rate" << this->mDecCtx->codec_ctx->sample_rate << ", " << std::endl;

        LOG(INFO) << "[FrameSequences][Decode]: Push one decoded audio frame at:" << av_frame->pts << ", frame fifo size is " << mDecCtx->get_size_of_frame() << endl;
        

        if (!mIsInitAudioOutput)
        {
            LOG(ERROR) << ".............xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx-------------SDL InitAudioOutput";
            if (SDL_Init(SDL_INIT_TIMER | SDL_INIT_AUDIO))
            {
                LOG(ERROR) << ".............xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx-------------SDL Could not initialize SDL" << SDL_GetError();
            }
            else
            {
                SDL_AudioDeviceID audio_dev;
                struct AudioParams audio_hw_params;

                int wanted_nb_channels = 0;
                int wanted_sample_rate = 0;
                int64_t wanted_channel_layout = 0;

                {
                    wanted_sample_rate = av_buffersink_get_sample_rate(mDecCtx->out_audio_filter);
                    wanted_nb_channels = av_buffersink_get_channels(mDecCtx->out_audio_filter);
                    wanted_channel_layout = av_buffersink_get_channel_layout(mDecCtx->out_audio_filter);
                }

                SDL_AudioSpec wanted_spec, spec;
                static const int next_nb_channels[] = {0, 0, 1, 6, 2, 6, 4, 6};
                static const int next_sample_rates[] = {0, 44100, 48000, 96000, 192000};
                int next_sample_rate_idx = FF_ARRAY_ELEMS(next_sample_rates) - 1;

                if (!wanted_channel_layout || wanted_nb_channels != av_get_channel_layout_nb_channels(wanted_channel_layout))
                {
                    wanted_channel_layout = av_get_default_channel_layout(wanted_nb_channels);
                    wanted_channel_layout &= ~AV_CH_LAYOUT_STEREO_DOWNMIX;
                }
                wanted_nb_channels = av_get_channel_layout_nb_channels(wanted_channel_layout);
                wanted_spec.channels = wanted_nb_channels;
                wanted_spec.freq = wanted_sample_rate;
                if (wanted_spec.freq <= 0 || wanted_spec.channels <= 0)
                {
                    LOG(ERROR) << ".............xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx-------------Invalid sample rate or channel count";
                }
                while (next_sample_rate_idx && next_sample_rates[next_sample_rate_idx] >= wanted_spec.freq)
                    next_sample_rate_idx--;
                wanted_spec.format = AUDIO_S16SYS;
                wanted_spec.silence = 0;
                wanted_spec.samples = FFMAX(512, 2 << av_log2(wanted_spec.freq / 30));
                wanted_spec.callback = sdl_audio_callback;
                wanted_spec.userdata = this->mDecCtx;
                while (!(audio_dev = SDL_OpenAudioDevice(NULL, 0, &wanted_spec, &spec, SDL_AUDIO_ALLOW_FREQUENCY_CHANGE | SDL_AUDIO_ALLOW_CHANNELS_CHANGE)))
                {
                    wanted_spec.channels = next_nb_channels[FFMIN(7, wanted_spec.channels)];
                    if (!wanted_spec.channels)
                    {
                        wanted_spec.freq = next_sample_rates[next_sample_rate_idx--];
                        wanted_spec.channels = wanted_nb_channels;
                        if (!wanted_spec.freq)
                        {
                        }
                    }
                    wanted_channel_layout = av_get_default_channel_layout(wanted_spec.channels);
                }
                if (spec.format != AUDIO_S16SYS)
                {
                    LOG(ERROR) << ".............xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx-------------SDL advised audio format is not supported!";
                }
                if (spec.channels != wanted_spec.channels)
                {
                    wanted_channel_layout = av_get_default_channel_layout(spec.channels);
                    if (!wanted_channel_layout)
                    {
                        LOG(ERROR) << ".............xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx-------------SDL advised channel count is not supported!";
                    }
                }

                audio_hw_params.fmt = AV_SAMPLE_FMT_S16;
                audio_hw_params.freq = spec.freq;
                audio_hw_params.channel_layout = wanted_channel_layout;
                audio_hw_params.channels = spec.channels;
                audio_hw_params.frame_size = av_samples_get_buffer_size(NULL, audio_hw_params.channels, 1, audio_hw_params.fmt, 1);
                audio_hw_params.bytes_per_sec = av_samples_get_buffer_size(NULL, audio_hw_params.channels, audio_hw_params.freq, audio_hw_params.fmt, 1);

                SDL_PauseAudioDevice(audio_dev, 0);

                //--------------------

                // static const int next_nb_channels[] = {0, 0, 1, 6, 2, 6, 4, 6};
                // static const int next_sample_rates[] = {0, 44100, 48000, 96000, 192000};
                // int next_sample_rate_idx = FF_ARRAY_ELEMS(next_sample_rates) - 1;
                // SDL_AudioDeviceID audio_dev;

                // SDL_AudioSpec wanted_spec, spec;

                
                // wanted_spec.channels = wanted_nb_channels;
                // wanted_spec.freq = av_frame->sample_rate;

                // while (next_sample_rate_idx && next_sample_rates[next_sample_rate_idx] >= wanted_spec.freq)
                //     next_sample_rate_idx--;

                // wanted_spec.format = AUDIO_S16SYS;
                // wanted_spec.silence = 0;
                // wanted_spec.samples = FFMAX(512, 2 << av_log2(av_frame->sample_rate / 30));
                // wanted_spec.callback = sdl_audio_callback;
                // wanted_spec.userdata = this->mDecCtx;

                // audio_dev = SDL_OpenAudioDevice(NULL, 0, &wanted_spec, &spec, SDL_AUDIO_ALLOW_FREQUENCY_CHANGE | SDL_AUDIO_ALLOW_CHANNELS_CHANGE);
                // LOG(ERROR) << ".............xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx-------------SDL_OpenAudioDevice " << SDL_GetError();
                // // while (!(audio_dev = SDL_OpenAudioDevice(NULL, 0, &wanted_spec, &spec, SDL_AUDIO_ALLOW_FREQUENCY_CHANGE | SDL_AUDIO_ALLOW_CHANNELS_CHANGE)))
                // // {
                // //     av_log(NULL, AV_LOG_WARNING, "SDL_OpenAudio (%d channels, %d Hz): %s\n",
                // //            wanted_spec.channels, wanted_spec.freq, SDL_GetError());
                // //     wanted_spec.channels = next_nb_channels[FFMIN(7, wanted_spec.channels)];
                // //     if (!wanted_spec.channels)
                // //     {
                // //         wanted_spec.freq = next_sample_rates[next_sample_rate_idx--];
                // //         wanted_spec.channels = wanted_nb_channels;
                // //         if (!wanted_spec.freq)
                // //         {
                // //             LOG(ERROR) << ".............xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx-------------SDL No more combinations to try, err: " << SDL_GetError();
                // //         }
                // //     }
                // // }

                // SDL_PauseAudioDevice(audio_dev, 0);
            }
            mIsInitAudioOutput = true;
        }

        av_buffersrc_add_frame(mDecCtx->in_audio_filter, av_frame);

        while (av_buffersink_get_frame_flags(mDecCtx->out_audio_filter, av_frame, 0) >= 0)
        {
            mDecCtx->push_frame(av_frame_clone(av_frame));
        }

        av_frame_free(&av_frame);
    }



    return RENDER_STATUS_OK;
}

VCD_NS_END