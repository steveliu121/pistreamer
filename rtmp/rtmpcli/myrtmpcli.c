#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <stdint.h>
#include <pthread.h>
#include <errno.h>

#include "myrtmp.h"
#include "aacenc.h"
#include "my_middle_media.h"
#include <my_video_input.h>


/* XXX:WARNING the pcm period buf length should be the common factor of
 * the aac input pcm frame length, or the aac timestamp will be wrong
 * here the pcm period buf length == 1024 bytes,
 * and the aac input pcm frame length == 2048
 */


#define SPS_LEN		28
#define PPS_LEN		6

#define RES_720P
#ifdef RES_720P
#define RESOLUTION_720P MY_VIDEO_RES_720P
#define RES_WIDTH	1280
#define RES_HEIGHT	720
#endif

#define VIDEO_FPS		15
#define VIDEO_TIME_SCALE	90000
#define VIDEO_SAMPLE_DURATION	(VIDEO_TIME_SCALE / VIDEO_FPS)

#define AUDIO_SAMPLERATE	8000
#define AUDIO_CHANNELS		1
#define AUDIO_TIME_SCALE	(AUDIO_SAMPLERATE * AUDIO_CHANNELS)
/* (AUDIO_TIME_SCALE / AUDIO_FPS)audio_period_time = 64ms, fps = 15.625 */
#define AUDIO_SAMPLE_DURATION	512
#define AAC_BITRATE		16000


static const uint8_t sps_buf[SPS_LEN] = {0x27, 0x64, 0x00, 0x29, 0xac, 0x1a, 0xd0, 0x0a,
			0x00, 0xb7, 0x4d, 0xc0, 0x40, 0x40, 0x50, 0x00,
			0x00, 0x03, 0x00, 0x10, 0x00 ,0x00, 0x03, 0x01,
			0xe8, 0xf1 ,0x42, 0x2a};
static const uint8_t pps_buf[PPS_LEN] = {0x28, 0xee, 0x01, 0x34, 0x92, 0x24};
/*
static const uint8_t sps_buf[SPS_LEN + 4] = {0x00, 0x00, 0x00, 0x1c, 0x27, 0x64,
			0x00, 0x29, 0xac, 0x1a, 0xd0, 0x0a,
			0x00, 0xb7, 0x4d, 0xc0, 0x40, 0x40, 0x50, 0x00,
			0x00, 0x03, 0x00, 0x10, 0x00 ,0x00, 0x03, 0x01,
			0xe8, 0xf1 ,0x42, 0x2a};
static const uint8_t pps_buf[PPS_LEN + 4] = {0x00, 0x00, 0x00, 0x06, 0x28, 0xee,
			0x01, 0x04, 0x92, 0x24};
			*/
static int g_exit;
static HANDLE_AACENCODER aac_enc_hd;
static uint8_t aac_decoder_conf[64];
static int aac_decoder_conf_len;
uint32_t g_timestamp_begin;
RTMP *rtmp;
RTMPPacket video_pkt;
RTMPPacket audio_pkt;
pthread_mutex_t av_mutex;

void sig_handle(int sig)
{
	g_exit = 1;
}

void h264_cb(const struct timeval *tv, const void *data,
	const int len, const int keyframe)
{
	int ret = 0;
	uint8_t *buf = NULL;
	int buf_len = 0;
	uint32_t timestamp = 0;
	int buf_payload_len = 0;

	timestamp = (tv->tv_sec * 1000) + (tv->tv_usec / 1000);

	if (g_timestamp_begin == 0)
		g_timestamp_begin = timestamp;

	/* strip sps/pps from I frame and
	 * replace NALU start flag '0x00/0x00/0x00/0x01' with
	 * the length of NALU in BIGENDIAN
	 */
	if (keyframe) {
		buf = (uint8_t *)data + SPS_LEN + PPS_LEN + 2 * 4;
		buf_len = len - SPS_LEN - PPS_LEN - 2 * 4;
	} else {
		buf = (uint8_t *)data;
		buf_len = len;
	}
	buf_payload_len = buf_len - 4;
	buf[0] = buf_payload_len >> 24;
	buf[1] = buf_payload_len >> 16;
	buf[2] = buf_payload_len >> 8;
	buf[3] = buf_payload_len & 0xff;

	video_pkt.m_headerType = RTMP_PACKET_SIZE_LARGE;
	video_pkt.m_nTimeStamp = (timestamp - g_timestamp_begin);
	video_pkt.m_nBodySize = buf_len + 5;//5bytes VIDEODATA tag header
	rtmppacket_alloc(&video_pkt, video_pkt.m_nBodySize);
	rtmp_write_avc_data_tag(video_pkt.m_body, buf, buf_len, keyframe);

	ret = rtmp_isconnected(rtmp);
	if (ret == true) {
		/* true: send to outqueue;false: send directly */
		pthread_mutex_lock(&av_mutex);
		ret = rtmp_sendpacket(rtmp, &video_pkt, true);
		if (ret == false)
			printf("rtmp send video packet fail\n");
		pthread_mutex_unlock(&av_mutex);
	}

	rtmppacket_free(&video_pkt);
}

