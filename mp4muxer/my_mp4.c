#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <stdint.h>
#include <pthread.h>
#include <errno.h>

#include <fdk-aac/aacenc_lib.h>
#include <mp4v2/mp4v2.h>

#include "../my_middle_media.h"
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


static const uint8_t sps[SPS_LEN] = {0x27, 0x64, 0x00, 0x29, 0xac, 0x1a, 0xd0, 0x0a,
			0x00, 0xb7, 0x4d, 0xc0, 0x40, 0x40, 0x50, 0x00,
			0x00, 0x03, 0x00, 0x10, 0x00 ,0x00, 0x03, 0x01,
			0xe8, 0xf1 ,0x42, 0x2a};
static const uint8_t pps[PPS_LEN] = {0x28, 0xee, 0x01, 0x34, 0x92, 0x24};
/*
static const uint8_t sps_buf[SPS_LEN + 4] = {0x00, 0x00, 0x00, 0x1c, 0x27, 0x64,
			0x00, 0x29, 0xac, 0x1a, 0xd0, 0x0a,
			0x00, 0xb7, 0x4d, 0xc0, 0x40, 0x40, 0x50, 0x00,
			0x00, 0x03, 0x00, 0x10, 0x00 ,0x00, 0x03, 0x01,
			0xe8, 0xf1 ,0x42, 0x2a};
static const uint8_t pps_buf[PPS_LEN + 4] = {0x00, 0x00, 0x00, 0x06, 0x28, 0xee,
			0x01, 0x04, 0x92, 0x24};
			*/
/* get from aac encoder */
static uint8_t aac_decoder_conf[64] = {0};
static int aac_decoder_conf_len;
static MP4FileHandle mp4_hd;
static MP4TrackId video_tk;
static MP4TrackId audio_tk;
static HANDLE_AACENCODER aac_enc_hd;
static uint8_t *aac_out_buf;
static int aac_out_buf_len;
static int aac_out_payload_len;
static uint8_t *aac_in_buf;
/* a const value needed by aac encoder */
static int aac_in_buf_len;
static int aac_in_payload_len;
/* h264_buf dosen't contain sps/pps */
static uint8_t *h264_buf;
static int h264_buf_len;
static int k_frame;
pthread_mutex_t mp4_mutex;
static int g_exit;


void sig_handle(int sig)
{
	g_exit = 1;
}

static int __create_mp4_muxer(void)
{
	mp4_hd = MP4_INVALID_FILE_HANDLE;
	video_tk = MP4_INVALID_TRACK_ID;
	audio_tk = MP4_INVALID_TRACK_ID;

	/* default setting */
	mp4_hd = MP4CreateEx(OUTFILE, 0, 1, 1, 0, 0, 0, 0);
	if (mp4_hd == MP4_INVALID_FILE_HANDLE)
		goto exit;

	MP4SetTimeScale(mp4_hd, VIDEO_TIME_SCALE);

	video_tk = MP4AddH264VideoTrack(mp4_hd, VIDEO_TIME_SCALE,
					VIDEO_SAMPLE_DURATION, RES_WIDTH,
					RES_HEIGHT,
					sps[1],// AVCProfileIndication
					sps[2],// profile_compat
					sps[3],// AVCLevelIndication
					3); // 4 bytes length before each NALU
	if (video_tk == MP4_INVALID_TRACK_ID)
		goto exit;

	audio_tk = MP4AddAudioTrack(mp4_hd, AUDIO_TIME_SCALE,
					AUDIO_SAMPLE_DURATION,
					MP4_MPEG4_AUDIO_TYPE);
	if (audio_tk == MP4_INVALID_TRACK_ID)
		goto exit;

	MP4AddH264SequenceParameterSet(mp4_hd, video_tk, sps, SPS_LEN);
	MP4AddH264PictureParameterSet(mp4_hd, video_tk, pps, PPS_LEN);
	/* 0x7f a reserved value, you can refer to linux man */
	/* https://linux.die.net/man/3/mp4 */
	MP4SetVideoProfileLevel(mp4_hd, 0x7f);
	MP4SetAudioProfileLevel(mp4_hd, 0x7f);
	/* set audio decoder configuration */
	if (aac_decoder_conf_len != 0)
		MP4SetTrackESConfiguration(mp4_hd, audio_tk,
					aac_decoder_conf, aac_decoder_conf_len);
	else {
		printf("Please init aac encoder before create mp4 muxer...\n");
		goto exit;
	}

//	pthread_mutex_init(&mp4_mutex, NULL);

	printf("Create MP4 muxer success...\n");

	return 0;

exit:
	printf("Init mp4 fail\n");
	return -1;
}

