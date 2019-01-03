/*
 * @file flv-muxer.c
 * @author Akagi201
 * @date 2015/02/04
 *
 * @forked	by Steve Liu <steveliu121@163.com>
 * @date 2018/12/28
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "flvmuxer.h"

static uint32_t g_time_begin;
static uint32_t g_time_now;

static uint8_t *ui08_to_bytes(uint8_t *buf, uint8_t val) {
	buf[0] = (val) & 0xff;
	return buf + 1;
}

static uint8_t *ui16_to_bytes(uint8_t *buf, uint16_t val) {
	buf[0] = (val >> 8) & 0xff;
	buf[1] = (val) & 0xff;

	return buf + 2;
}

static uint8_t *ui24_to_bytes(uint8_t *buf, uint32_t val) {
	buf[0] = (val >> 16) & 0xff;
	buf[1] = (val >> 8) & 0xff;
	buf[2] = (val) & 0xff;

	return buf + 3;
}

static uint8_t *ui32_to_bytes(uint8_t *buf, uint32_t val) {
	buf[0] = (val >> 24) & 0xff;
	buf[1] = (val >> 16) & 0xff;
	buf[2] = (val >> 8) & 0xff;
	buf[3] = (val) & 0xff;

	return buf + 4;
}

static uint8_t *ui64_to_bytes(uint8_t *buf, uint64_t val) {
	buf[0] = (val >> 56) & 0xff;
	buf[1] = (val >> 48) & 0xff;
	buf[2] = (val >> 40) & 0xff;
	buf[3] = (val >> 32) & 0xff;
	buf[4] = (val >> 24) & 0xff;
	buf[5] = (val >> 16) & 0xff;
	buf[6] = (val >> 8) & 0xff;
	buf[7] = (val) & 0xff;

	return buf + 8;
}

static uint8_t *double_to_bytes(uint8_t *buf, double val) {
	union {
	uint8_t dc[8];
	double dd;
	} d;

	uint8_t b[8];

	d.dd = val;

	b[0] = d.dc[7];
	b[1] = d.dc[6];
	b[2] = d.dc[5];
	b[3] = d.dc[4];
	b[4] = d.dc[3];
	b[5] = d.dc[2];
	b[6] = d.dc[1];
	b[7] = d.dc[0];

	memcpy(buf, b, 8);

	return buf + 8;
}

static uint8_t bytes_to_ui32(const uint8_t *buf) {
	return (((buf[0]) << 24) & 0xff000000)
		| (((buf[1]) << 16) & 0xff0000)
		| (((buf[2]) << 8) & 0xff00)
		| (((buf[3])) & 0xff);
}

static uint8_t *amf_string_to_bytes(uint8_t *buf, const char *str) {
	uint8_t *pbuf = buf;
	size_t len = strlen(str);

	pbuf = ui08_to_bytes(pbuf, AMF_DATA_TYPE_STRING);
	pbuf = ui16_to_bytes(pbuf, len);
	memcpy(pbuf, str, len);
	pbuf += len;

	return pbuf;
}

static uint8_t *amf_double_to_bytes(uint8_t *buf, double d) {
	uint8_t *pbuf = buf;

	pbuf = ui08_to_bytes(pbuf, AMF_DATA_TYPE_NUMBER);
	pbuf = double_to_bytes(pbuf, d);

	return pbuf;
}

static uint8_t *amf_bool_to_bytes(uint8_t *buf, int b) {
	uint8_t *pbuf = buf;

	pbuf = ui08_to_bytes(pbuf, AMF_DATA_TYPE_BOOL);
	pbuf = ui08_to_bytes(pbuf, !!b);

	return pbuf;
}

static uint8_t *amf_ecmaarray_to_bytes(uint8_t *buf, uint32_t size)
{
	uint8_t *pbuf = buf;

	pbuf = ui08_to_bytes(pbuf, AMF_DATA_TYPE_ECMAARRAY);
	pbuf = ui32_to_bytes(pbuf, size);

	return pbuf;
}

static uint8_t *amf_ecmaarray_add_key(uint8_t *buf, const char *str)
{
	uint8_t *pbuf = buf;
	uint16_t len = strlen(str);

	pbuf = ui16_to_bytes(pbuf, len);
	memcpy(pbuf, str, len);
	pbuf += len;

	return pbuf;
}

/*TODO*/
static uint8_t *amf_ecmaarray_add_double(uint8_t *buf, double value)
{
	uint8_t *pbuf = buf;

	pbuf = amf_double_to_bytes(pbuf, value);

	return pbuf;
}

