#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <stdint.h>
#include <pthread.h>
#include <errno.h>

#include "mp4muxer.h"
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
#define OUTFILE		"my.mp4"

#define RES_720P
#ifdef RES_720P
#define RESOLUTION_720P QCAM_VIDEO_RES_720P
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
static MP4FileHandle mp4_hd;
static MP4TrackId video_tk;
static MP4TrackId audio_tk;
static HANDLE_AACENCODER aac_enc_hd;
static uint8_t aac_decoder_conf[64];
static int aac_decoder_conf_len;


void sig_handle(int sig)
{
	g_exit = 1;
}

void h264_cb(const struct timeval *tv, const void *data,
	const int len, const int keyframe)
{
	mp4_pack_h264(mp4_hd, video_tk, tv, (uint8_t *)data, len, keyframe);
}

void audio_cb(const struct timeval *tv, const void *pcm_buf,
	const int pcm_len, const void *spk_buf)
{
	static uint8_t *aac_buf = NULL;
	static int aac_buf_len = 0;

	mp4_pack_aac(mp4_hd, audio_tk, aac_buf, aac_buf_len, tv);

	aac_buf_len = aac_encode(aac_enc_hd, pcm_buf, pcm_len, &aac_buf);
}

int main(int argc, char *argv[])
{
	int ret = 0;

	struct MP4Profile mp4_profile = {
		.name = OUTFILE,
		.video_time_scale = VIDEO_TIME_SCALE,
		.video_sample_duration = VIDEO_SAMPLE_DURATION,
		.audio_time_scale = AUDIO_TIME_SCALE,
		.audio_sample_duration = AUDIO_SAMPLE_DURATION,
		.width = RES_WIDTH,
		.height = RES_HEIGHT,
		.sps = sps_buf,
		.pps = pps_buf,
		.sps_len = SPS_LEN,
		.pps_len = PPS_LEN,
	};

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

	ret = create_aac_encoder(&aac_enc_hd,
				AUDIO_CHANNELS, AUDIO_SAMPLERATE, AAC_BITRATE,
				aac_decoder_conf, &aac_decoder_conf_len);
	if (ret < 0)
		goto exit;

	mp4_profile.aac_decoder_conf = aac_decoder_conf;
	mp4_profile.aac_decoder_conf_len = aac_decoder_conf_len;
	ret = create_mp4_muxer(&mp4_hd, &video_tk, &audio_tk, &mp4_profile);
	if (ret < 0)
		goto exit;

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
		goto out;

	while (!g_exit)
		sleep(1);

out:
	MYVideoInput_Uninit();
	MYAudioInputStop();
	MYAudioInputClose();

	MYAV_Context_Release();

exit:
	destroy_aac_encoder(&aac_enc_hd);
	destroy_mp4_muxer(mp4_hd);
	return ret;

}
