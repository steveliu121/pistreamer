/*
 * aacenc.h based on fdk-aac
 *
 * Copyright (C) 2018      Steve Liu<steveliu121@163.com>
 */


#ifndef __AACENC_H__
#define __AACENC_H__


#include <fdk-aac/aacenc_lib.h>


/*
 * @[aac_decoder_conf]&[aac_decoder_conf_len] are output value and they are used
 * only by mp4muxer [MP4SetTrackESConfiguration], aac_decoder_conf should be
 * an [array] type [uint8_t] size [64]
 */
int create_aac_encoder(HANDLE_AACENCODER *aac_enc_hd,
		int channels, int samplerate, int bitrate,
		uint8_t *aac_decoder_conf, int *aac_decoder_conf_len);

/*
 * @aac_buf: output param
 * @return: length of aac output buffer on success, '0' on error
 */
int aac_encode(HANDLE_AACENCODER aac_enc_hd,
				const void *pcm_buf, int pcm_buf_len,
				uint8_t **aac_buf);

int destroy_aac_encoder(HANDLE_AACENCODER *aac_enc_hd);

#endif