static uint8_t *amf_objend(uint8_t *buf)
{
	uint8_t *pbuf = buf;

	pbuf = ui08_to_bytes(pbuf, 0);
	pbuf = ui08_to_bytes(pbuf, 0);
	pbuf = ui08_to_bytes(pbuf, 9);

	return pbuf;
}

FILE *flv_file_open(const char *filename)
{
	FILE *file_hd = NULL;

	if (NULL == filename)
		goto exit;

	file_hd = fopen(filename, "wb");
	if (file_hd == NULL)
		goto exit;

	return file_hd;

exit:
	printf("FLV file open failed\n");
	return file_hd;
}

void flv_write_file_header(FILE *file_hd, bool is_have_audio, bool is_have_video)
{
	char flv_file_header[] = "FLV\x1\x5\0\0\0\x9\0\0\0\0"; // have audio and have video

	if (is_have_audio && is_have_video)
		flv_file_header[4] = 0x05;
	else if (is_have_audio && !is_have_video)
		flv_file_header[4] = 0x04;
	else if (!is_have_audio && is_have_video)
		flv_file_header[4] = 0x01;
	else
		flv_file_header[4] = 0x00;

	fwrite(flv_file_header, 13, 1, file_hd);

	return;
}

/*
 * @brief write flv tag
 * @param[in] buf:
 * @param[in] buf_len: flv tag body size
 * @param[in] timestamp: flv tag timestamp
 */
static void flv_write_flv_tag(FILE *file_hd,
					uint8_t *buf, uint32_t buf_len,
					uint32_t timestamp, int tag_type)
{
	uint8_t prev_size[4] = {0};

	struct FLVTag flvtag;

	memset(&flvtag, 0, sizeof(flvtag));

	flvtag.type = tag_type;
	ui24_to_bytes(flvtag.data_size, buf_len);
	flvtag.timestamp_ex = (uint8_t) ((timestamp >> 24) & 0xff);
	flvtag.timestamp[0] = (uint8_t) ((timestamp >> 16) & 0xff);
	flvtag.timestamp[1] = (uint8_t) ((timestamp >> 8) & 0xff);
	flvtag.timestamp[2] = (uint8_t) ((timestamp) & 0xff);

	fwrite(&flvtag, sizeof(flvtag), 1, file_hd);
	fwrite(buf, 1, buf_len, file_hd);

	ui32_to_bytes(prev_size, buf_len + (uint32_t) sizeof(flvtag));
	fwrite(prev_size, 4, 1, file_hd);

	return;
}

/*TODO*/
static void flv_write_onmetadata_tag(FILE *file_hd, double value)
{
	uint8_t *buf = NULL;
	uint8_t *pbuf = NULL;

	buf = (uint8_t *)malloc(1024);
	pbuf = buf;

	pbuf = amf_string_to_bytes(pbuf, "onMetadata");//SCRIPTDATA tag name
	pbuf = amf_ecmaarray_to_bytes(pbuf, 1);//ECMAARRAY size
	/* ECMAARRAY properties */
	pbuf = amf_ecmaarray_add_key(pbuf, "duration");
	pbuf = amf_ecmaarray_add_double(pbuf, value);

	pbuf= amf_objend(pbuf);

	flv_write_flv_tag(file_hd, buf, (uint32_t)(pbuf - buf),
				0, FLV_TAG_TYPE_META);

	free(buf);

	return;
}

/*
 * @brief write video tag
 * @param[in] buf:
 * @param[in] buf_len: flv tag body size
 * @param[in] timestamp: flv tag timestamp
 */
static void flv_write_video_tag(FILE *file_hd,
					uint8_t *buf, uint32_t buf_len,
					uint32_t timestamp)
{
	flv_write_flv_tag(file_hd, buf, buf_len, timestamp, FLV_TAG_TYPE_VIDEO);
}

/*
 * @brief write audio tag
 * @param[in] buf:
 * @param[in] buf_len: flv tag body size
 * @param[in] timestamp: flv tag timestamp
 */
