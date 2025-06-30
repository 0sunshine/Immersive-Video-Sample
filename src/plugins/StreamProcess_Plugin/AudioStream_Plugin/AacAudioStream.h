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
 */

//!
//! \file:   AacAudioStream.h
//! \brief:  AAC Audio stream process class definition
//!
//! Created on November 6, 2020, 6:04 AM
//!

#ifndef _AACAUDIOSTREAM_H_
#define _AACAUDIOSTREAM_H_

#include "../AudioStreamPluginAPI.h"
#include "../../../utils/safe_mem.h"
#include "../../../utils/OmafPackingLog.h"

//!
//! \class AacAudioStream
//! \brief Define the data and data operation for AAC audio stream
//!

class AacAudioStream : public AudioStream
{
public:
    //!
    //! \brief  Constructor
    //!
    AacAudioStream();
    //!
    //! \brief  Destructor
    //!
    virtual ~AacAudioStream();

       //!
    //! \brief  Initialize the audio stream
    //!
    //! \param  [in] streamIdx
    //!         the index of the audio in all streams
    //! \param  [in] bs
    //!         pointer to the BSBuffer information of
    //!         the audio stream, including sample rate
    //!         and bitrate, channel number and so on.
    //! \param  [in] initInfo
    //!         pointer to the initial information input
    //!         by the library interface
    //!
    //! \return int32_t
    //!         ERROR_NONE if success, else failed reason
    //!
    virtual int32_t Initialize(uint8_t streamIdx, BSBuffer *bs, InitialInfo *initInfo);

    //!
    //! \brief  Get the sample rate of the audio stream
    //!
    //! \return uint16_t
    //!         the sample rate of the audio stream
    //!
    virtual uint32_t GetSampleRate() { return m_iSampleRate; };

    //!
    //! \brief  Get the channel number of the audio stream
    //!
    //! \return uint8_t
    //!         the channel number of the audio stream
    //!
    virtual uint8_t GetChannelNum() {return m_iChannelNum; };

    //!
    //! \brief  Get the bit rate of the audio stream
    //!
    //! \return uint16_t
    //!         the bit rate of the audio stream, in the unit of kbps
    //!
    virtual uint16_t  GetBitRate(){ return m_iBitRate; };

    //!
    //! \brief  Add frame information for a new frame into
    //!         frame information list of the audio
    //!
    //! \param  [in] frameInfo
    //!         pointer to the frame information of the new frame
    //!
    //! \return int32_t
    //!         ERROR_NONE if success, else failed reason
    //!
    virtual int32_t AddFrameInfo(FrameBSInfo *frameInfo);

    //!
    //! \brief  Fetch the front frame information in frame
    //!         information list as current frame information
    //!
    //! \return void
    //!
    virtual void SetCurrFrameInfo();

    //!
    //! \brief  Get the current frame information
    //!
    //! \return FrameBSInfo*
    //!         the pointer to the current frame information
    //!
    virtual FrameBSInfo* GetCurrFrameInfo();

    //!
    //! \brief  Destroy current frame information
    //!
    //! \return void
    //!
    virtual void DestroyCurrFrameInfo();

    //!
    //! \brief  Destroy all frame information belong to current
    //!         segment
    //!
    //! \return void
    //!
    virtual void DestroyCurrSegmentFrames();

    //!
    //! \brief  Set the EOS status for the audio stream
    //!
    //! \param  [in] isEOS
    //!         the status will be set to EOS of the audio stream
    //!
    //! \return void
    //!
    virtual void SetEOS(bool isEOS) { m_isEOS = isEOS; };

    //!
    //! \brief  Get the EOS status of the audio stream
    //!
    //! \return bool
    //!         the EOS status of the audio stream
    //!
    virtual bool GetEOS() { return m_isEOS; };

    //!
    //! \brief  Add current frame to frames list for current
    //!         segment
    //!
    //! \return void
    //!
    virtual void AddFrameToSegment();

    //!
    //! \brief  Get current buffered frames number in
    //!         frame list which have not been written
    //!         into segments
    //!
    //! \return uint32_t
    //!         current buffered frames number
    //!
    virtual uint32_t GetBufferedFrameNum();

    virtual std::vector<uint8_t> GetPackedSpecCfg();

    virtual uint8_t  GetHeaderDataSize();

private:

    int32_t   m_iSampleRate;
    int8_t    m_iChannelNum;
    uint16_t  m_iBitRate;
    uint32_t  m_audioObjType;
    bool      m_isEOS;

    uint8_t       m_iStreamIdx;
    InitialInfo  *m_pInitInfo;

    std::list<FrameBSInfo*>   m_frameInfoList;    //!< frame information list of the audio
    std::list<FrameBSInfo*>   m_framesToOneSeg;   //!< frames will be written into one segment
    FrameBSInfo               *m_currFrameInfo;   //!< pointer to the current frame information
    std::mutex                m_mutex;

};

extern "C" AudioStream* Create();
extern "C" void Destroy(AudioStream* AS);

#endif /* _HEVCVIDEOSTREAM_H_ */
