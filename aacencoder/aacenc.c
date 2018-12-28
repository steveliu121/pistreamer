/*
 * aacenc.c based on fdk-aac
 *
 * Copyright (C) 2018      Steve Liu<steveliu121@163.com>
 */


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>

#include <fdk-aac/aacenc_lib.h>

/* XXX:WARNING the pcm period buf length should be the common factor of
 * the aac input pcm frame length, or the aac timestamp will be wrong
 * here the pcm period buf length == 1024 bytes,
 * and the aac input pcm frame length == 2048
 */


/* get from aac encoder */
static uint8_t *aac_out_buf;
static int aac_out_buf_size;
static int aac_out_payload_len;
static uint8_t *aac_in_buf;
static int aac_in_buf_size;
static int aac_in_payload_len;


/* @caution: only when the sum of input pcm_buf len >
 * aac encoder minimum input buf len will aac encoder
 * generate out aac buf
 * @here the pcm_buf_len is fix to 1024 bytes
 * (which is half of the aac encoder minimum input buf len)
 * @return: aac frame length
 */
static int __aac_encode_a_frame(HANDLE_AACENCODER aac_enc_hd)
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

	in_args.numInSamples = aac_in_buf_size / 2;		//bitfmt 16 bits width
	in_buf.numBufs = 1;
	in_buf.bufs = (void **)&aac_in_buf;
	in_buf.bufferIdentifiers = &in_identifier;
	in_buf.bufSizes = &aac_in_buf_size;
	in_buf.bufElSizes = &in_elem_size;

	out_buf.numBufs = 1;
	out_buf.bufs = (void **)&aac_out_buf;
	out_buf.bufferIdentifiers = &out_identifier;
	out_buf.bufSizes = &aac_out_buf_size;
	out_buf.bufElSizes = &out_elem_size;

	ret = aacEncEncode(aac_enc_hd, &in_buf, &out_buf, &in_args, &out_args);
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

/*
 * @aac_buf: output param
 * @return: length of aac output buffer on success, '0' on error
 */
int aac_encode(HANDLE_AACENCODER aac_enc_hd,
				const void *pcm_buf, int pcm_buf_len,
				uint8_t **aac_buf)
{
	int buf_over_len = 0;
	int pcm_payload_len = 0;
	int append_pcm_len = 0;

	*aac_buf = aac_out_buf;

	aac_out_payload_len = 0;

	pcm_payload_len = aac_in_payload_len + pcm_buf_len;

	if (pcm_payload_len > aac_in_buf_size) {
		buf_over_len = pcm_payload_len - aac_in_buf_size;
		if (buf_over_len >  aac_in_buf_size) {
			printf("!!!!!!Audio input pcm frame is too huge "
				"so crop to aac_in_buf_size!!!!!!\n");
			buf_over_len = aac_in_buf_size;
		}
		append_pcm_len = aac_in_buf_size - aac_in_payload_len;
	} else
		append_pcm_len = pcm_buf_len;

	memcpy(aac_in_buf + aac_in_payload_len, pcm_buf, append_pcm_len);

	if (pcm_payload_len < aac_in_buf_size) {
		printf("AAC Skip once cause input buf not full\n");
		aac_in_payload_len = aac_in_payload_len + append_pcm_len;

		return 0;
	}

	aac_out_payload_len = __aac_encode_a_frame(aac_enc_hd);

	if (buf_over_len) {
		memcpy(aac_in_buf, pcm_buf + append_pcm_len, buf_over_len);
		aac_in_payload_len = buf_over_len;
	} else
		aac_in_payload_len = 0;

	return aac_out_payload_len;
}

/*
 * @[aac_decoder_conf]&[aac_decoder_conf_len] are output value and they are used
 * only by mp4muxer [MP4SetTrackESConfiguration], aac_decoder_conf should be
 * an [array] type [uint8_t] size [64]
 */