void audio_cb(const struct timeval *tv, const void *pcm_buf,
	const int pcm_len, const void *spk_buf)
{
	int ret = 0;
	uint8_t *aac_buf = NULL;
	int aac_buf_len = 0;
	uint32_t timestamp = 0;

	timestamp = (tv->tv_sec * 1000) + (tv->tv_usec / 1000);

	if (g_timestamp_begin == 0)
		g_timestamp_begin = timestamp;

	aac_buf_len = aac_encode(aac_enc_hd, pcm_buf, pcm_len, &aac_buf);
	if (aac_buf_len == 0)
		return;

	audio_pkt.m_headerType = RTMP_PACKET_SIZE_LARGE;
	audio_pkt.m_nTimeStamp = (timestamp - g_timestamp_begin);
	audio_pkt.m_nBodySize = aac_buf_len - 7 + 2;//7bytes ADTS header & 2bytes AUDIODATA tag header
	rtmppacket_alloc(&audio_pkt, audio_pkt.m_nBodySize);
	rtmp_write_aac_data_tag(audio_pkt.m_body, aac_buf, aac_buf_len);

	ret = rtmp_isconnected(rtmp);
	if (ret == true) {
		/* true: send to outqueue;false: send directly */
		pthread_mutex_lock(&av_mutex);
		ret = rtmp_sendpacket(rtmp, &audio_pkt, true);
		if (ret == false)
			printf("rtmp send audio packet fail\n");
		pthread_mutex_unlock(&av_mutex);
	}

	rtmppacket_free(&audio_pkt);
}

static int __connect2rtmpsvr(char *url)
{
	int ret = 0;

	rtmp = rtmp_alloc();
	rtmp_init(rtmp);

	rtmp->Link.timeout=5;	//default 30s
	ret = rtmp_setupurl(rtmp, url);
	if (ret == false) {
		printf("rtmp setup url fail\n");
		goto exit;
	}

	rtmp_enablewrite(rtmp);

	ret = rtmp_connect(rtmp, NULL);
	if (ret == false) {
		printf("rtmp connect fail\n");
		goto exit;
	}

	ret = rtmp_connectstream(rtmp, 0);
	if (ret == false) {
		printf("rtmp connect stream fail\n");
		rtmp_close(rtmp);
		goto exit;
	}

	return 0;

exit:
	return -1;
}

