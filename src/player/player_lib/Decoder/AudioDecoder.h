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
#include <libavfilter/buffersink.h>
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersink.h>
#include <libavutil/avutil.h>
#include <libavutil/opt.h>
}

VCD_NS_BEGIN

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

          listFrame.clear();
          listPacket.clear();

          bPacketEOS = false;
     };

     ~AudioDecoderContext()
     {
          AVFrame *frame = pop_frame();
          while (frame)
          {
               av_frame_free(&frame);

               frame = pop_frame();
          }

          AVPacket *pkt = pop_packet();
          while (frame)
          {
               av_packet_free(&pkt);

               pkt = pop_packet();
          }
     };

     void push_packet(AVPacket *pktInfo)
     {
          ScopeLock lock(PacketLock);
          listPacket.push_back(pktInfo);
     };

     AVPacket *pop_packet()
     {
          ScopeLock lock(PacketLock);
          AVPacket *pkt = NULL;
          if (!listPacket.empty())
          {
               pkt = listPacket.front();
               listPacket.pop_front();
          }
          return pkt;
     };

     void push_frame(AVFrame *frame)
     {
          ScopeLock lock(FrameLock);
          listFrame.push_back(frame);
     };

     AVFrame *pop_frame()
     {
          ScopeLock lock(FrameLock);
          AVFrame *frame = NULL;
          if (!listFrame.empty())
          {
               frame = listFrame.front();
               listFrame.pop_front();
          }
          return frame;
     };

     uint32_t get_size_of_packet()
     {
          ScopeLock lock(PacketLock);
          uint32_t size = listPacket.size();
          return size;
     };

     uint32_t get_size_of_frame()
     {
          ScopeLock lock(FrameLock);
          uint32_t size = listFrame.size();
          return size;
     };

     AVCodecID codec_id;
     AVCodecContext *codec_ctx;
     AVCodec *decoder;
     AVFilterGraph *agraph;
     AVFilterContext* in_audio_filter;  // the first filter in the audio chain
     AVFilterContext* out_audio_filter; // the last filter in the audio chain

     std::list<AVFrame*> listFrame;
     std::list<AVPacket*> listPacket;

     ThreadLock FrameLock;
     ThreadLock PacketLock;

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
     AudioDecoderContext *mDecCtx;
     AVPacket *mPkt;
     bool mIsFlushed;

     bool mIsInitAudioOutput;
};

VCD_NS_END

#endif