/*
 * piavchn.h
 * Copyright (C) 2018      Steve Liu<steveliu121@163.com>
 *
 */

#ifndef _PIAVCHN_H
#define _PIAVCHN_H

#include "pi_circle_queue.h"

enum chn_type {
	/* video */
	PI_AV_ID_VIDEO_ISP,
	PI_AV_ID_VIDEO_H264,
	PI_AV_ID_VIDEO_MJPEG,

	/* audio */
	PI_AV_ID_AUDIO_CAPTURE,
	PI_AV_ID_AUDIO_PLAYBACK,
	PI_AV_ID_AUDIO_ENCODER,
	PI_AV_ID_AUDIO_DECODER,

	PI_AV_ID_NULL,
};

struct pi_av_chn_node {
	struct list_head list;
	int chnno;
	enum chn_type type;
	int stat; /* 0,disable/1,enable */
	void *attr;
	void *ctx;
	struct pi_av_circle_queue *buf_queue;
	pthread_t work_thread;
};

/* channel number range:0~1023, channel 1024 is null */
#define MAX_CHANNEL_NUM 1024

struct pi_chn_list {
	struct list_head head;
	int max_operator_num;
};

int  __chn_list_init();
void __chn_list_destroy();
struct pi_av_chn_node *__find_chn_by_chnno(int chnno);
int __create_chn(int id, void *attr, void *ctx);
int __destory_chn_by_chnno(int chnno);

#endif
