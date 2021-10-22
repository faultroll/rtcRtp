/*************************************************************************
    > File Name: rtsp_demo.h
    > Author: bxq
    > Mail: 544177215@qq.com
    > Created Time: Monday, November 23, 2015 AM12:22:43 CST
 ************************************************************************/

#ifndef __RTSP_DEMO_H__
#define __RTSP_DEMO_H__
/*
 * a simple RTSP server demo
 * RTP over UDP/TCP H264/G711a
 * */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum rtsp_codec_id {
    RTSP_CODEC_ID_NONE = 0,
    RTSP_CODEC_ID_VIDEO_H264 = 0x0001,  /*codec_data is SPS + PPS frames*/
    RTSP_CODEC_ID_VIDEO_H265,           /*codec_data is VPS + SPS + PPS frames*/
    RTSP_CODEC_ID_VIDEO_MPEG4,          /*now not support*/
    RTSP_CODEC_ID_AUDIO_G711A = 0x4001, /*codec_data is NULL*/
    RTSP_CODEC_ID_AUDIO_G711U,          /*codec_data is NULL*/
    RTSP_CODEC_ID_AUDIO_G7221,
    RTSP_CODEC_ID_AUDIO_G726,           /*codec_data is channels + bitrate + samplerate (4bytes)*/
    RTSP_CODEC_ID_AUDIO_AAC,            /*codec_data is audio specific config (2bytes). frame type is ADTS*/
};

typedef void *rtsp_demo_handle;
typedef void *rtsp_session_handle;

rtsp_demo_handle rtsp_new_demo(int port);
// rtsp_demo_handle create_rtsp_demo(int port);
int rtsp_do_event(rtsp_demo_handle demo);
rtsp_session_handle rtsp_new_session(rtsp_demo_handle demo, const char *path);
// rtsp_session_handle create_rtsp_session(rtsp_demo_handle demo, const char *path);
void rtsp_del_session(rtsp_session_handle session);
void rtsp_del_demo(rtsp_demo_handle demo);


int rtsp_set_video(rtsp_session_handle session, int codec_id, const uint8_t *codec_data, int data_len);
int rtsp_set_audio(rtsp_session_handle session, int codec_id, const uint8_t *codec_data, int data_len);
int rtsp_sync_video_ts(rtsp_session_handle session, uint64_t ts, uint64_t ntptime);
int rtsp_sync_audio_ts(rtsp_session_handle session, uint64_t ts, uint64_t ntptime);
uint64_t rtsp_get_reltime(void);
uint64_t rtsp_get_ntptime(void);

// int rtsp_sever_tx_video (rtsp_demo_handle demo,rtsp_session_handle session, const uint8_t *frame, int len, uint64_t ts);
int rtsp_tx_video(rtsp_session_handle session, const uint8_t *frame, int len, uint64_t ts);
int rtsp_tx_audio(rtsp_session_handle session, const uint8_t *frame, int len, uint64_t ts);


// helper
#define DEMO_RTSP_SESSIONS (3)
static rtsp_demo_handle demo_live_;
static rtsp_session_handle demo_session_[DEMO_RTSP_SESSIONS];
static inline void demo_rtsp_start(int codec_id[][2])
{
    demo_live_ = rtsp_new_demo(554);
    for (int i = 0; i < DEMO_RTSP_SESSIONS; i++) {
        char suffix[16];
        snprintf(suffix, sizeof(suffix), "/live%d.sdp", i);
        demo_session_[i] = rtsp_new_session(demo_live_, suffix);

        if (codec_id[i][0] != RTSP_CODEC_ID_NONE) {
            rtsp_set_video(demo_session_[i], codec_id[i][0], NULL, 0);
            rtsp_sync_video_ts(demo_session_[i], rtsp_get_reltime(), rtsp_get_ntptime());
        }
        if (codec_id[i][1] != RTSP_CODEC_ID_NONE) {
            rtsp_set_audio(demo_session_[i], codec_id[i][0], NULL, 0);
            rtsp_sync_audio_ts(demo_session_[i], rtsp_get_reltime(), rtsp_get_ntptime());
        }

        printf("%s %d: 0x%x, 0x%x\n", __func__, i, codec_id[i][0], codec_id[i][1]);
    }
}
static inline void demo_rtsp_stop(void)
{
    for (int i = 0; i < DEMO_RTSP_SESSIONS; i++) {
        rtsp_del_session(demo_session_[i]);
    }
    rtsp_del_demo(demo_live_);
}

#ifdef __cplusplus
}
#endif

#endif /* __RTSP_DEMO_H__ */