static void __rtmp_send_sequence_header(void)
{
	int ret = 0;

/* rtmp send audio/video sequence header frame */
	rtmppacket_reset(&video_pkt);
	rtmppacket_reset(&audio_pkt);

	video_pkt.m_packetType = RTMP_PACKET_TYPE_VIDEO;
	video_pkt.m_nChannel = 0x04;
	video_pkt.m_nInfoField2 = rtmp->m_stream_id;
	video_pkt.m_hasAbsTimestamp = false;

	audio_pkt.m_packetType = RTMP_PACKET_TYPE_AUDIO;
	audio_pkt.m_nChannel = 0x04;
	audio_pkt.m_nInfoField2 = rtmp->m_stream_id;
	audio_pkt.m_hasAbsTimestamp = false;

	video_pkt.m_headerType = RTMP_PACKET_SIZE_LARGE;
	video_pkt.m_nTimeStamp = 0;
	video_pkt.m_nBodySize = SPS_LEN + PPS_LEN + 16;
	rtmppacket_alloc(&video_pkt, video_pkt.m_nBodySize);
	rtmp_write_avc_sequence_header_tag(video_pkt.m_body,
						sps_buf, SPS_LEN,
						pps_buf, PPS_LEN);

	ret = rtmp_isconnected(rtmp);
	if (ret == true) {
		/* true: send to outqueue;false: send directly */
		pthread_mutex_lock(&av_mutex);
		ret = rtmp_sendpacket(rtmp, &video_pkt, true);
		if (ret == false)
			printf("rtmp send video packet fail\n");
		pthread_mutex_unlock(&av_mutex);
	}

	rtmppacket_free(&video_pkt);


	audio_pkt.m_headerType = RTMP_PACKET_SIZE_LARGE;
	audio_pkt.m_nTimeStamp = 0;
	audio_pkt.m_nBodySize = 4;
	rtmppacket_alloc(&audio_pkt, audio_pkt.m_nBodySize);
	rtmp_write_aac_sequence_header_tag(audio_pkt.m_body,
					AUDIO_SAMPLERATE, AUDIO_CHANNELS);

	ret = rtmp_isconnected(rtmp);
	if (ret == true) {
		/* true: send to outqueue;false: send directly */
		pthread_mutex_lock(&av_mutex);
		ret = rtmp_sendpacket(rtmp, &audio_pkt, true);
		if (ret == false)
			printf("rtmp send audio packet fail\n");
		pthread_mutex_unlock(&av_mutex);
	}

	rtmppacket_free(&audio_pkt);/* rtmp send audio/video sequence header frame */
}

int main(int argc, char *argv[])
{
	int ret = 0;

	MYVideoInputChannel chn = {
		.channelId = 0,
		.res = RESOLUTION_720P,
		.fps = VIDEO_FPS,
		.bitrate = 1024,
		.gop = 1,
		.vbr = MY_BITRATE_MODE_CBR,
		.cb = h264_cb
	};

	MYVideoInputOSD osd_info = {
		.pic_enable = 0,
		.pic_path = "/usr/osd_char_lib/argb_2222",
		.pic_x = 200,
		.pic_y = 200,
		.time_enable = 1,
		.time_x = 100,
		.time_y  = 100
	};

	MYAudioInputAttr_aec audio_in = {
		.sampleRate = AUDIO_SAMPLERATE,
		.sampleBit = 16,
		.volume = 95,
		.cb = audio_cb
	};


	signal(SIGTERM, sig_handle);
	signal(SIGINT, sig_handle);

	pthread_mutex_init(&av_mutex, NULL);

	rtmp_logsetlevel(RTMP_LOGINFO);

	if (argc <= 1) {
		printf("Usage: %s <rtmp_url>\n"
		"	rtmp_url	 RTMP stream url to publish\n"
		"For example:\n"
		"	%s rtmp://127.0.0.1:1935/live/livestream\n",
		argv[0], argv[0]);
		exit(-1);
	}


	ret = __connect2rtmpsvr(argv[1]);
	if (ret < 0)
		goto exit;

/* create aacencoder */
	ret = create_aac_encoder(&aac_enc_hd,
				AUDIO_CHANNELS, AUDIO_SAMPLERATE, AAC_BITRATE,
				aac_decoder_conf, &aac_decoder_conf_len);
	if (ret < 0)
		goto exit;/* create aacencoder */

	__rtmp_send_sequence_header();

/* start audio&video device and receive buffers, do muxer in callback */
	MYAV_Context_Init();

	ret = MYVideoInput_Init();
	if (ret)
		goto out;

	ret = MYVideoInput_AddChannel(chn);
	if (ret)
		goto out;

	ret = MYVideoInput_SetOSD(chn.channelId, &osd_info);
	if (ret)
		goto out;

	ret = MYAudioInputOpen(&audio_in);
	if (ret)
		goto out;

	ret = MYVideoInput_Start();
	if (ret)
		goto out;

	ret = MYAudioInputStart();
	if (ret)
		goto out;/* start audio&video device and receive buffers, do muxer in callback */

	while (!g_exit)
		sleep(1);

out:
	MYVideoInput_Uninit();
	MYAudioInputStop();
	MYAudioInputClose();

	MYAV_Context_Release();

exit:
	pthread_mutex_destroy(&av_mutex);
	rtmp_close(rtmp);
	rtmp_free(rtmp);
	rtmp = NULL;

	return ret;

}
