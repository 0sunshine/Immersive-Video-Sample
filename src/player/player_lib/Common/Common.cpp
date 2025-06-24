#include "Common.h"

#ifndef AVIT_LOG_TAG
#define AVIT_LOG_TAG "avit log >>>>>>>>"
#endif

static int32_t s_audioDelay = 0;

int32_t getPlayAudioDelayMs()
{
    return s_audioDelay;
}

void setPlayAudioDelayMs(int32_t ms)
{
    s_audioDelay = ms;

    ANDROID_LOGD("%s set audio delay: %d", AVIT_LOG_TAG, s_audioDelay);
    LOG(INFO) << AVIT_LOG_TAG << " set audio delay: " << s_audioDelay << std::endl;
}
