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

#include <ctype.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <list>
#include <map>
#include <pthread.h>
#include <sys/time.h>

#ifdef __cplusplus
extern "C" {
#endif
#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libavutil/opt.h"
#include "libavutil/samplefmt.h"
#include "libavutil/timestamp.h"
#include "libavutil/imgutils.h"
#include "libswresample/swresample.h"
#include "libswscale/swscale.h"
#ifdef __cplusplus
};
#endif

#include "./ParseEncodingParam/EncodingParamsDef.h"
#include "./ParseEncodingParam/ParseEncodingParamAPI.h"

typedef struct
{
    EncodingCfg            *encCfg;
    AVCodecContext         *codecCtx;
    uint64_t               sendFrameCnt;
    uint64_t               getPacketCnt;
    pthread_t              threadIdx;
    pthread_mutex_t        encMutex;
    FILE                   *outFP;
} EncThreadCtx;

AVFormatContext *inFmtCtx = NULL;
int32_t inLoopCnt = 1;
AVCodecContext  *decCodecCtx = NULL;
pthread_mutex_t decMutex;
char *inFileName = NULL;
int32_t threadType = 0;
int32_t threadCnt  = 0;
std::map<uint32_t, std::pair<uint32_t, AVFrame*>> framesList; //<frameIdxInDisplayOrder, <refCnt, AVFrame*>>
int32_t videoStrIdx = -1;
int32_t outVideosNum = 0;
static uint64_t decFrameCnt = 0;
static uint64_t delFrameCnt = 0;
static bool     decDone = false;
OutputParam *outParams = NULL;
void *parserHdl = NULL;
std::list<EncThreadCtx*> encThreadCtxs;
uint64_t bufferedFramesNum = 0;  // buffered decoded frames number in framesList, when decoded frames number is larger than it, then blocking the decoding

std::map<uint32_t, uint8_t *> bufList;
char * input_yuv_format = NULL;
int frame_num = 0;
int frame_cnt = 0;
std::map<int, AVPacket *> pktList;

