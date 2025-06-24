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
//!
//! \file     AudioDecoder.h
//! \brief    Defines class for AudioDecoder derived from MediaDecoder.
//!

#ifndef _AUDIODEOCODER_H_
#define _AUDIODEOCODER_H_

#include "MediaDecoder.h"
#include "../../../utils/Threadable.h"

#include <list>

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libavutil/pixfmt.h>
#include <libavutil/samplefmt.h>
#include <libswscale/swscale.h>
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersrc.h>
#include <libavfilter/buffersink.h>
#include <libavutil/avutil.h>
#include <libavutil/opt.h>
}

#ifdef _ANDROID_OS_
#include <aaudio/AAudio.h>
#else
#include <SDL.h>
#define ANDROID_LOGD(...)
#define ANDROID_LOGE(...)
#endif

VCD_NS_BEGIN

class AudioOutputer
{
public:
     AudioOutputer();
     ~AudioOutputer();

     bool Initialize(int32_t sample_rate, AVSampleFormat sample_fmt, int32_t channels, int64_t channel_layout);
     void UnInitialize();

     void AddOriginalFrame(AVFrame *frame);
     AVFrame* PopFrame();

private:
     bool InitFilter(int32_t sample_rate, AVSampleFormat sample_fmt, int32_t channels, int64_t channel_layout);

#ifndef _ANDROID_OS_
     void GetAudioSpec(SDL_AudioSpec& wanted_spec);
#endif

     bool InitSDL();

     void UnInitFilter();
     void UnInitSDL();

     static int ConfigFilterGraph(AVFilterGraph *graph, const char *filtergraph,
                                      AVFilterContext *source_ctx, AVFilterContext *sink_ctx);

     static void SDLAudioCallback(void *opaque, uint8_t *stream, int len);

#ifdef _ANDROID_OS_
     static aaudio_data_callback_result_t AAudioStreamCallback(AAudioStream *stream,void *userData,void *audioData,int32_t numFrames);
#endif

     void AddFrame(AVFrame *frame);

private:
     AVFilterGraph* m_agraph;
     AVFilterContext* m_in_audio_filter;  // the first filter in the audio chain
     AVFilterContext* m_out_audio_filter; // the last filter in the audio chain

#ifdef _ANDROID_OS_
     AAudioStream *mAAudioStream;
#else
     SDL_AudioDeviceID mAudioDev;
#endif

     ThreadLock mFrameLock;
     std::list<AVFrame*> mFrames;

     AVFrame *mCurrFrame = NULL;
     int mCurrFrameReadByte = 0;

     uint64_t mPopFrameTime = 0;
     int      mHasDalayed = 0;
};

typedef struct AudioPacketInfo
{
     AVPacket *pkt = nullptr;
     bool bEOS = false;
     uint64_t pts = 0;
} AudioPacketInfo;

struct AudioDecoderContext
{
public:
     AudioDecoderContext()
     {
          codec_id = AV_CODEC_ID_NONE;
          codec_ctx = NULL;
          decoder = NULL;
          agraph = NULL;
          in_audio_filter = NULL;
          out_audio_filter = NULL;

          listPacket.clear();

          bPacketEOS = false;
     };

     ~AudioDecoderContext()
     {
          AudioPacketInfo *pkt_info = pop_packet();
          while (pkt_info)
          {
               av_packet_free(&pkt_info->pkt);
               SAFE_DELETE(pkt_info);

               pkt_info = pop_packet();
          }
     };

     void push_packet(AudioPacketInfo *pktInfo)
     {
          ScopeLock lock(PacketLock);
          listPacket.push_back(pktInfo);
     };

     AudioPacketInfo *pop_packet()
     {
          ScopeLock lock(PacketLock);
          AudioPacketInfo *pkt = NULL;
          if (!listPacket.empty())
          {
               pkt = listPacket.front();
               listPacket.pop_front();
          }
          return pkt;
     };


     uint32_t get_size_of_packet()
     {
          ScopeLock lock(PacketLock);
          uint32_t size = listPacket.size();
          return size;
     };


     AVCodecID codec_id;
     AVCodecContext *codec_ctx;
     AVCodec *decoder;
     AVFilterGraph *agraph;
     AVFilterContext* in_audio_filter;  // the first filter in the audio chain
     AVFilterContext* out_audio_filter; // the last filter in the audio chain

     ThreadLock PacketLock;
     std::list<AudioPacketInfo*> listPacket;

     bool bPacketEOS;
};

class AudioDecoder : public MediaDecoder, public Threadable
{
public:
     AudioDecoder();
     virtual ~AudioDecoder();
     //!
     //! \brief initialize a video decoder based on input information
     //!
     void SetDecodeInfo(DecodeInfo& info);
     virtual RenderStatus Initialize(int32_t id, Codec_Type codec, FrameHandler *handler, uint64_t startPts);

     //!
     //! \brief destroy a video decoder
     //!
     virtual RenderStatus Destroy(){return RENDER_STATUS_OK;};

     //!
     //! \brief  reset the decoder when decoding information changes
     //!
     virtual RenderStatus Reset(int32_t id, Codec_Type codec, uint64_t startPts) { return RENDER_STATUS_OK; };

     virtual bool IsReady(uint64_t pts) { return true; };

     //!
     //! \brief  udpate frame to destination with the callback class FrameHandler
     //!
     virtual RenderStatus UpdateFrame(uint64_t pts, int64_t *corr_pts, HeadPose* pose){return RENDER_STATUS_OK;};

     virtual void Run();

     //!
     //! \brief  send a coded packet to decoder
     //!
     virtual RenderStatus SendPacket(DashPacket* packet);

     //!
     //! \brief  pending a decoder
     //!
     virtual void Pending(){};

     //!
     //! \brief  get status of a decoder
     //!
     virtual ThreadStatus GetDecoderStatus(){ return STATUS_UNKNOWN; };

private:
     RenderStatus FlushDecoder();
     RenderStatus DecodeFrame(AVPacket *pkt);

private:
     DecodeInfo m_decodeInfo;
     AudioDecoderContext *mDecCtx;
     AVPacket *mPkt;
     AudioPacketInfo *mPktInfo;
     bool mIsFlushed;

     AudioOutputer mAudioOutputer;
};

VCD_NS_END

#endif