static void __destroy_mp4_muxer(void)
{
	MP4Close(mp4_hd, 0);
	if (h264_buf != NULL) {
		free(h264_buf);
		h264_buf = NULL;
	}

	printf("Destroy MP4 muxer success...\n");
}

/* @caution: only when the sum of input pcm_buf len >
 * aac encoder minimum input buf len will aac encoder
 * generate out aac buf
 * @here the pcm_buf_len is fix to 1024 bytes
 * (which is half of the aac encoder minimum input buf len)
 * @return: aac frame length
 */
static int __aac_encode_a_frame(HANDLE_AACENCODER enc,
				void *pcm_buf, int pcm_buf_len,
				void *aac_buf, int aac_buf_len)
{
	AACENC_BufDesc in_buf = {0}, out_buf = {0};
	AACENC_InArgs in_args = {0};
	AACENC_OutArgs out_args = {0};
	int in_identifier = IN_AUDIO_DATA;
	int in_elem_size;
	int out_identifier = OUT_BITSTREAM_DATA;
	int out_elem_size;
	int ret = 0;

	in_elem_size = 2;	//bitfmt 16 bits width
	out_elem_size = 1;

	in_args.numInSamples = pcm_buf_len / 2;		//bitfmt 16 bits width
	in_buf.numBufs = 1;
	in_buf.bufs = &pcm_buf;
	in_buf.bufferIdentifiers = &in_identifier;
	in_buf.bufSizes = &pcm_buf_len;
	in_buf.bufElSizes = &in_elem_size;

	out_buf.numBufs = 1;
	out_buf.bufs = &aac_buf;
	out_buf.bufferIdentifiers = &out_identifier;
	out_buf.bufSizes = &aac_buf_len;
	out_buf.bufElSizes = &out_elem_size;

	ret = aacEncEncode(enc, &in_buf, &out_buf, &in_args, &out_args);
	if (ret != AACENC_OK) {
		if (ret == AACENC_ENCODE_EOF) {
			printf("AAC encode EOF...\n");
			return 0;
		}

		printf("AAC encode a frame fail\n");
		return 0;
	}

	return out_args.numOutBytes;
}

static int __aac_encode(HANDLE_AACENCODER enc,
				const void *pcm_buf, int pcm_buf_len,
				uint8_t *aac_input_buf, int aac_input_buf_len,
				int *aac_in_payload_len,
				uint8_t *aac_output_buf, int aac_output_buf_len,
				int *aac_out_payload_len)
{
	int ret = 0;
	int buf_over_len = 0;
	int pcm_payload_len = 0;
	int append_pcm_len = 0;

	*aac_out_payload_len = 0;

	pcm_payload_len = *aac_in_payload_len + pcm_buf_len;

	if (pcm_payload_len > aac_input_buf_len) {
		buf_over_len = pcm_payload_len - aac_input_buf_len;
		if (buf_over_len >  aac_input_buf_len) {
			printf("!!!!!!Audio input pcm frame is too huge "
				"so crop to aac_input_buf_len!!!!!!\n");
			buf_over_len = aac_input_buf_len;
		}
		append_pcm_len = aac_input_buf_len - *aac_in_payload_len;
	} else
		append_pcm_len = pcm_buf_len;

	memcpy(aac_input_buf + *aac_in_payload_len, pcm_buf, append_pcm_len);

	if (pcm_payload_len < aac_input_buf_len) {
		printf("AAC Skip once cause input buf not full\n");
		*aac_in_payload_len = *aac_in_payload_len + append_pcm_len;

		return 0;
	}

	*aac_out_payload_len = __aac_encode_a_frame(aac_enc_hd, aac_input_buf,
							aac_input_buf_len,
							aac_output_buf,
							aac_output_buf_len);

	if (buf_over_len) {
		memcpy(aac_input_buf, pcm_buf + append_pcm_len, buf_over_len);
		*aac_in_payload_len = buf_over_len;
	} else
		*aac_in_payload_len = 0;

	return ret;
}

