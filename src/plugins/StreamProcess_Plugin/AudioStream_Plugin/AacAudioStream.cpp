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
//! \file:   AacAudioStream.cpp
//!
//! Created on November 6, 2020, 6:04 AM
//!

#include "AacAudioStream.h"
#include "error.h"

AacAudioStream::AacAudioStream()
{
    m_currFrameInfo = NULL;
    m_isEOS = false;
}
    
AacAudioStream::~AacAudioStream()
{
    std::list<FrameBSInfo*>::iterator it1;
    for (it1 = m_frameInfoList.begin(); it1 != m_frameInfoList.end();)
    {
        FrameBSInfo *frameInfo = *it1;
        if (frameInfo)
        {
            DELETE_ARRAY(frameInfo->data);

            delete frameInfo;
            frameInfo = NULL;
        }

        it1 = m_frameInfoList.erase(it1);
    }
    m_frameInfoList.clear();

    std::list<FrameBSInfo*>::iterator it2;
    for (it2 = m_framesToOneSeg.begin(); it2 != m_framesToOneSeg.end();)
    {
        FrameBSInfo *frameInfo = *it2;
        if (frameInfo)
        {
            DELETE_ARRAY(frameInfo->data);

            delete frameInfo;
            frameInfo = NULL;
        }

        it2 = m_framesToOneSeg.erase(it2);
    }
    m_framesToOneSeg.clear();
}

int32_t AacAudioStream::Initialize(uint8_t streamIdx, BSBuffer *bs, InitialInfo *initInfo)
{
    m_iStreamIdx = streamIdx;
    m_pInitInfo = initInfo;
    m_iSampleRate = bs->sampleRate;
    m_iChannelNum = bs->channelNum;
    m_iBitRate = bs->bitRate;
    m_audioObjType = bs->audioObjType;
    OMAF_LOG(LOG_INFO, "AacAudioStream::Initialize Audio sample rate %d\n", m_iSampleRate);
    OMAF_LOG(LOG_INFO, "AacAudioStream::Initialize Audio channel number %d\n", m_iChannelNum);
    OMAF_LOG(LOG_INFO, "AacAudioStream::Initialize Audio bit rate %d\n", m_iBitRate);
    OMAF_LOG(LOG_INFO, "AacAudioStream::Initialize Audio obj type %d\n", m_audioObjType);
    
    return 0;
}

void AacAudioStream::SetCurrFrameInfo()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_frameInfoList.size() > 0)
    {
        m_currFrameInfo = m_frameInfoList.front();
        m_frameInfoList.pop_front();
    }
}

FrameBSInfo* AacAudioStream::GetCurrFrameInfo()
{
    return m_currFrameInfo;
}

int32_t AacAudioStream::AddFrameInfo(FrameBSInfo *frameInfo)
{
    //pkt.data[2] == 0x54 && pkt.data[3] == 0x6d && pkt.data[8] == 0x20
    if(frameInfo->data[2] == 0x54 && frameInfo->data[3] == 0x6d && frameInfo->data[8] == 0x20 && frameInfo->data[9] == 0x75 && frameInfo->data[10] == 0xae && frameInfo->data[14] == 0xc7)
    {
      OMAF_LOG(LOG_INFO, " >>>> AddFrameInfo want wantwantwant fram len: %d\n", frameInfo->dataSize);
    }
    
    OMAF_LOG(LOG_INFO, "AddFrameInfo fram len: %d\n", frameInfo->dataSize);
    OMAF_LOG(LOG_INFO, "AddFrameInfo is key: %d\n", frameInfo->isKeyFrame);
    if (!frameInfo || !(frameInfo->data))
        return OMAF_ERROR_NULL_PTR;

    if (!frameInfo->dataSize)
        return OMAF_ERROR_DATA_SIZE;

    FrameBSInfo *newFrameInfo = new FrameBSInfo;
    if (!newFrameInfo)
        return OMAF_ERROR_NULL_PTR;

    memset_s(newFrameInfo, sizeof(FrameBSInfo), 0);

    uint8_t *localData = new uint8_t[frameInfo->dataSize];
    if (!localData)
    {
        delete newFrameInfo;
        newFrameInfo = NULL;
        return OMAF_ERROR_NULL_PTR;
    }
    memcpy_s(localData, frameInfo->dataSize, frameInfo->data, frameInfo->dataSize);

    newFrameInfo->data = localData;
    newFrameInfo->dataSize = frameInfo->dataSize;
    newFrameInfo->pts = frameInfo->pts;
    newFrameInfo->isKeyFrame = frameInfo->isKeyFrame;

    std::lock_guard<std::mutex> lock(m_mutex);
    m_frameInfoList.push_back(newFrameInfo);

    return ERROR_NONE;
}

