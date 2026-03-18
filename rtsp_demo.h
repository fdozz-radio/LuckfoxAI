#ifndef __RTSP_DEMO_H__
#define __RTSP_DEMO_H__

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void* rtsp_demo_handle;
typedef void* rtsp_session_handle;

rtsp_demo_handle create_rtsp_demo(int port);
rtsp_session_handle rtsp_new_session(rtsp_demo_handle demo, const char* path);
void rtsp_set_video(rtsp_session_handle session, int codec_id, const uint8_t* extradata, int extradata_size);
void rtsp_sync_video_ts(rtsp_session_handle session, uint32_t reltime, uint64_t ntptime);
void rtsp_tx_video(rtsp_session_handle session, const uint8_t* frame, int len, uint64_t pts);
void rtsp_do_event(rtsp_demo_handle demo);
void rtsp_del_demo(rtsp_demo_handle demo);
uint32_t rtsp_get_reltime();
uint64_t rtsp_get_ntptime();

// Codec IDs
#define RTSP_CODEC_ID_VIDEO_H264 96

#ifdef __cplusplus
}
#endif

#endif // __RTSP_DEMO_H__