static int __create_aac_encoder(void)
{
	int ret = 0;
	int i;
	int enc_modules = 0x01;//AAC_LC low complexity
	AACENC_InfoStruct aac_enc_info = {0};


	ret = aacEncOpen(&aac_enc_hd, enc_modules, AUDIO_CHANNELS);
	if (ret != AACENC_OK) {
		printf( "Open AAC encoder fail\n");
		goto exit;
	}

	ret = aacEncoder_SetParam(aac_enc_hd, AACENC_AOT, AOT_AAC_LC);
	if (ret != AACENC_OK) {
		printf( "Set AAC AOT fail\n");
		goto exit;
	}

	ret = aacEncoder_SetParam(aac_enc_hd, AACENC_SAMPLERATE, AUDIO_SAMPLERATE);
	if (ret != AACENC_OK) {
		printf( "Set AAC SAMPLERATE fail\n");
		goto exit;
	}

	ret = aacEncoder_SetParam(aac_enc_hd, AACENC_CHANNELMODE, MODE_1);
	if (ret != AACENC_OK) {
		printf( "Set AAC CHANNELMODE fail\n");
		goto exit;
	}

	/* channel order: MPEG order by default */
/*
	ret = aacEncoder_SetParam(aac_enc_hd, AACENC_CHANNELORDER, 0);
	if (ret != AACENC_OK) {
		printf( "Set AAC CHANNELMODE fail\n");
		goto exit;
	}
*/

	/* bitrate mode: CBR by default */
/*
	ret = aacEncoder_SetParam(aac_enc_hd, AACENC_BITRATEMODE, 0)
	if (ret != AACENC_OK) {
		printf( "Set AAC BITRATEMODE fail\n");
		goto exit;
	}
*/

	ret = aacEncoder_SetParam(aac_enc_hd, AACENC_BITRATE, AAC_BITRATE);
	if (ret != AACENC_OK) {
		printf( "Set AAC BITRATE fail\n");
		goto exit;
	}

	ret = aacEncoder_SetParam(aac_enc_hd, AACENC_TRANSMUX, TT_MP4_ADTS);
	if (ret != AACENC_OK) {
		printf( "Set AAC TRANSPORT TYPE fail\n");
		goto exit;
	}

	/* afterburner is disbaled by default */
/*
	ret = aacEncoder_SetParam(aac_enc_hd, AACENC_AFTERBURNER, 0);
	if (ret != AACENC_OK) {
		printf( "Set AAC TRANSPORT TYPE fail\n");
		goto exit;
	}
*/

	ret = aacEncEncode(aac_enc_hd, NULL, NULL, NULL, NULL);
	if (ret != AACENC_OK) {
		printf( "Initialize AAC encoder fail\n");
		goto exit;
	}

	ret = aacEncInfo(aac_enc_hd, &aac_enc_info);
	if (ret != AACENC_OK) {
		printf( "Get AAC encoder fail\n");
		goto exit;
	} else {
		printf("AAC max_out_buffer_len[%d], input_pcm_frame_len[%d]\n",
				aac_enc_info.maxOutBufBytes,
				aac_enc_info.frameLength * AUDIO_CHANNELS * 2);
		aac_out_buf_len = aac_enc_info.maxOutBufBytes;
		aac_out_buf = calloc(1, aac_out_buf_len);
		if (aac_out_buf == NULL) {
			printf("Malloc aac_out_buf fail\n");
			goto exit;
		}

		aac_in_buf_len = aac_enc_info.frameLength * AUDIO_CHANNELS * 2;
		aac_in_buf = calloc(1, aac_in_buf_len);
		if (aac_in_buf == NULL) {
			printf("Malloc aac_in_buf fail\n");
			goto exit;
		}

		for (i = 0; i < aac_enc_info.confSize; i++) {
			aac_decoder_conf[i] = aac_enc_info.confBuf[i];
			printf(".....AAC encoder conf [0x%x]\n",
					aac_enc_info.confBuf[i]);
		}
		aac_decoder_conf_len = aac_enc_info.confSize;
	}

	printf("Init AAC encoder success...\n");

	return 0;

exit:
	printf("Init AAC encoder fail\n");
	return -1;
}

