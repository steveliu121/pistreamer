/*
 * piavisp.h
 * Copyright (C) 2018      Steve Liu<steveliu121@163.com>
 *
 */
#ifndef _PIAVISP_H
#define _PIAVISP_H


struct pi_frame_attr {
	AVPixelFormat pixel_fmt;
	int width;
	int height;
	int fps;
	int buf_num;
};


#endif
