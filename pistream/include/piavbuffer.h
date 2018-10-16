/*
 * piavbuffer.h
 * Copyright (C) 2018      Steve Liu<steveliu121@163.com>
 *
 */

#ifndef _PIAVBUFFER_H
#define _PIAVBUFFER_H

#include <pthread.h>

enum buffer_type {
	ID_VIDEO_ISP = 0,
	ID_VIDEO_H264,
	ID_VIDEO_MJPEG,

	ID_AUDIO_CAPTURE,
	ID_AUDIO_PLAYBACK,
	ID_AUDIO_ENCODER,
	ID_AUDIO_DECODER,
};

typedef void (*buffer_recycle_f)(struct pi_av_buffer_ex *buffer_ex);

struct pi_av_buffer {
	void *vm_addr;
	int length;
	int type;
};

struct pi_av_buffer_ex {
	void *vm_addr;
	int length;
	int type;
	pthread_mutex_t mutex;
	buffer_recycle_f recycle;
	int user_count;
	struct pi_circle_queue *buf_queue;
};


#endif
