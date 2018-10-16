/*
 * piavchn.c
 * Copyright (C) 2018      Steve Liu<steveliu121@163.com>
 *
 */

#include <stdlib.h>

#include <libavformat/avformat.h>

#include "pi_errno.h"
#include "list.h"
#include "piavisp.h"


static struct pi_chn_list g_chn_list;

/* TODO called when pi_av_init*/
/* init list head & the last null list node(channel number == 1024)*/
int  __chn_list_init()
{
	struct pi_av_chn_node *chn_node = NULL;

	LIST_HEAD_INIT(g_chn_list.head);
	g_chn_list.max_operator_num = MAX_CHANNEL_NUM;

	chn_node = (struct pi_av_chn_node *)malloc(sizeof(struct pi_av_chn_node));
	if (chn_node == NULL) {
		printf("Malloc channel node fail\n");
		return -PI_E_NO_MEMORY;
	}

	chn_node->chnno = g_chn_list.max_operator_num;
	chn_node->type = PI_AV_ID_NULL;
	chn_node->stat = 0;
	chn_node->attr = NULL;
	chn_node->ctx = NULL;

	list_add_tail(&chn_node->list, &g_chn_list.head);
}

static int __find_idle_chnno()
{
	int start= 0;
	struct pi_av_chn_node *chn_node;

	list_for_each_entry(chn_node, &g_chn_list.head, list) {
		if (start == g_chn_list.max_operator_num) {
			printf("Channel operator is full\n");
			return -PI_E_NOT_FOUND;
		}
		if (start < chn_node->chnno)
			return start;
		if (start == chn_node->chnno)
			start++;
	}

	return -PI_E_NO_FOUND;
}

struct pi_av_chn_node *__find_chn_by_chnno(int chnno)
{
	struct pi_av_chn_node *chn_node;

	list_for_each_entry(chn_node, &g_chn_list.head, list) {
		if (chn_node->chnno == chnno)
			return chn_node;
	}

	return NULL;
}

static void __add_chn(struct pi_av_chn_node *chn_node)
{
	struct pi_av_chn_node *cur_node = NULL;

	list_for_each_entry(chn_node, &g_chn_list.head, list) {
		if (chn_node->chnno < cur_node->chnno)
			list_add_tail(&chn_node->list, &cur_node->list);
	}
}

int __create_chn(int id, void *attr, void *ctx)
{
	struct pi_av_chn_node *chn_node = NULL;
	int chnno;
	int ret = 0;

	chn_node = (struct pi_av_chn_node *)malloc(sizeof(struct pi_av_chn_node));
	if (chn_node == NULL) {
		printf("Malloc channel node fail\n");
		return -PI_E_NO_MEMORY;
	}

	if (id == PI_AV_ID_VIDEO_ISP) {
		/* backup fattr, ctx no need backup(cause is malloced)*/
		struct pi_frame_attr *fattr == NULL;

		*fattr = *((struct pi_frame_attr *)attr);

		chnno = __find_idle_chnno();
		if (chnno < 0) {
			ret = -PI_E_NOT_FOUND;
			goto error;
		}

		chn_node->chnno = chnno;
		chn_node->type = PI_AV_ID_VIDEO_ISP;
		chn_node->stat = 1;
		chn_node->attr = fattr;
		chn_node->ctx = ctx;
		chn_node->buf_queue = NULL;
		chn_node->work_thread = NULL;
	}

	/*TODO*/
	if (id == PI_AV_ID_VIDEO_H264) {
	}
	if (id == PI_AV_ID_VIDEO_MJPEG) {
	}
	if (id == PI_AV_ID_AUDIO_CAPTURE_) {
	}
	if (id == PI_AV_ID_AUDIO_PLAYBACK) {
	}
	if (id == PI_AV_ID_AUDIO_ENCODER) {
	}
	if (id == PI_AV_ID_AUDIO_DECODER) {
	}

	__add_chn(chn_node);

	return chnno;
error:
	free(chn_node);
	return ret;
}

static void __del_chn(struct pi_av_chn_node *chn_node)
{
	struct pi_av_chn_node *chn_node;

	list_del(&chn_node->list);

	free(chn_node);
}

static int __destroy_chn(int chnno, struct pi_av_chn_node *chn)
{
	struct pi_av_chn_node *chn_node = NULL;

	if (chn == NULL) {
		chn_node =__find_chn_by_chnno(chnno);
		if (chn_node == NULL) {
			printf("Could not found channel[%]\n", chnno);
			return -PI_E_NO_FOUND;
		}
	} else
		chn_node = chn;

	if (chn_node->type != PI_AV_ID_NULL)
		free(chn_node->attr);

	if (chn_node->type == PI_AV_ID_VIDEO_ISP) {
		avformat_close_input(&(chn_node->ctx));
	}
	/*TODO*/
	if (chn_node->type == PI_AV_ID_VIDEO_H264) {
	}
	if (chn_node->type == PI_AV_ID_VIDEO_MJPEG) {
	}
	if (chn_node->type == PI_AV_ID_AUDIO_CAPTURE) {
	}
	if (chn_node->type == PI_AV_ID_AUDIO_PLAYBACK) {
	}
	if (chn_node->type == PI_AV_ID_AUDIO_ENCODER) {
	}
	if (chn_node->type == PI_AV_ID_AUDIO_DECODER) {
	}

	chn_node->stat = 0;
	chn_node->type = PI_AV_ID_NULL;
	chn_node->chnno = -1;

	__del_chn(chn_node);

	return PI_OK;
}

int __destory_chn_by_chnno(int chnno)
{
	return __destory_chn(chnno, NULL);
}

static int __destory_chn_by_chnnode(struct pi_av_chn_node * chn_node)
{
	return __destory_chn(-1, chn_node);
}

/* TODO called by pi_av_exit*/
void __chn_list_destroy()
{
	struct pi_av_chn_node *chn_node;
	struct pi_av_chn_node *tmp_node;

	list_for_each_entry_safe(chn_node, tmp_node , &g_chn_list.head, list)
		__destory_chn_by_chnnode(chn_node);
}