static void flv_write_audio_tag(FILE *file_hd,
					uint8_t *buf, uint32_t buf_len,
					uint32_t timestamp)
{
	flv_write_flv_tag(file_hd, buf, buf_len, timestamp, FLV_TAG_TYPE_AUDIO);
}

/*
 * @brief write video(H264/AVC) tag data
 *
 */
void flv_write_avc_data_tag(FILE *file_hd,
					const uint8_t *data, uint32_t data_len,
					uint32_t timestamp, int keyframe)
{
	uint8_t *buf = (uint8_t *) malloc(data_len + 5);
	uint8_t *pbuf = buf;

	uint8_t flag = 0;
	// (FrameType << 4) | CodecID, 1 - keyframe, 2 - inner frame, 7 - AVC(h264)
	if (keyframe)
		flag = 0x17;
	else
		flag = 0x27;

	pbuf = ui08_to_bytes(pbuf, flag);

	pbuf = ui08_to_bytes(pbuf, 1);	  // AVCPacketType: 0x00 - AVC sequence header; 0x01 - AVC NALU
	pbuf = ui24_to_bytes(pbuf, 0);	  // composition time

	memcpy(pbuf, data, data_len);
	pbuf += data_len;

	if (g_time_begin == 0)
		g_time_begin = timestamp;

	g_time_now = timestamp;

	flv_write_video_tag(file_hd, buf, (uint32_t)(pbuf - buf), (timestamp - g_time_begin));

	free(buf);

	return;
}

/*
 * @brief write audio(AAC) tag data
 *
 */
void flv_write_aac_data_tag(FILE *file_hd,
					const uint8_t *data, uint32_t data_len,
					uint32_t timestamp)
{
	uint8_t *buf = NULL;
	uint8_t *pbuf = NULL;
	uint32_t payload_len = 0;
	uint8_t *payload = NULL;

	/* strip ADTS from frame */
	payload = (uint8_t *)(data + 7);
	payload_len = data_len - 7;

	buf = (uint8_t *)malloc(payload_len + 2);
	pbuf = buf;

	/* SoundFormat|SoundRate|SoundSize|SoundType:0xa0|0x0c|0x02|0x01*/
	pbuf = ui08_to_bytes(pbuf, 0xaf);
	pbuf = ui08_to_bytes(pbuf, 1); // AACPacketType: 0x01 - AAC frame data

	memcpy(pbuf, payload, payload_len);
	pbuf += payload_len;

	if (g_time_begin == 0)
		g_time_begin = timestamp;

	g_time_now = timestamp;

	flv_write_audio_tag(file_hd, buf, (uint32_t)(pbuf - buf), (timestamp - g_time_begin));

	free(buf);

	return;
}

//void flv_write_video_data_tag(FILE *file_hd,
//					const uint8_t *data, uint32_t data_len,
//					uint32_t timestamp)
//{
//	  if (g_time_begin == 0)
//		g_time_begin = timestamp;
//
//	g_time_now = timestamp;
//
//	  flv_write_video_tag(file_hd, (uint8_t *) data, data_len, (timestamp - g_time_begin));
//}

/*
 * @brief write AVC sequence header in header of video tag data part, the first video tag
 * AVCDecoderConfigurationRecord
 */
void flv_write_avc_sequence_header_tag(FILE *file_hd,
					const uint8_t *sps, uint32_t sps_len,
					const uint8_t *pps, uint32_t pps_len)
{
	uint8_t *buf = (uint8_t *) malloc(sps_len + pps_len + 16);
	uint8_t *pbuf = buf;

	uint8_t flag = 0;

	flag = (1 << 4) // frametype "1 == keyframe"
		| 7; // codecid "7 == AVC"

	pbuf = ui08_to_bytes(pbuf, flag);

	pbuf = ui08_to_bytes(pbuf, 0); // AVCPacketType: 0x00 - AVC sequence header
	pbuf = ui24_to_bytes(pbuf, 0); // composition time

	// generate AVCC with sps and pps, AVCDecoderConfigurationRecord

	pbuf = ui08_to_bytes(pbuf, 1); // configurationVersion
	pbuf = ui08_to_bytes(pbuf, sps[1]); // AVCProfileIndication
	pbuf = ui08_to_bytes(pbuf, sps[2]); // profile_compatibility
	pbuf = ui08_to_bytes(pbuf, sps[3]); // AVCLevelIndication
	// 6 bits reserved (111111) + 2 bits nal size length - 1
	// (Reserved << 2) | Nal_Size_length = (0x3F << 2) | 0x03 = 0xFF
	pbuf = ui08_to_bytes(pbuf, 0xff);
	// 3 bits reserved (111) + 5 bits number of sps (00001)
	// (Reserved << 5) | Number_of_SPS = (0x07 << 5) | 0x01 = 0xe1
	pbuf = ui08_to_bytes(pbuf, 0xe1);

	// sps
	pbuf = ui16_to_bytes(pbuf, (uint16_t)sps_len);
	memcpy(pbuf, sps, sps_len);
	pbuf += sps_len;

	// pps
	pbuf = ui08_to_bytes(pbuf, 1); // number of pps
	pbuf = ui16_to_bytes(pbuf, (uint16_t)pps_len);
	memcpy(pbuf, pps, pps_len);
	pbuf += pps_len;

	flv_write_video_tag(file_hd, buf, (uint32_t)(pbuf - buf), 0);

	free(buf);

	return;
}