uint32_t AacAudioStream::GetBufferedFrameNum() 
{
    uint32_t bufferedFrmNum = 0;
    std::lock_guard<std::mutex> lock(m_mutex);
    bufferedFrmNum = m_frameInfoList.size();
    return bufferedFrmNum;
}

void AacAudioStream::AddFrameToSegment()
{
    m_framesToOneSeg.push_back(m_currFrameInfo);
    m_currFrameInfo = NULL;
 }

void AacAudioStream::DestroyCurrFrameInfo()
{
    std::list<FrameBSInfo*>::iterator it;
    for (it = m_framesToOneSeg.begin(); it != m_framesToOneSeg.end(); )
    {
        FrameBSInfo *frameInfo = *it;
        if (frameInfo)
        {
            DELETE_ARRAY(frameInfo->data);
            delete frameInfo;
            frameInfo = NULL;
        }

        //m_framesToOneSeg.erase(it++);
        it = m_framesToOneSeg.erase(it);

    }
    m_framesToOneSeg.clear();
}

 void AacAudioStream::DestroyCurrSegmentFrames()
 {
    if (m_currFrameInfo)
    {
        DELETE_ARRAY(m_currFrameInfo->data);

        delete m_currFrameInfo;
        m_currFrameInfo = NULL;
    }
 }

 std::vector<uint8_t> AacAudioStream::GetPackedSpecCfg()
 {
    //只会在构造track时，调用一次。
    //ffmpeg读mp4文件时，speccfg放在AVStream->AVCodecContext->extradata。长度为2.
    //ffmpeg读ts文件时，长度为0.
    std::vector<uint8_t> packedAudioSpecCfg;
    uint32_t objectType = 2;//m_audioObjType; //AAC LC 默认只支持AAC LC
    char dsi[2] = {0};
    uint32_t samplingFrequencyIdx = 0;
    switch (m_iSampleRate)
    {
    case 48000: 
        samplingFrequencyIdx = 3;
        break;
    case 44100: 
        samplingFrequencyIdx = 4;
        break;
    default:
        break;
    }

    dsi[0] = (objectType << 3) | (samplingFrequencyIdx>>1);
    dsi[1] = ((samplingFrequencyIdx&1) << 7) | ((uint32_t)m_iChannelNum << 3);
    packedAudioSpecCfg.push_back(uint8_t(dsi[0]));
    packedAudioSpecCfg.push_back(uint8_t(dsi[1]));

    OMAF_LOG(LOG_INFO, "AacAudioStream::GetPackedSpecCfg byte[0]: %d\n", (uint8_t)dsi[0]);
    OMAF_LOG(LOG_INFO, "AacAudioStream::GetPackedSpecCfg byte[1]: %d\n", (uint8_t)dsi[1]);
    
    
    return packedAudioSpecCfg;
 }

 uint8_t  AacAudioStream::GetHeaderDataSize()
 {
    //如果ffmpeg读的是mp4文件，则header=0；如果读取的是ts文件，则header为adts头，header=7
    //默认ffmpeg读取mp4文件进行播放。
    return 0;
 }

extern "C" AudioStream* Create()
{
    AacAudioStream *aacAS = new AacAudioStream;
    return (AudioStream*)(aacAS);
}

extern "C" void Destroy(AudioStream* AS)
{
    delete AS;
    AS = NULL;
}