int create_aac_encoder(HANDLE_AACENCODER *aac_enc_hd, int channels, int samplerate, int bitrate,
		uint8_t *aac_decoder_conf, int *aac_decoder_conf_len)
{
	int ret = 0;
	int i;
	int enc_modules = 0x01;//AAC_LC low complexity
	AACENC_InfoStruct aac_enc_info = {0};


	ret = aacEncOpen(aac_enc_hd, enc_modules, channels);
	if (ret != AACENC_OK) {
		printf( "Open AAC encoder fail\n");
		goto exit;
	}

	ret = aacEncoder_SetParam(*aac_enc_hd, AACENC_AOT, AOT_AAC_LC);
	if (ret != AACENC_OK) {
		printf( "Set AAC AOT fail\n");
		goto exit;
	}

	ret = aacEncoder_SetParam(*aac_enc_hd, AACENC_SAMPLERATE, samplerate);
	if (ret != AACENC_OK) {
		printf( "Set AAC SAMPLERATE fail\n");
		goto exit;
	}

	ret = aacEncoder_SetParam(*aac_enc_hd, AACENC_CHANNELMODE, MODE_1);
	if (ret != AACENC_OK) {
		printf( "Set AAC CHANNELMODE fail\n");
		goto exit;
	}

	/* channel order: MPEG order by default */
/*
	ret = aacEncoder_SetParam(*aac_enc_hd, AACENC_CHANNELORDER, 0);
	if (ret != AACENC_OK) {
		printf( "Set AAC CHANNELMODE fail\n");
		goto exit;
	}
*/

	/* bitrate mode: CBR by default */
/*
	ret = aacEncoder_SetParam(*aac_enc_hd, AACENC_BITRATEMODE, 0)
	if (ret != AACENC_OK) {
		printf( "Set AAC BITRATEMODE fail\n");
		goto exit;
	}
*/

	ret = aacEncoder_SetParam(*aac_enc_hd, AACENC_BITRATE, bitrate);
	if (ret != AACENC_OK) {
		printf( "Set AAC BITRATE fail\n");
		goto exit;
	}

	ret = aacEncoder_SetParam(*aac_enc_hd, AACENC_TRANSMUX, TT_MP4_ADTS);
	if (ret != AACENC_OK) {
		printf( "Set AAC TRANSPORT TYPE fail\n");
		goto exit;
	}

	/* afterburner is disbaled by default */
/*
	ret = aacEncoder_SetParam(*aac_enc_hd, AACENC_AFTERBURNER, 0);
	if (ret != AACENC_OK) {
		printf( "Set AAC TRANSPORT TYPE fail\n");
		goto exit;
	}
*/

	ret = aacEncEncode(*aac_enc_hd, NULL, NULL, NULL, NULL);
	if (ret != AACENC_OK) {
		printf( "Initialize AAC encoder fail\n");
		goto exit;
	}

	ret = aacEncInfo(*aac_enc_hd, &aac_enc_info);
	if (ret != AACENC_OK) {
		printf( "Get AAC encoder fail\n");
		goto exit;
	} else {
		printf("AAC max_out_buffer_len[%d], input_pcm_frame_len[%d]\n",
				aac_enc_info.maxOutBufBytes,
				aac_enc_info.frameLength * channels * 2);
		aac_out_buf_size = aac_enc_info.maxOutBufBytes;
		aac_out_buf = calloc(1, aac_out_buf_size);
		if (aac_out_buf == NULL) {
			printf("Malloc aac_out_buf fail\n");
			goto exit;
		}

		aac_in_buf_size = aac_enc_info.frameLength * channels * 2;
		aac_in_buf = calloc(1, aac_in_buf_size);
		if (aac_in_buf == NULL) {
			printf("Malloc aac_in_buf fail\n");
			goto exit;
		}

		for (i = 0; i < aac_enc_info.confSize; i++) {
			aac_decoder_conf[i] = aac_enc_info.confBuf[i];
			printf(".....AAC encoder conf [0x%x]\n",
					aac_enc_info.confBuf[i]);
		}
		*aac_decoder_conf_len = aac_enc_info.confSize;
	}

	printf("Init AAC encoder success...\n");

	return 0;

exit:
	printf("Init AAC encoder fail\n");
	return -1;
}

int destroy_aac_encoder(HANDLE_AACENCODER *aac_enc_hd)
{
	int ret = 0;

	ret = aacEncClose(aac_enc_hd);
	if (ret != AACENC_OK) {
		printf("Destroy AAC encoder fail...\n");
		ret = -1;
	} else
		printf("Destroy AAC encoder success...\n");

	if (aac_out_buf != NULL) {
		free(aac_out_buf);
		aac_out_buf = NULL;
		aac_out_buf_size = 0;
	}
	if (aac_in_buf != NULL) {
		free(aac_in_buf);
		aac_in_buf = NULL;
		aac_in_buf_size = 0;
	}

	return ret;
}