static int __destroy_aac_encoder(void)
{
	int ret = 0;

	ret = aacEncClose(&aac_enc_hd);
	if (ret != AACENC_OK) {
		printf("Destroy AAC encoder fail...\n");
		ret = -1;
	} else
		printf("Destroy AAC encoder success...\n");

	if (aac_out_buf != NULL) {
		free(aac_out_buf);
		aac_out_buf = NULL;
	}
	if (aac_in_buf != NULL) {
		free(aac_in_buf);
		aac_in_buf = NULL;
	}

	return ret;
}

/* use the current pcm frame timestamp
 * to calculate the previous aac frame's duration
 * and write the aac frame to mp4 file
 */

/* XXX: [__mp4_pack_aac] share the same buf with [__aac_encode_a_frame] */
static int __mp4_pack_aac(MP4FileHandle mp4_hd, void *buf,
				int buf_len, const struct timeval *tv)
{
	static struct timeval tv_before;
	int time_duration = 0;
	static int first_frame = 1;
	int ret = 0;

	if ((buf_len == 0) && !first_frame)
		return 0;

	/* actually every audio frame duration is fixed to AUDIO_SAMPLE_DURATION
	 * cause it's pcm period time is fixed,
	 * and usually audio will not lose any frame
	 */
	if (first_frame) {
		/* first frame is a pcm frame, we just record it's timestamp */
		first_frame = 0;
		tv_before.tv_sec = tv->tv_sec;
		tv_before.tv_usec = tv->tv_usec;

		return 0;
	} else {
		time_duration = (AUDIO_TIME_SCALE / 1000) *
				((tv->tv_sec * 1000 + tv->tv_usec / 1000) -
				 (tv_before.tv_sec * 1000 + tv_before.tv_usec /1000));
		tv_before.tv_sec = tv->tv_sec;
		tv_before.tv_usec = tv->tv_usec;
	}

//	pthread_mutex_lock(&mp4_mutex);

	/* remove aac ADTS(7 bytes len) from aac buf */
	ret = MP4WriteSample(mp4_hd, audio_tk, buf + 7, buf_len - 7,
						time_duration, 0, 1);
	if (!ret) {
		printf("Pack aac frame to mp4 file fail\n");
		ret = -1;
	}

//	pthread_mutex_unlock(&mp4_mutex);

	return ret;
}

