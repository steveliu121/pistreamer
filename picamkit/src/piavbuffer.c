/*
 * piavbuffer.c
 * Copyright (C) 2018      Steve Liu<steveliu121@163.com>
 *
 */

#include <stdlib.h>
#include <stdio.h>

#include <libavformat/avformat.h>

#include "piavchn.h"
#include "piavbuffer.h"
#include "pi_errno.h"

void buffer_recycle(struct pi_av_buffer_ex *buffer_ex)
{
	pi_circle_queue_pop(buffer_ex->buf_queue);
}

void *__isp_buf_work_thread(void *arg)
{
	struct pi_av_chn_node *chn_node = NULL;
	AVPacket *packet = NULL;
	AVFormatContext *ctx = NULL;
	AVFrame *pframe = NULL;
	struct pi_av_buffer_ex *buffer_ex = NULL;
	int run_tmp = 0;
	int ret = 0;

	chn_node = (struct pi_av_chn_node *)arg;
	ctx = (AVFormatContext *)chn_node->ctx;
	packet = (AVPacket *)av_malloc(sizeof(AVPacket));
	if (packet == NULL)
		return NULL;

	while (chn_node->stat) {
		if (!chn_node->buf_queue->run) {
			/* clean up buffer queue once stop receive*/
			if (run_tmp == 1)
				pi_circle_queue_empty(chn_node->buf_queue);
			run_tmp = 0;
			usleep(1000);
			continue;
		}

		run_tmp = 1;

		while (av_read_frame(ctx, packet) < 0)
			usleep(1000);

		while (pi_circle_queue_fake_push(chn_node->buf_queue, &buffer_ex) < 0)
			usleep(1000);

		if (buffer_ex->length != packet->size)
			printf("ERROR isp buffer length is wrong\n");

		pframe = (AVFrame *)(buffer_ex->vm_addr);

		memcpy(pframe->data, packet->data, packet->size);
		av_free_packet(packet);

		pi_circle_queue_push(chn_node->buf_queue, buffer_ex);
	}

	av_free(packet);
}

static int __init_isp_buf_queue(struct pi_av_chn_node *chn_node)
{
	struct pi_frame_attr *fattr = NULL;
	uint8_t *frame_data = NULL;
	AVFrame **pframe = NULL;
	struct pi_av_buffer_ex *buffer_ex = NULL;
	int frame_len = 0;
	int ret = 0;

	fattr = (struct pi_frame_attr *)(chn_node->attr);	int enable;

	chn_node->buf_queue->size = fattr->buf_num;

	ret = pi_circle_queue_create(chn_node->buf_queue);
	if (ret < 0)
		return ret;

	buffer_ex = (struct pi_av_buffer_ex *)calloc(chn_node->buf_queue->size,
						sizeof(struct pi_av_buffer_ex));
	if (buffer_ex == NULL) {
		ret = -PI_E_NO_MEMORY;
		goto error;
	}

	frame_len = avpicture_get_size(fattr->pixle_fmt, fattr->width,
								fattr->height);
	frame_data = (uint8_t *)calloc(chn_node->buf_queue->size, frame_len);
	if (frame_data == NULL) {
		ret = -PI_E_NO_MEMORY;
		goto error;
	}

	pframe == (AVFrame **)calloc(chn_node->buf_queue->size,
						sizeof (AVFrame *));
	if (pframe == NULL) {
		ret = -PI_E_NO_MEMORY;
		goto error;
	}


	for (i = 0; i < chn_node->buf_queue->size; i++) {
		pframe[i] = av_frame_alloc();
		if (pframe[i] == NULL) {
			ret = -PI_E_NO_MEMORY;
			goto error;
		}
		ret = avpicture_fill((AVPicture *)pframe[i], (frame_data + i),
						fattr->pixle_fmt, fattr->width,
								fattr->height);
		if (ret < 0) {
			ret = -PI_E_FILL_FAIL;
			goto error;
		}
	}

	for (i = 0; i < chn_node->buf_queue->size; i++) {
		buffer_ex[i].vm_addr = pframe[i];
		buffer_ex[i].length = 0;
		buffer_ex[i].type = ID_VIDEO_ISP;
		pthread_mutex_init(&buffer_ex[i].mutex, NULL);
		buffer_ex[i].recycle = buffer_recycle;
		buffer_ex[i].user_count = 0;
		buffer_ex[i].buf_queue = chn_node->buf_queue;

		pi_circle_queue_push(chn_node->buf_queue, &buffer_ex[i]);
	}

	pi_circle_queue_empty(chn_node->buf_queue);

	ret = pthread_create(&(chn_node->work_thread), NULL,
			__isp_buf_work_thread, chn_node->buf_queue);
	if (ret < 0) {
		ret = -PI_E_THREAD_FAIL;
		goto error;
	}

	free(pframe);

	return PI_OK;

error:
	if (frame_data)
		free(frame_data);

	for (i = 0; i < chn_node->buf_queue->size; i++) {
		if (pframe[i] != NULL)
			av_frame_free(pframe[i]);
	}

	if (pframe)
		free(pframe);

	if (buffer_ex)
		free(buffer_ex);

	pi_circle_queue_destroy(chn_node);

	printf("Init isp buffer queue fail\n");
	return ret;
}

