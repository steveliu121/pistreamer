/*
 * mp4muxer.c based on mp4v2
 *
 * Copyright (C) 2018      Steve Liu<steveliu121@163.com>
 */


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>

#include <mp4v2/mp4v2.h>
#include "mp4muxer.h"


/* h264_buf has stripped sps/pps and
 * it's length has been changed to bigendian
 */
static uint8_t *h264_buf;
static int h264_buf_len;
static int k_frame;
static int g_sps_len;
static int g_pps_len;
static int g_video_time_scale;
static int g_audio_time_scale;


/*
 * @[mp4_hd][video_tk][audio_tk] output param
 * @return: '0' on success, '-1' on fail
 */
int create_mp4_muxer(MP4FileHandle *mp4_hd,
			MP4TrackId *video_tk, MP4TrackId *audio_tk,
			struct MP4Profile *mp4_profile)
{
	*mp4_hd = MP4_INVALID_FILE_HANDLE;
	*video_tk = MP4_INVALID_TRACK_ID;
	*audio_tk = MP4_INVALID_TRACK_ID;

	g_sps_len = mp4_profile->sps_len;
	g_pps_len = mp4_profile->pps_len;
	g_video_time_scale = mp4_profile->video_time_scale;
	g_audio_time_scale = mp4_profile->audio_time_scale;

	/* default setting */
	*mp4_hd = MP4CreateEx(mp4_profile->name, 0, 1, 1, 0, 0, 0, 0);
	if (*mp4_hd == MP4_INVALID_FILE_HANDLE)
		goto exit;

	MP4SetTimeScale(*mp4_hd, mp4_profile->video_time_scale);

	*video_tk = MP4AddH264VideoTrack(*mp4_hd, mp4_profile->video_time_scale,
					mp4_profile->video_sample_duration,
					mp4_profile->width, mp4_profile->height,
					mp4_profile->sps[1],// AVCProfileIndication
					mp4_profile->sps[2],// profile_compat
					mp4_profile->sps[3],// AVCLevelIndication
					3); // 4 bytes length before each NALU
	if (*video_tk == MP4_INVALID_TRACK_ID)
		goto exit;

	*audio_tk = MP4AddAudioTrack(*mp4_hd, mp4_profile->audio_time_scale,
					mp4_profile->audio_sample_duration,
					MP4_MPEG4_AUDIO_TYPE);
	if (*audio_tk == MP4_INVALID_TRACK_ID)
		goto exit;

	MP4AddH264SequenceParameterSet(*mp4_hd, *video_tk,
					mp4_profile->sps, mp4_profile->sps_len);
	MP4AddH264PictureParameterSet(*mp4_hd, *video_tk,
					mp4_profile->pps, mp4_profile->pps_len);
	/* 0x7f a reserved value, you can refer to linux man */
	/* https://linux.die.net/man/3/mp4 */
	MP4SetVideoProfileLevel(*mp4_hd, 0x7f);
	MP4SetAudioProfileLevel(*mp4_hd, 0x7f);
	/* set audio decoder configuration */
	if (mp4_profile->aac_decoder_conf_len != 0)
		MP4SetTrackESConfiguration(*mp4_hd, *audio_tk,
					mp4_profile->aac_decoder_conf,
					mp4_profile->aac_decoder_conf_len);
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

void destroy_mp4_muxer(MP4FileHandle mp4_hd)
{
	MP4Close(mp4_hd, 0);
	if (h264_buf != NULL) {
		free(h264_buf);
		h264_buf = NULL;
		h264_buf_len = 0;
	}

	printf("Destroy MP4 muxer success...\n");
}

static int __cache_h264_a_frame(uint8_t *buf, int buf_len, int keyframe)
{
	uint8_t *i_frame_buf = NULL;
	int pay_load_len = 0;

	if (keyframe) {
		k_frame = 1;
		h264_buf_len = buf_len - g_sps_len - g_pps_len - (2 * 4);
		pay_load_len = h264_buf_len - 4;
		h264_buf = calloc(1, h264_buf_len);
		if (h264_buf == NULL) {
			printf("Malloc h264 cache buf fail\n");
			return -1;
		}


		i_frame_buf = buf + g_sps_len + g_pps_len + 2 * 4;
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

/* @use the current pcm frame timestamp
 * to calculate the previous aac frame's duration
 * and write the aac frame to mp4 file
 * @[mp4_pack_aac] should be called paried with [aac_encode],
 * and [mp4_pack_aac] should called before [aac_encode]
 * in order to cache timestamp to calculate duration
 */

/* XXX: [mp4_pack_aac] share the same buf with [aac_encode] */
int mp4_pack_aac(MP4FileHandle mp4_hd, MP4TrackId audio_tk, void *buf,
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
		time_duration = (g_audio_time_scale / 1000) *
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

/* @h264_data: should be h264 raw data which contains sps/pps for I frame and
 * [0x00/0x00/0x00/0x01] NALU header for each NALU
 * @mp4_pack_h264: pack the previous one frame and
 * process & cache the current frame
 */
int mp4_pack_h264(MP4FileHandle mp4_hd, MP4TrackId video_tk,
				const struct timeval *tv,
				uint8_t *h264_data, int h264_data_len,
				int keyframe)
{
	static struct timeval tv_before;
	int time_duration = 0;
	static int first_frame = 1;
	int ret = 0;

	if (h264_buf == NULL && !first_frame) {
		ret = -1;
		goto exit;
	}

	if (first_frame) {
		/* first frame just record it's timestamp */
		first_frame = 0;
		tv_before.tv_sec = tv->tv_sec;
		tv_before.tv_usec = tv->tv_usec;

		ret = 0;
		goto exit;
	} else {
		time_duration = (g_video_time_scale / 1000) *
				((tv->tv_sec * 1000 + tv->tv_usec / 1000) -
				 (tv_before.tv_sec * 1000 + tv_before.tv_usec /1000));
		tv_before.tv_sec = tv->tv_sec;
		tv_before.tv_usec = tv->tv_usec;
	}

//	pthread_mutex_lock(&mp4_mutex);

	/* mp4 chunk data dosen't need sps/pps nalu, but only frame data,
	 * so you should strip sps/pps nalu from I frame
	 */
//	if (k_frame) {
//		ret = MP4WriteSample(mp4_hd, video_tk, sps_buf, SPS_LEN + 4,
//							time_duration, 0, k_frame);
//		if (!ret)
//			goto err;
//		ret = MP4WriteSample(mp4_hd, video_tk, pps_buf, PPS_LEN + 4,
//							time_duration, 0, k_frame);
//		if (!ret)
//			goto err;
//		printf("~~~~~~pack sps/pps to mp4\n");
//	}

	ret = MP4WriteSample(mp4_hd, video_tk, h264_buf, h264_buf_len,
					time_duration, 0, k_frame);

//	pthread_mutex_unlock(&mp4_mutex);

//err:
	if (!ret) {
		printf("Pack h264 frame to mp4 file fail\n");
		ret = -1;
	}

	free(h264_buf);
	h264_buf = NULL;
	h264_buf_len = 0;

exit:
	__cache_h264_a_frame(h264_data, h264_data_len, keyframe);

	return ret;
}
