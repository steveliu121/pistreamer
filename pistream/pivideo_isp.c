/*
 * video_yuv.cpp
 * Copyright (C) 2018      Steve Liu<steveliu121@163.com>
 *
 * get yuv420p data based on FFMPEG, worked on linux
 */

/* TODO av_log*/
/* TODO one isp channel stream*/
/* TODO multiply isp stream forked from main isp stream using pi_av_isp_scale*/
#include <stdio.h>

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libavdevice/avdevice.h>

#include "pi_errno.h"

/* there's only one isp channel -- /dev/video0*/
int pi_av_create_isp_chn(struct pi_frame_attr *fattr)
{
	char *pixle_fmt = NULL; //convert from pixel_fmt
	char *resolution = NULL; //convert from width height

	switch (fattr->pixel_fmt) {
		case AV_PIX_FMT_YUV420P:
			pixle_fmt = "yuv420p";
			break;
		case AV_PIX_FMT_NV12:
			pixle_fmt = "nv12";
			break;
		default:
			pixle_fmt = "yuv420p";
	}

	sprintf(resolution, "%dx%d", fattr->width, fattr->height);

	AVFormatContext *pFormatCtx = NULL;
	AVFrame *pFrameRaw = NULL;
	AVInputFormat *ifmt = NULL;
	AVDicitonary *options = NULL;
	int ret = 0;

	pFormatCtx = avformat_alloc_context();

	ifmt = av_find_input_format("video4linux2");
	if (ifmt == NULL) {
		printf("Couldn't find input format [v4l2].\n");
		ret = -PI_E_NOT_FOUND;
		goto error;
	}

	av_dict_set(&options, "video_size", resolution, 0);
	av_dict_set(&options, "pixel_format", pixel_fmt, 0);
	av_dict_set(&options, "framerate", fattr->fps, 0);

	ret = avformat_open_input(&pFormatCtx, "/dev/video0", ifmt, &options);
	if(ret < 0) {
		printf("Couldn't open input stream.\n");
		ret = -PI_E_NOT_EXIST;
		goto error;
	}

	ret = avformat_find_stream_info(pFormatCtx,NULL);
	if(ret < 0) {
		printf("Couldn't find stream information.\n");
		ret = -PI_E_NOT_FOUND;
		goto error;
	}

	return PI_OK;

error:
	avformat_close_input(&pFormatCtx);
}

int main(int argc, char* argv[])
{
	/* TODO API params*/
	struct pi_frame_attr *fattr;

	fattr->pixel_fmt = AV_PIX_FMT_YUV420P;
	fattr->width = 1280;
	fattr->height = 720;
	fattr->fps = 20;

	AVFormatContext *pFormatCtx = NULL;
	AVFrame *pFrameRaw = NULL;
	AVPacket *packet = NULL;
	AVInputFormat *ifmt = NULL;
	AVDicitonary *options = NULL;
	int i, videoindex = -1;
	int got_picture = 0;
	int ret = 0;

	av_register_all();
	avdevice_register_all();
	avformat_network_init();


	pFrameRaw = av_frame_alloc();
	if (pFrameRaw == NULL) {
		printf("Could not alloc AVFrame.\n");
		ret = -PI_E_NO_MEMORY;
		goto error;
	}

	packet = (AVPacket *)av_malloc(sizeof(AVPacket));
	if (packet == NULL) {
		printf("Could not alloc AVPacket.\n");
		ret = -PI_E_NO_MEMORY;
		goto error;
	}


	int frame_buf_len = 0;
	frame_buf_len = avpicure_get_size(fattr->pixle_fmt, fattr->width,
								fattr->height);

	if((av_read_frame(pFormatCtx, packet) == 0)
			&& (frame_buf_len == packet->size)){

		ret = avpicture_fill((AVPicture *)pFrameRaw, packet->data,
					fattr->pixle_fmt, fattr->width,
							fattr->height);
		if (ret < 0) {
			printf("Fill frame fail.\n");
			ret = -PI_E_FILL_FAIL;
			goto error;
		}
	}
	av_free_packet(packet);

error:
	av_free_packet(packet);
	av_frame_free(&pFrameRaw);

	return ret;
}


/* scale source image size & pixle format
 * convert source image(isp data) to dest image
 * release frame use av_frame_free()*/
int pi_av_isp_image_scale(AVFrame *pFrame_src, struct pi_frame_attr *fattr,
						AVFrame *pFrame_dst)
{
	int frame_buf_len = 0;
	uint8_t *frame_buf = NULL;
	struct SwsContext *img_convert_ctx = NULL;
	int ret = 0;

	if (pFrame_dst == NULL) {
		printf("Invalid param.\n");
		ret = -PI_E_INVALID_PARAM;
		goto error;
	}

	frame_buf_len = avpicure_get_size(fattr->pixle_fmt, fattr->width,
								fattr->height);
	frame_buf = (uint8_t *)av_malloc(frame_buf_len);
	if ((frame_buf == NULL) || (frame_buf_len == 0)) {
		printf("Could not alloc AVFrame buf[len=%d].\n",  frame_buf_len);
		ret = -PI_E_NO_MEMORY;
		goto error;
	}

	ret = avpicture_fill((AVPicture *)pFrame_dst, frame_buf,
					fattr->pixle_fmt, fattr->width,
							fattr->height);
	if (ret < 0) {
		printf("Fill frame fail.\n");
		ret = -PI_E_FILL_FAIL;
		goto error;
	}
	printf("####YUV linesize[0]:%d, linesize[1]:%d, linesize[2]:%d\n",
			pFrame_dst->linesize[0], pFrame_dst->linesize[1],
			pFrame_dst->linesize[2]);


	img_convert_ctx = sws_getContext(pFrame_src->width, pFrame_src->height,
					pFrame_src->format, fattr->width,
					fattr->height, fattr->pixle_fmt,
						SWS_BICUBIC, NULL, NULL, NULL);
	if (img_convert_ctx == NULL) {
		printf("Could not alloc SwsContext.\n");
		ret = -PI_E_NO_MEMORY;
		goto error;
	}

	ret = sws_scale(img_convert_ctx,
			(const unsigned char* const*)pFrame_src->data,
			pFrame_src->linesize, 0, pFrame_src->height,
			pFrame_dst->data, pFrame_dst->linesize);
	if (ret <= 0) {
		printf("Scale image fail\n");
		ret = -PI_E_UNKONW;
		return ret;
	}

	sws_freeContext(img_convert_ctx);
	return PI_OK;

error:
	av_free(frame_buf);
	av_frame_free(pFrame_dst);
	sws_freeContext(img_convert_ctx);
	return ret;
}