static void __destroy_isp_buf_queue(struct pi_av_chn_node *chn_node)
{
	AVFrame *pframe = NULL;
	struct pi_av_buffer_ex *buffer_ex = NULL;

	/* wait work thread exit*/
	chn_node->buf_queue->run = 0;
	chn_node->stat = 0;

	pthread_join(chn_node->work_thread, NULL);

	pi_circle_queue_full(chn_node->buf_queue);

	pi_circle_queue_fake_pop(chn_node->buf_queue, &buffer_ex);
	pframe = (AVFrame *)(buffer_ex->vm_addr);
	/*TODO dose av_free_frame() free it's data?*/

	free(pframe->data);

	for (i = 0; i < chn_node->buf_queue->size; i++) {
		pi_circle_queue_pop(chn_node->buf_queue, &buffer_ex);
		pframe = (AVFrame *)(buffer_ex->vm_addr);
		av_frame_free(pframe);
		free(buffer_ex);
	}

}

static void __destroy_buf_queue(struct pi_av_chn_node *chn_node)
{
	switch (chn_node->type) {
		case PI_AV_ID_VIDEO_ISP:
			__destroy_isp_buf_queue(chn_node);
			break;
		/*TODO*/
		case PI_AV_ID_VIDEO_H264:
		case PI_AV_ID_VIDEO_MJPEG:
		case PI_AV_ID_AUDIO_CAPTURE:
		case PI_AV_ID_AUDIO_PLAYBACK:
		case PI_AV_ID_AUDIO_ENCODER:
		case PI_AV_ID_AUDIO_DECODER:
	}

	free(chn_node->buf_queue);
}

static int __init_buf_queue(struct pi_av_chn_node *chn_node)
{
	struct pi_circle_queue *queue = NULL;
	int ret = 0;

	queue = (struct pi_circle_queue)malloc(sizeof(struct pi_circle_queue));
	if (queue == NULL)
		return -PI_E_NO_MEMORY;

	chn_node->buf_queue = queue;

	switch (chn_node->type) {
		case PI_AV_ID_VIDEO_ISP:
			ret = __init_isp_buf_queue(chn_node);
			break;
		/*TODO*/
		case PI_AV_ID_VIDEO_H264:
		case PI_AV_ID_VIDEO_MJPEG:
		case PI_AV_ID_AUDIO_CAPTURE:
		case PI_AV_ID_AUDIO_PLAYBACK:
		case PI_AV_ID_AUDIO_ENCODER:
		case PI_AV_ID_AUDIO_DECODER:
		default:
			ret = -PI_E_UNKNOW;
	}

	if (ret < 0)
		free(queue);

	return ret;
}

int __destroy_buf_queue_manager(int chnno)
{
	struct pi_av_chn_node *chn_node = NULL;
	int ret = 0;

	chn_node = __find_chn_by_chnno(chnno);
	if (chn_node == NULL)
		return -PI_E_NOT_FOUND;

	__destroy_buf_queue(chn_node);

	return PI_OK;
}

int __init_buf_queue_manager(int chnno)
{
	struct pi_av_chn_node *chn_node = NULL;
	int ret = 0;

	chn_node = __find_chn_by_chnno(chnno);
	if (chn_node == NULL)
		return -PI_E_NOT_FOUND;

	ret = __init_buf_queue(chn_node);

	return ret;
}
