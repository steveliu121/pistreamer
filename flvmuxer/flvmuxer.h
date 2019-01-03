/*
 * @file flvmuxer.h
 * @author Akagi201
 * @date 2015/02/04
 *
 * @forked  by Steve Liu <steveliu121@163.com>
 * @date 2018/12/28
 */

#ifndef FLV_MUXER_H_
#define FLV_MUXER_H_

#include <stdbool.h>
#include <stdint.h>

struct FLVTag {
	uint8_t type;
	uint8_t data_size[3];
	uint8_t timestamp[3];
	uint8_t timestamp_ex;
	uint8_t streamid[3];
} __attribute__((__packed__));

struct FLVProfile {
	char name[64];
	bool has_video;
	bool has_audio;
	int sample_rate;
	int channels;
	uint8_t *sps;
	uint8_t *pps;
	int sps_len;
	int pps_len;
};

enum {
    AMF_DATA_TYPE_NUMBER = 0x00,
    AMF_DATA_TYPE_BOOL = 0x01,
    AMF_DATA_TYPE_STRING = 0x02,
    AMF_DATA_TYPE_OBJECT = 0x03,
    AMF_DATA_TYPE_NULL = 0x05,
    AMF_DATA_TYPE_UNDEFINED = 0x06,
    AMF_DATA_TYPE_REFERENCE = 0x07,
    AMF_DATA_TYPE_ECMAARRAY = 0x08,
    AMF_DATA_TYPE_OBJECT_END = 0x09,
    AMF_DATA_TYPE_ARRAY = 0x0a,
    AMF_DATA_TYPE_DATE = 0x0b,
    AMF_DATA_TYPE_LONG_STRING = 0x0c,
    AMF_DATA_TYPE_UNSUPPORTED = 0x0d,
};

enum {
    FLV_TAG_TYPE_AUDIO = 0x08,
    FLV_TAG_TYPE_VIDEO = 0x09,
    FLV_TAG_TYPE_META = 0x12,
};

/* @[create_flv_muxer] is the wrapper of [flv_file_open][flv_write_file_header]
 * [flv_write_aac_sequence_header_tag][flv_write_avc_sequence_header_tag]
 * @[destroy_flv_muxer] is the wrapper of [flv_file_close]
 */
int create_flv_muxer(FILE **flv_hd, struct FLVProfile *flv_profile);
void destroy_flv_muxer(FILE *flv_hd);

FILE *flv_file_open(const char *filename);

void flv_file_close(FILE *file_hd);

void flv_write_file_header(FILE *file_hd,
				bool is_have_audio, bool is_have_video);

void flv_write_aac_sequence_header_tag(FILE *file_hd,
						int sample_rate, int channel);

void flv_write_avc_sequence_header_tag(FILE *file_hd,
					const uint8_t *sps, uint32_t sps_len,
					const uint8_t *pps, uint32_t pps_len);

void flv_write_aac_data_tag(FILE *file_hd,
					const uint8_t *data, uint32_t data_len,
					uint32_t timestamp);

void flv_write_avc_data_tag(FILE *file_hd,
					const uint8_t *data, uint32_t data_len,
					uint32_t timestamp, int keyframe);

#endif // FLV_MUXER_H_