static int __cache_h264_a_frame(uint8_t *buf, int buf_len, int keyframe)
{
	uint8_t *i_frame_buf = NULL;
	int pay_load_len = 0;

	if (keyframe) {
		k_frame = 1;
		h264_buf_len = buf_len - SPS_LEN - PPS_LEN - (2 * 4);
		pay_load_len = h264_buf_len - 4;
		h264_buf = calloc(1, h264_buf_len);
		if (h264_buf == NULL) {
			printf("Malloc h264 cache buf fail\n");
			return -1;
		}


		i_frame_buf = buf + SPS_LEN + PPS_LEN + 2 * 4;
		i_frame_buf[0] = pay_load_len  >> 24;
		i_frame_buf[1] = pay_load_len >> 16;
		i_frame_buf[2] = pay_load_len >> 8;
		i_frame_buf[3] = pay_load_len & 0xff;
		memcpy(h264_buf, i_frame_buf, h264_buf_len);
	} else {
		k_frame = 0;
		h264_buf_len = buf_len;
		pay_load_len = h264_buf_len - 4;
		h264_buf = calloc(1, h264_buf_len);
		if (h264_buf == NULL) {
			printf("Malloc h264 cache buf fail\n");
			return -1;
		}

		buf[0] = pay_load_len >> 24;
		buf[1] = pay_load_len >> 16;
		buf[2] = pay_load_len >> 8;
		buf[3] = pay_load_len & 0xff;
		memcpy(h264_buf, buf, h264_buf_len);
	}

	return 0;
}

static int __mp4_pack_h264(MP4FileHandle mp4_hd, uint8_t **buf,
				int *buf_len, const struct timeval *tv,
				int keyframe)
{
	static struct timeval tv_before;
	int time_duration = 0;
	static int first_frame = 1;
	int ret = 0;

	if (*buf == NULL && !first_frame)
		return -1;

	if (first_frame) {
		/* first frame just record it's timestamp */
		first_frame = 0;
		tv_before.tv_sec = tv->tv_sec;
		tv_before.tv_usec = tv->tv_usec;

		return 0;
	} else {
		time_duration = (VIDEO_TIME_SCALE / 1000) *
				((tv->tv_sec * 1000 + tv->tv_usec / 1000) -
				 (tv_before.tv_sec * 1000 + tv_before.tv_usec /1000));
		tv_before.tv_sec = tv->tv_sec;
		tv_before.tv_usec = tv->tv_usec;
	}

//	pthread_mutex_lock(&mp4_mutex);

	/* mp4 chunk data dosen't need sps/pps nalu, but only frame data,
	 * so you should strip sps/pps nalu from I frame
	 */
//	if (keyframe) {
//		ret = MP4WriteSample(mp4_hd, video_tk, sps_buf, SPS_LEN + 4,
//							time_duration, 0, keyframe);
//		if (!ret)
//			goto err;
//		ret = MP4WriteSample(mp4_hd, video_tk, pps_buf, PPS_LEN + 4,
//							time_duration, 0, keyframe);
//		if (!ret)
//			goto err;
//		printf("~~~~~~pack sps/pps to mp4\n");
//	}

	ret = MP4WriteSample(mp4_hd, video_tk, *buf, *buf_len,
						time_duration, 0, keyframe);

//	pthread_mutex_unlock(&mp4_mutex);

//err:
	if (!ret) {
		printf("Pack h264 frame to mp4 file fail\n");
		ret = -1;
	}

	free(*buf);
	*buf = NULL;
	*buf_len = 0;

	return ret;
}

void h264_cb(const struct timeval *tv, const void *data,
	const int len, const int keyframe)
{
	__mp4_pack_h264(mp4_hd, &h264_buf, &h264_buf_len, tv, k_frame);

	__cache_h264_a_frame((uint8_t *)data, len, keyframe);
}

void audio_cb(const struct timeval *tv, const void *pcm_buf,
	const int pcm_len, const void *spk_buf)
{
	__mp4_pack_aac(mp4_hd, aac_out_buf, aac_out_payload_len, tv);

	__aac_encode(aac_enc_hd, pcm_buf, pcm_len,
			aac_in_buf, aac_in_buf_len, &aac_in_payload_len,
			aac_out_buf, aac_out_buf_len,&aac_out_payload_len);
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

	ret = __create_aac_encoder();
	if (ret < 0)
		goto exit;

	ret = __create_mp4_muxer();
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
	__destroy_aac_encoder();
	__destroy_mp4_muxer();
	return ret;

}
