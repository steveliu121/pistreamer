/*
 * pi_errno.h
 * Copyright (C) 2018      Steve Liu<steveliu121@163.com>
 *
 */

#ifndef _PI_ERRNO_H
#define _PI_ERRNO_H

enum PI_ERRRNO {
	PI_OK = 0,
	PI_FAIL,
	PI_E_NULL_POINT = 10,
	PI_E_NO_MEMORY,
	PI_E_NOT_FOUND,
	PI_E_FULL,
	PI_E_EMPTY,
	PI_E_EXIST,
	PI_E_NOT_EXIST,
	PI_E_OPEN_FAIL,
	PI_E_CLOSE_FAIL,
	PI_E_DECOCE_DAIL,
	PI_E_FILL_FAIL,
	PI_E_INVALID_PARAM,
	PI_E_THREAD_FAIL,
	PI_E_UNKONW,
};

#endif