/*
 * @brief write AAC pcm profile in header of audio tag data part, the first audio tag
 * AudioSpecificConfig
 */
void flv_write_aac_sequence_header_tag(FILE *file_hd,
						int sample_rate, int channels)
{
	uint8_t *buf = (uint8_t *) malloc(4);
	uint8_t *pbuf = buf;

	/* SoundFormat|SoundRate|SoundSize|SoundType:0xa0|0x0c|0x02|0x01*/
	pbuf = ui08_to_bytes(pbuf, 0xaf);
	pbuf = ui08_to_bytes(pbuf, 0); // AACPacketType: 0x00 - AAC sequence header

	pbuf = ui16_to_bytes(pbuf, 0x1590); // AudioSpecificConfig: 0x1590 //TODO confirm

	flv_write_audio_tag(file_hd, buf, (uint32_t)(pbuf - buf), 0);

	free(buf);

	return;
}

/*
 * it sames dosen't work */
/*
void flv_write_avc_stop_tag(FILE *file_hd)
{
	uint8_t *buf = (uint8_t *) malloc(6);
	uint8_t *pbuf = buf;

	uint8_t flag = 0;
	// (FrameType << 4) | CodecID, 1 - keyframe, 2 - inner frame, 5 info/command frame, 7 - AVC(h264)
	flag = 0x57;

	pbuf = ui08_to_bytes(pbuf, flag);

	pbuf = ui08_to_bytes(pbuf, 2);	  // AVCPacketType: 0x00 - AVC sequence header; 0x01 - AVC NALU; 0x02 - AVC end of sequence

	pbuf = ui24_to_bytes(pbuf, 0);	  // composition time

	pbuf = ui08_to_bytes(pbuf, 1);	  // VideoTagBody: 0x00 - Start of client-side seeking video frame sequence; 0x01 - End of ...

	flv_write_video_tag(file_hd, buf, (uint32_t)(pbuf - buf), 0);

	free(buf);

	return;
}*/

int create_flv_muxer(FILE **flv_hd, struct FLVProfile *flv_profile)
{
	FILE *file_hd = NULL;

	file_hd = flv_file_open(flv_profile->name);
	if (file_hd == NULL)
		return -1;

	flv_write_file_header(file_hd,
				flv_profile->has_audio, flv_profile->has_video);

	flv_write_avc_sequence_header_tag(file_hd,
				flv_profile->sps, flv_profile->sps_len,
				flv_profile->pps, flv_profile->pps_len);

	flv_write_aac_sequence_header_tag(file_hd,
				flv_profile->sample_rate, flv_profile->channels);

	*flv_hd = file_hd;

	printf("Create FLV muxer\n");

	return 0;
}

void flv_file_close(FILE *file_hd)
{
	double duration = 0;

	if (file_hd == NULL)
		return;

	duration = (double)(g_time_now - g_time_begin) / 1000;
	flv_write_onmetadata_tag(file_hd, duration);

	fclose(file_hd);
	file_hd = NULL;

	return;
}

void destroy_flv_muxer(FILE *flv_hd)
{
	if (flv_hd != NULL) {
//		flv_write_avc_stop_tag(flv_hd);
		flv_file_close(flv_hd);
		printf("Destroy FLV muxer\n");
	}
}