int32_t OpenInput()
{
    int32_t ret = 0;
    uint32_t i = 0;

    if ((ret = avformat_open_input(&inFmtCtx, inFileName, NULL, NULL)) < 0)
    {
        av_log(NULL, AV_LOG_ERROR, "Failed to open input file !\n");
        return ret;
    }

    if ((ret = avformat_find_stream_info(inFmtCtx, NULL)) < 0)
    {
        av_log(NULL, AV_LOG_ERROR, "Failed to find stream info !\n");
        return ret;
    }

    for (i = 0; i < inFmtCtx->nb_streams; i++)
    {
        AVStream *stream = NULL;
        stream = inFmtCtx->streams[i];

        if (stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
        {
            videoStrIdx = i;
            AVCodec *codec = NULL;
            if (stream->codecpar->codec_id == AV_CODEC_ID_H264)
            {
                codec = avcodec_find_decoder(AV_CODEC_ID_H264);
                if (!codec)
                {
                    av_log(NULL, AV_LOG_ERROR, "Failed to find H264 decoder !\n");
                    return -1;
                }

                decCodecCtx = avcodec_alloc_context3(codec);
                avcodec_parameters_to_context(decCodecCtx, stream->codecpar);
                decCodecCtx->thread_count = threadCnt;
            }
            else if (stream->codecpar->codec_id == AV_CODEC_ID_HEVC)
            {
                codec = avcodec_find_decoder_by_name("libopenhevc");
                if (!codec)
                {
                    av_log(NULL, AV_LOG_ERROR, "Failed to find OpenHEVC decoder !\n");
                    return -1;
                }

                decCodecCtx = avcodec_alloc_context3(codec);
                avcodec_parameters_to_context(decCodecCtx, stream->codecpar);
                char typeStr[1024] = { 0 };
                snprintf(typeStr, 1024, "%d", threadType);
                av_opt_set(decCodecCtx->priv_data, "thread_type", typeStr, 0);
                char cntStr[1024] = { 0 };
                snprintf(cntStr, 1024, "%d", threadCnt);
                av_opt_set(decCodecCtx->priv_data, "thread_count", cntStr, 0);
            }

            ret = avcodec_open2(decCodecCtx, codec, NULL);
            if (ret < 0)
            {
                av_log(NULL, AV_LOG_ERROR, "Failed to open the decoder for stream %u !\n", i);
                return ret;
            }

            break;
        }
    }

    if (videoStrIdx == -1)
    {
        av_log(NULL, AV_LOG_ERROR, "No video in input file !\n");
        return -1;
    }

    av_dump_format(inFmtCtx, 0, inFileName, 0);
    return 0;
}

int32_t OpenOutput(AVCodecContext **codecCtx, EncodingCfg *encCfg, FILE **outFP)
{
    int32_t ret = 0;

    const char *encoderName = "distributed_encoder";
    AVCodec *encCodec = NULL;
    encCodec = avcodec_find_encoder_by_name(encoderName);
    if (!encCodec)
    {
        av_log(NULL, AV_LOG_ERROR, "Failed to find distributed encoder !\n");
        return -1;
    }

    *codecCtx = NULL;
    *codecCtx = avcodec_alloc_context3(encCodec);
    if (!(*codecCtx))
    {
        av_log(NULL, AV_LOG_ERROR, "NULL encode codec context !\n");
        return -1;
    }

    AVCodecContext *encCodecCtx = *codecCtx;
    encCodecCtx->width = encCfg->width;
    encCodecCtx->height = encCfg->height;
    encCodecCtx->gop_size = encCfg->gopSize;
    encCodecCtx->bit_rate = encCfg->bitRate;
    encCodecCtx->pix_fmt  = AV_PIX_FMT_YUV420P;
    encCodecCtx->time_base.num = encCfg->frameRate;
    encCodecCtx->time_base.den = 1;
    encCodecCtx->framerate.num = encCfg->frameRate;
    encCodecCtx->framerate.den = 1;
    av_opt_set(encCodecCtx->priv_data, "input_type", encCfg->inputType, 0);
    av_opt_set(encCodecCtx->priv_data, "input_codec", encCfg->inputCodec, 0);
    av_opt_set(encCodecCtx->priv_data, "config_file", encCfg->cfgFile, 0);
    av_opt_set(encCodecCtx->priv_data, "vui", encCfg->vuiInfo, 0);
    av_opt_set(encCodecCtx->priv_data, "hielevel", encCfg->hierarchicalLevel, 0);
    av_opt_set(encCodecCtx->priv_data, "la_depth", encCfg->laDepth, 0);
    av_opt_set(encCodecCtx->priv_data, "preset", encCfg->encMode, 0);
    av_opt_set(encCodecCtx->priv_data, "rc", encCfg->rcMode, 0);
    av_opt_set(encCodecCtx->priv_data, "sc_detection", encCfg->scDetection, 0);
    av_opt_set(encCodecCtx->priv_data, "tune", encCfg->tune, 0);
    av_opt_set(encCodecCtx->priv_data, "qp", encCfg->qp, 0);
    av_opt_set(encCodecCtx->priv_data, "hdr", encCfg->hdr, 0);
    av_opt_set(encCodecCtx->priv_data, "asm_type", encCfg->asmType, 0);
    av_opt_set(encCodecCtx->priv_data, "forced-idr", encCfg->forcedIdr, 0);
    av_opt_set(encCodecCtx->priv_data, "gpucopy", encCfg->gpuCopy, 0);
    av_opt_set(encCodecCtx->priv_data, "aud", encCfg->aud, 0);
    av_opt_set(encCodecCtx->priv_data, "profile", encCfg->profile, 0);
    av_opt_set(encCodecCtx->priv_data, "level", encCfg->level, 0);
    av_opt_set(encCodecCtx->priv_data, "pred_struct", encCfg->predStructure, 0);
    av_opt_set(encCodecCtx->priv_data, "bl_mode", encCfg->baseLayerSwitchMode, 0);
    av_opt_set(encCodecCtx->priv_data, "tile_row", encCfg->tileRows, 0);
    av_opt_set(encCodecCtx->priv_data, "tile_column", encCfg->tileColumns, 0);
    av_opt_set(encCodecCtx->priv_data, "in_parallel", encCfg->inParallel, 0);
    av_opt_set(encCodecCtx->priv_data, "external_log_flag", encCfg->needExternalLog, 0);
    av_opt_set(encCodecCtx->priv_data, "min_log_level", encCfg->minLogLevel, 0);
    av_opt_set(encCodecCtx->priv_data, "proj_type", encCfg->projType, 0);
    av_opt_set(encCodecCtx->priv_data, "input_yuv_format", input_yuv_format, 0);

    ret = avcodec_open2(encCodecCtx, encCodec, NULL);
    if (ret < 0)
    {
        av_log(NULL, AV_LOG_ERROR, "Failed to open distributed encoder !\n");
        return ret;
    }

    *outFP = NULL;
    *outFP = fopen(encCfg->outFileName, "wb+");
    if (!(*outFP))
    {
        av_log(NULL, AV_LOG_ERROR, "Failed to open output file !\n");
        return -1;
    }

    return 0;
}

int32_t DecodeOneFrame()
{
    int32_t ret = 0;
    AVFrame *oneFrame = NULL;

    while (1)
    {
        pthread_mutex_lock(&decMutex);
        if (framesList.size() > bufferedFramesNum)
        {
            std::map<uint32_t, std::pair<uint32_t, AVFrame*>>::iterator itr;
            for (itr = framesList.begin(); itr != framesList.end();)
            {
                uint32_t frameIdx = itr->first;
                std::pair<uint32_t, AVFrame*> frontFrame = itr->second;
                AVFrame *currFrame = frontFrame.second;

                if (frameIdx != delFrameCnt)
                    av_log(NULL, AV_LOG_ERROR, "Mismatch frameIdx %d vs delFrameCnt %ld ~~~~~ \n", frameIdx, delFrameCnt);

                int32_t sendDoneNum = 0;
                std::list<EncThreadCtx*>::iterator iter1;
                for (iter1 = encThreadCtxs.begin(); iter1 != encThreadCtxs.end(); iter1++)
                {
                    EncThreadCtx *encThreadCtx = *iter1;
                    if (encThreadCtx->sendFrameCnt > frameIdx)
                    {
                        sendDoneNum++;
                    }
                }
                if (sendDoneNum == outVideosNum)
                {//here means the frame has been used by all encoding thread
                    framesList.erase(itr++);
                    av_frame_free(&currFrame);
                    currFrame = NULL;
                    delFrameCnt++;
                }
                else
                    break;
            }
        }
        if (framesList.size() > bufferedFramesNum)
        {
            pthread_mutex_unlock(&decMutex);
            usleep(1000);
            continue;
        }
        else
        {
            pthread_mutex_unlock(&decMutex);
            break;
        }
    }

    AVPacket *pkt = NULL;
    pkt = (AVPacket*)av_malloc(sizeof(AVPacket));
    if (!pkt)
        return -1;

    ret = av_read_frame(inFmtCtx, pkt);
    if (ret < 0)
    {
        if (ret == AVERROR_EOF)
        {
            if (inLoopCnt > 1)
            {
                inLoopCnt--;
                avformat_close_input(&inFmtCtx);
                inFmtCtx = NULL;
                ret = avformat_open_input(&inFmtCtx, inFileName, NULL, NULL);
                if (ret < 0)
                    av_log(NULL, AV_LOG_ERROR, "Failed to re-open input file !\n");

                ret = avformat_find_stream_info(inFmtCtx, NULL);
                if (ret < 0)
                    av_log(NULL, AV_LOG_ERROR, "Failed to re-find stream info !\n");

                ret = av_read_frame(inFmtCtx, pkt);
                if (ret < 0)
                {
                    av_log(NULL, AV_LOG_ERROR, "Failed to read in one packet for decoding !\n");
                    av_packet_unref(pkt);
                    av_packet_free(&pkt);
                    pkt = NULL;
                    return ret;
                }
            }
            else
            {
                av_log(NULL, AV_LOG_INFO, "Reach the end of input file !\n");
            }
        }
        else
        {
            av_log(NULL, AV_LOG_ERROR, "Failed to read in one packet for decoding !\n");
            av_packet_unref(pkt);
            av_packet_free(&pkt);
            pkt = NULL;
            return ret;
        }
    }

    int32_t gotOneDecodedFrame = 0;
    if (pkt->stream_index == videoStrIdx)
    {
        AVCodecContext *codecCtx = decCodecCtx;
        ret = avcodec_send_packet(codecCtx, pkt);
        if (ret < 0)
        {
            if (ret == AVERROR_EOF)
                ret = 0;
            else
            {
                av_log(NULL, AV_LOG_ERROR, "Failed to send packet !\n");
                av_packet_unref(pkt);
                av_packet_free(&pkt);
                pkt = NULL;
                return ret;
            }
        }

        while (ret >= 0)
        {
            oneFrame = NULL;
            oneFrame = av_frame_alloc();
            if (!oneFrame)
            {
                av_log(NULL, AV_LOG_ERROR, "Failed to alloc AVFrame for decoding !\n");
                av_packet_unref(pkt);
                av_packet_free(&pkt);
                pkt = NULL;
                return -1;
            }

            ret = avcodec_receive_frame(codecCtx, oneFrame);

            if (ret < 0)
            {
                av_frame_free(&oneFrame);
                oneFrame = NULL;
                av_packet_unref(pkt);
                av_packet_free(&pkt);
                pkt = NULL;
                if (ret == AVERROR_EOF)
                {
                    av_log(NULL, AV_LOG_INFO, "*************Decode gets EOF***********************\n");
                }
                return ret;
            }

            if (ret >= 0)
                gotOneDecodedFrame = 1;

            if (gotOneDecodedFrame)
            {
                pthread_mutex_lock(&decMutex);
                framesList.insert(std::make_pair(decFrameCnt, std::make_pair(1, oneFrame)));
                pthread_mutex_unlock(&decMutex);
                decFrameCnt++;
                printf("%ld frames have been decoded !\n", decFrameCnt);
            }
        }
    }

    av_packet_unref(pkt);
    av_packet_free(&pkt);
    pkt = NULL;
    return 0;
}

int32_t EncodeOneFrame(EncThreadCtx *encThreadCtx)
{
    int32_t ret = 0;

    AVFrame *currFrame = NULL;
    AVFrame *encFrame = NULL;

    pthread_mutex_lock(&(encThreadCtx->encMutex));
    std::map<uint32_t, std::pair<uint32_t, AVFrame*>>::iterator itr;
    if (!decDone || (encThreadCtx->sendFrameCnt != decFrameCnt))
    {
        while (framesList.size())
        {
            itr = framesList.find(encThreadCtx->sendFrameCnt);
            if (itr == framesList.end())
            {
                pthread_mutex_unlock(&(encThreadCtx->encMutex));
                usleep(1000);
                pthread_mutex_lock(&(encThreadCtx->encMutex));
                continue;
            }
            std::pair<uint32_t, AVFrame*> oneFramePair = itr->second;
            currFrame = oneFramePair.second;
            break;
        }
    }
    if (!currFrame)
    {
        av_log(NULL, AV_LOG_ERROR, "There is no AVFrame for encoding !\n");
        pthread_mutex_unlock(&(encThreadCtx->encMutex));
        return AVERROR_EOF;
    }
    encFrame = currFrame;
    pthread_mutex_unlock(&(encThreadCtx->encMutex));

    AVPacket *packet = NULL;
    packet = av_packet_alloc();
    if (!packet)
    {
        av_log(NULL, AV_LOG_ERROR, "Failed to alloc AVPacket for encoding !\n");
        encFrame = NULL;
        return -1;
    }

    AVCodecContext *codecCtx = encThreadCtx->codecCtx;
    encFrame->pts = encThreadCtx->sendFrameCnt;
    ret = avcodec_send_frame(codecCtx, encFrame);
    if (ret < 0 && ret != AVERROR_EOF)
    {
        if (ret != AVERROR(EAGAIN))
            av_log(NULL, AV_LOG_ERROR, "Error in sending frame !\n");
        else
            av_log(NULL, AV_LOG_ERROR, "Need to send frame again !\n");

        encFrame = NULL;
        av_packet_unref(packet);
        av_packet_free(&packet);
        return ret;
    }
    if (ret == AVERROR_EOF)
        ret = 0;

    struct timeval tv1, tv2;
    gettimeofday(&tv1, NULL);
    pthread_mutex_lock(&(encThreadCtx->encMutex));
    encThreadCtx->sendFrameCnt++;

    pthread_mutex_unlock(&(encThreadCtx->encMutex));
    encFrame = NULL;
    gettimeofday(&tv2, NULL);

    while (ret >= 0)
    {
        ret = avcodec_receive_packet(codecCtx, packet);
        if (!ret)
        {
            encThreadCtx->getPacketCnt++;
            fwrite(packet->data, packet->size, 1, encThreadCtx->outFP);
            av_packet_unref(packet);
        }
    }

    av_packet_free(&packet);
    packet = NULL;
    return ret;
}

void *EncodeFrameThread(void *threadCtx)
{
    int32_t ret = 0;
    EncThreadCtx *encThreadCtx = (EncThreadCtx*)threadCtx;
    if (!encThreadCtx)
        return NULL;

    ret = OpenOutput(&(encThreadCtx->codecCtx), encThreadCtx->encCfg, &(encThreadCtx->outFP));
    if (ret)
        return NULL;

    while (1)
    {
        pthread_mutex_lock(&(encThreadCtx->encMutex));
        if (!(framesList.size()))
        {
            pthread_mutex_unlock(&(encThreadCtx->encMutex));
            usleep(1000);
            continue;
        }
        else
        {
            pthread_mutex_unlock(&(encThreadCtx->encMutex));
            break;
        }
    }

    while (1)
    {
        ret = EncodeOneFrame(encThreadCtx);
        if (ret < 0)
        {
            if (ret == AVERROR(EAGAIN))
                continue;
            else if (ret == AVERROR_EOF)
            {
                av_log(NULL, AV_LOG_INFO, "Encoding reaches the EOF !\n");
                ret = 0;
                break;
            }
            else
            {
                return NULL;
            }
        }
    }

    AVPacket *packet = NULL;
    packet = av_packet_alloc();
    AVCodecContext *codecCtx = encThreadCtx->codecCtx;
    while (ret >= 0)
    {
        ret = avcodec_receive_packet(codecCtx, packet);
        if (!ret)
        {
            encThreadCtx->getPacketCnt++;
            fwrite(packet->data, packet->size, 1, encThreadCtx->outFP);
            av_packet_unref(packet);
        }
        else if (ret == AVERROR(EAGAIN))
        {
            ret = avcodec_send_frame(codecCtx, NULL);
        }
    }

    av_packet_free(&packet);
    packet = NULL;

    return NULL;
}

void FreeResource()
{
    pthread_mutex_destroy(&decMutex);

    if (decCodecCtx)
    {
        avcodec_close(decCodecCtx);
        decCodecCtx = NULL;
    }

    if (inFmtCtx)
    {
        avformat_close_input(&inFmtCtx);
        inFmtCtx = NULL;
    }

    if (framesList.size())
    {
        std::map<uint32_t, std::pair<uint32_t, AVFrame*>>::iterator itrFrame;
        for (itrFrame = framesList.begin(); itrFrame != framesList.end(); itrFrame++)
        {
            std::pair<uint32_t, AVFrame*> framePair = itrFrame->second;
            AVFrame *oneFrame = framePair.second;
            if (oneFrame)
            {
                av_frame_free(&oneFrame);
                oneFrame = NULL;
            }
        }
        framesList.clear();
    }

    std::list<EncThreadCtx*>::iterator iter1;
    for (iter1 = encThreadCtxs.begin(); iter1 != encThreadCtxs.end(); iter1++)
    {
        EncThreadCtx *encThreadCtx = *iter1;
        if (encThreadCtx)
        {
            if (encThreadCtx->codecCtx)
            {
                avcodec_free_context(&(encThreadCtx->codecCtx));
                encThreadCtx->codecCtx = NULL;
            }

            int32_t ret = pthread_join(encThreadCtx->threadIdx, NULL);
            if (ret)
                av_log(NULL, AV_LOG_ERROR, "!!!!!Failed to destroy encoding thread!!!!!!!!!!!!!\n");

            pthread_mutex_destroy(&(encThreadCtx->encMutex));

            if (encThreadCtx->outFP)
            {
                fclose(encThreadCtx->outFP);
                encThreadCtx->outFP = NULL;
            }

            free(encThreadCtx);
            encThreadCtx = NULL;
        }
    }
    encThreadCtxs.clear();

    if (outParams)
    {
        free(outParams);
        outParams = NULL;
    }

    if (parserHdl)
    {
        ParseEncodingParamClose(parserHdl);
        parserHdl = NULL;
    }
}

int32_t ReadYUVFrame(int32_t frame_size)
{
    FILE *infd = fopen(inFileName, "rb");
    if (!infd)
    {
        printf("open file failed");
        return -1;
    }

    while (1)
    {
        uint8_t *picture_buf = (uint8_t *) av_malloc(frame_size);
        size_t ret = fread(picture_buf, frame_size, 1, infd);
        if (ret < 0)
        {
            printf("Read file failed");
            return -1;
        }
        else if (feof(infd))
        {
            av_log(NULL, AV_LOG_INFO, "Reach the end of input file !\n");
            return AVERROR_EOF;
        }
        bufList.insert(std::make_pair(frame_num, picture_buf));
        frame_num++;
    }
    return 0;
}

int32_t BuildP010AVFrames()
{
    int width = outParams->videos_encoding_cfg->width;
    int height = outParams->videos_encoding_cfg->height;
    for (int i = 0; i < inLoopCnt; i++)
    {
        for (int j = 0; j < frame_num; j++)
        {
            while (1)
            {
                pthread_mutex_lock(&decMutex);
                if (framesList.size() > bufferedFramesNum)
                {
                    std::map<uint32_t, std::pair<uint32_t, AVFrame*>>::iterator itr;
                    for (itr = framesList.begin(); itr != --framesList.end();)
                    {
                        uint32_t frameIdx = itr->first;
                        std::pair<uint32_t, AVFrame*> frontFrame = itr->second;
                        AVFrame *currFrame = frontFrame.second;

                        if (frameIdx != delFrameCnt)
                            av_log(NULL, AV_LOG_ERROR, "Mismatch frameIdx %d vs delFrameCnt %ld ~~~~~ \n", frameIdx, delFrameCnt);

                        int32_t sendDoneNum = 0;
                        std::list<EncThreadCtx*>::iterator iter1;
                        for (iter1 = encThreadCtxs.begin(); iter1 != encThreadCtxs.end(); iter1++)
                        {
                            EncThreadCtx *encThreadCtx = *iter1;
                            if (encThreadCtx->sendFrameCnt > frameIdx)
                            {
                                sendDoneNum++;
                            }
                        }
                        if (sendDoneNum == outVideosNum)
                        {//here means the frame has been used by all encoding thread
                            framesList.erase(itr++);
                            av_frame_free(&currFrame);
                            currFrame = NULL;
                            delFrameCnt++;
                            // av_log(NULL, AV_LOG_ERROR, "free one frame, frameIDx: %d, delFrameCnt: %ld ~~~~~ \n", frameIdx, delFrameCnt);
                        }
                        else
                            break;
                    }
                }
                if (framesList.size() > bufferedFramesNum)
                {
                    pthread_mutex_unlock(&decMutex);
                    usleep(1000);
                    continue;
                }
                else
                {
                    pthread_mutex_unlock(&decMutex);
                    break;
                }
            }

            // Build AVFrame structure
            AVFrame *oneFrame = av_frame_alloc();
            oneFrame->width = width;
            oneFrame->height = height;
            oneFrame->data[0] = bufList.find(j)->second;
            oneFrame->linesize[0] = width * 2;
            oneFrame->data[1] = oneFrame->data[0] + width * height * 2;
            oneFrame->linesize[1] = oneFrame->linesize[0];

            pthread_mutex_lock(&decMutex);
            framesList.insert(std::make_pair(decFrameCnt, std::make_pair(1, oneFrame)));
            pthread_mutex_unlock(&decMutex);
            decFrameCnt += 1;
            av_log(NULL, AV_LOG_INFO, "Loaded one frame: %d!\n", j);
        }
    }
    decDone = true;
    return 0;
}

int32_t BuildV210AVFrames()
{
    int width = outParams->videos_encoding_cfg->width;
    int height = outParams->videos_encoding_cfg->height;
    for (int i = 0; i < inLoopCnt; i++)
    {
        for (int j = 0; j < frame_num; j++)
        {
            while (1)
            {
                pthread_mutex_lock(&decMutex);
                if (framesList.size() > bufferedFramesNum)
                {
                    std::map<uint32_t, std::pair<uint32_t, AVFrame*>>::iterator itr;
                    for (itr = framesList.begin(); itr != --framesList.end();)
                    {
                        uint32_t frameIdx = itr->first;
                        std::pair<uint32_t, AVFrame*> frontFrame = itr->second;
                        AVFrame *currFrame = frontFrame.second;

                        if (frameIdx != delFrameCnt)
                            av_log(NULL, AV_LOG_ERROR, "Mismatch frameIdx %d vs delFrameCnt %ld ~~~~~ \n", frameIdx, delFrameCnt);

                        int32_t sendDoneNum = 0;
                        std::list<EncThreadCtx*>::iterator iter1;
                        for (iter1 = encThreadCtxs.begin(); iter1 != encThreadCtxs.end(); iter1++)
                        {
                            EncThreadCtx *encThreadCtx = *iter1;
                            if (encThreadCtx->sendFrameCnt > frameIdx)
                            {
                                sendDoneNum++;
                            }
                        }
                        if (sendDoneNum == outVideosNum)
                        {//here means the frame has been used by all encoding thread
                            framesList.erase(itr++);
                            av_frame_free(&currFrame);
                            currFrame = NULL;
                            delFrameCnt++;
                            // av_log(NULL, AV_LOG_ERROR, "free one frame, frameIDx: %d, delFrameCnt: %ld ~~~~~ \n", frameIdx, delFrameCnt);
                        }
                        else
                            break;
                    }
                }
                if (framesList.size() > bufferedFramesNum)
                {
                    pthread_mutex_unlock(&decMutex);
                    usleep(1000);
                    continue;
                }
                else
                {
                    pthread_mutex_unlock(&decMutex);
                    break;
                }
            }

            // Build AVFrame structure
            AVFrame *oneFrame = av_frame_alloc();
            oneFrame->width = width;
            oneFrame->height = height;
            oneFrame->data[0] = bufList.find(j)->second;
            oneFrame->linesize[0] = width * 8 / 3;

            pthread_mutex_lock(&decMutex);
            framesList.insert(std::make_pair(decFrameCnt, std::make_pair(1, oneFrame)));
            pthread_mutex_unlock(&decMutex);
            decFrameCnt += 1;
            av_log(NULL, AV_LOG_INFO, "Loaded one frame: %d!\n", j);
        }
    }
    decDone = true;
    return 0;
}

int32_t main(int32_t argc, char *argv[])
{
    int32_t ret = 0;
    char *encCfgFile = NULL;
    struct timeval tv1, tv2;
    gettimeofday(&tv1, NULL);


    if (argc < 13)
    {
        av_log(NULL, AV_LOG_ERROR, "Not enough command line parameters !\n");
        return -1;
    }

    if (strncmp(argv[1], "-thread_type", 12) == 0)
    {
        threadType = atoi(argv[2]);
        av_log(NULL, AV_LOG_INFO, "Thread type for decoding is %d !\n", threadType);
    }
    if (strncmp(argv[3], "-thread_count", 13) == 0)
    {
        threadCnt = atoi(argv[4]);
        av_log(NULL, AV_LOG_INFO, "Thread count for decoding is %d !\n", threadCnt);
    }
    if (strncmp(argv[5], "-loop", 5) == 0)
    {
        inLoopCnt = atoi(argv[6]);
        av_log(NULL, AV_LOG_INFO, "Input file loop times is %d !\n", inLoopCnt);
    }

    if (strncmp(argv[7], "-i", 2) == 0)
    {
        inFileName = argv[8];
        av_log(NULL, AV_LOG_INFO, "Input source file is %s !\n", inFileName);
    }

    if (strncmp(argv[9], "-c", 2) == 0)
    {
        encCfgFile = argv[10];
        av_log(NULL, AV_LOG_INFO, "Output videos config file is %s !\n", encCfgFile);
    }

    if (strncmp(argv[11], "-buf_frames", 11) == 0)
    {
        bufferedFramesNum = atoi(argv[12]);
        av_log(NULL, AV_LOG_INFO, "Buffered frames number in frames list is %ld for performance optimization !\n", bufferedFramesNum);
    }

    if (argc > 13 && strncmp(argv[13], "-yuv", 4) == 0)
    {
        input_yuv_format = argv[14];
        if (strncmp(input_yuv_format, "p010", 4) &&
            strncmp(input_yuv_format, "v210", 4))
        {
            av_log(NULL, AV_LOG_ERROR, "Input yuv format not supported: %s !\n", input_yuv_format);
            av_log(NULL, AV_LOG_ERROR, "Supported input yuv type: p010, v210\n");
            return -1;
        }
        av_log(NULL, AV_LOG_INFO, "Input file type is %s !\n", input_yuv_format);
    }

    if (!inFileName || !encCfgFile)
    {
        av_log(NULL, AV_LOG_ERROR, "Incorrect command line parameters !\n");
        return -1;
    }

    inFmtCtx = NULL;
    ret = pthread_mutex_init(&decMutex, NULL);
    if (ret)
    {
        av_log(NULL, AV_LOG_ERROR, "Failed to initialize decode thread mutex !\n");
        FreeResource();
        return -1;
    }

    outParams = (OutputParam *)malloc(sizeof(OutputParam));
    if (!outParams)
    {
        av_log(NULL, AV_LOG_ERROR, "Failed to alloc memory for output parameters !\n");
        FreeResource();
        return -1;
    }
    memset(outParams, 0, sizeof(OutputParam));

    parserHdl = ParseEncodingParamInit();
    if (!parserHdl)
    {
        av_log(NULL, AV_LOG_ERROR, "Failed to initialize encoding params parsing lib !\n");

        FreeResource();
        return -1;
    }

    ret = ParseEncodingParamParse(parserHdl, encCfgFile, (void*)outParams);
    if (ret)
    {
        av_log(NULL, AV_LOG_ERROR, "Failed to parse encoding params !\n");

        FreeResource();
        return -1;
    }

    outVideosNum = outParams->out_videos_num;

    if (input_yuv_format)
    {
        int32_t frame_width = outParams->videos_encoding_cfg->width;
        int32_t frame_height = outParams->videos_encoding_cfg->height;
        if (strncmp(input_yuv_format, "p010", 4) == 0)
        {
            int32_t p010_frame_size = frame_width * frame_height * 3;
            ret = ReadYUVFrame(p010_frame_size);
        }
        else if (strncmp(input_yuv_format, "v210", 4) == 0)
        {
            int32_t v210_frame_size = frame_width * frame_height * 8 / 3;
            ret = ReadYUVFrame(v210_frame_size);
        }
        if (ret < 0 && ret != AVERROR_EOF)
        {
            FreeResource();
            return -1;
        }
    }
    else
    {
        ret = OpenInput();
        if (ret)
        {
            FreeResource();
            return -1;
        }
    }

    if (input_yuv_format)
    {
        gettimeofday(&tv1, NULL);
    }

    for (uint32_t outIdx = 0; outIdx < outParams->out_videos_num; outIdx++)
    {
        EncThreadCtx *encThreadCtx = (EncThreadCtx*)malloc(sizeof(EncThreadCtx));
        if (!encThreadCtx)
        {
            av_log(NULL, AV_LOG_ERROR, "Failed to alloc memory for encoding thread context !\n");
            FreeResource();
            return -1;
        }
        memset(encThreadCtx, 0, sizeof(EncThreadCtx));
        encThreadCtx->encCfg = &(outParams->videos_encoding_cfg[outIdx]);

        ret = pthread_create(&(encThreadCtx->threadIdx), NULL, EncodeFrameThread, (void*)encThreadCtx);
        if (ret)
        {
            av_log(NULL, AV_LOG_ERROR, "Failed to create encoding thread !\n");
            FreeResource();
            return -1;
        }

        encThreadCtxs.push_back(encThreadCtx);
    }

    if (input_yuv_format && strncmp(input_yuv_format, "p010", 4) == 0)
    {
        ret = BuildP010AVFrames();
    }
    else if (input_yuv_format && strncmp(input_yuv_format, "v210", 4) == 0)
    {
        ret = BuildV210AVFrames();
    }
    else
    {
        while(1)
        {
            ret = DecodeOneFrame();
            if (ret < 0)
            {
                if (ret == AVERROR(EAGAIN))
                    continue;
                else if (ret == AVERROR_EOF)
                    break;
                else
                    break;
            }
        }
    }

    av_log(NULL, AV_LOG_INFO, "Totally decoding %ld frames !\n", decFrameCnt);
    decDone = true;
    gettimeofday(&tv2, NULL);
    uint64_t duration2 = (uint64_t)((tv2.tv_sec - tv1.tv_sec) * 1000000 + tv2.tv_usec - tv1.tv_usec);
    float fps2 = (float)(decFrameCnt * 1000000) / duration2;
    av_log(NULL, AV_LOG_INFO, "Decoding %ld frames takes %ld us, and average fps is %.2f !\n", decFrameCnt, duration2, fps2);
    pthread_mutex_lock(&decMutex);
    av_log(NULL, AV_LOG_ERROR, "Now there are still %ld frames in frames list !\n", framesList.size());
    pthread_mutex_unlock(&decMutex);

    std::list<EncThreadCtx*>::iterator iter;
    for (iter = encThreadCtxs.begin(); iter != encThreadCtxs.end(); iter++)
    {
        EncThreadCtx *encThreadCtx = *iter;
        while (1)
        {
            pthread_mutex_lock(&(encThreadCtx->encMutex));
            if (encThreadCtx->getPacketCnt < encThreadCtx->sendFrameCnt)
            {
                pthread_mutex_unlock(&(encThreadCtx->encMutex));
                usleep(1000);
                continue;
            }
            else
            {
                pthread_mutex_unlock(&(encThreadCtx->encMutex));
                break;
            }
        }
    }

    while(1)
    {
        pthread_mutex_lock(&decMutex);
        if (framesList.size() > 0)
        {
            std::map<uint32_t, std::pair<uint32_t, AVFrame*>>::iterator itr;
            for (itr = framesList.begin(); itr != framesList.end();)
            {
                uint32_t frameIdx = itr->first;
                std::pair<uint32_t, AVFrame*> framePair = itr->second;
                if (frameIdx != delFrameCnt)
                    av_log(NULL, AV_LOG_ERROR, "Mismatch deleted frame cnt frameIdx %d vs delFrameCnt %ld !\n", frameIdx, delFrameCnt);

                AVFrame *currFrame = framePair.second;

                int32_t sendDoneNum = 0;
                std::list<EncThreadCtx*>::iterator iter1;
                for (iter1 = encThreadCtxs.begin(); iter1 != encThreadCtxs.end(); iter1++)
                {
                    EncThreadCtx *encThreadCtx = *iter1;
                    if (encThreadCtx->sendFrameCnt > frameIdx)
                    {
                        sendDoneNum++;
                    }
                }
                if (sendDoneNum == outVideosNum)
                {
                    framesList.erase(itr++);
                    av_frame_free(&currFrame);
                    currFrame = NULL;
                    delFrameCnt++;
                }
                else
                {
                    break;
                }
            }
            if (!framesList.size())
            {
                av_log(NULL, AV_LOG_INFO, "All decoded frames have been sent !\n");

                pthread_mutex_unlock(&decMutex);
                break;
            }
        }
        else
        {
            av_log(NULL, AV_LOG_INFO, "All decoded frames have been sent !\n");
            pthread_mutex_unlock(&decMutex);

            break;
        }

        usleep(1000);
    }

    for (iter = encThreadCtxs.begin(); iter != encThreadCtxs.end(); iter++)
    {
        EncThreadCtx *encCtx = *iter;
        if (encCtx)
        {
            if (encCtx->sendFrameCnt != decFrameCnt)
            {
                av_log(NULL, AV_LOG_ERROR, "Different encoded frames num among multiple encoding threads !\n");
                break;
            }
        }
    }

    FreeResource();
    gettimeofday(&tv2, NULL);
    uint64_t duration = (uint64_t)((tv2.tv_sec - tv1.tv_sec) * 1000000 + tv2.tv_usec - tv1.tv_usec);
    float fps = (float)(decFrameCnt * 1000000) / duration;
    av_log(NULL, AV_LOG_INFO, "Transcoding %ld frames takes %ld us, and average fps is %.2f !\n", decFrameCnt, duration, fps);
    return 0;
}
