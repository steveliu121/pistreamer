/*
 * @file myrtmp.c
 * @author Akagi201
 * @date 2015/02/04
 *
 * @forked	by Steve Liu <steveliu121@163.com>
 * @date 2019/01/15
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "myrtmp.h"

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

void rtmp_logsetlevel(RTMP_LogLevel level)
{
	RTMP_LogSetLevel(level);
}

RTMP *rtmp_alloc(void)
{
	return RTMP_Alloc();
}

void rtmp_init(RTMP *rtmp)
{
	RTMP_Init(rtmp);
}

void rtmp_close(RTMP *rtmp)
{
	RTMP_Close(rtmp);
}

void rtmp_free(RTMP *rtmp)
{
	RTMP_Free(rtmp);
}

int rtmppacket_alloc(RTMPPacket *packet, uint32_t size)
{
	return RTMPPacket_Alloc(packet, size);
}

void rtmppacket_reset(RTMPPacket *packet)
{
	RTMPPacket_Reset(packet);
}

void rtmppacket_free(RTMPPacket *packet)
{
	RTMPPacket_Free(packet);
}

int rtmp_setupurl(RTMP *rtmp, char *url)
{
	return RTMP_SetupURL(rtmp, url);
}

int rtmp_connect(RTMP *rtmp, RTMPPacket *packet)
{
	return RTMP_Connect(rtmp, packet);
}

int rtmp_connectstream(RTMP *rtmp, int seekTime)
{
	return RTMP_ConnectStream(rtmp, seekTime);
}

int rtmp_isconnected(RTMP *rtmp)
{
	return RTMP_IsConnected(rtmp);
}

void rtmp_enablewrite(RTMP *rtmp)
{
	RTMP_EnableWrite(rtmp);
}

int rtmp_sendpacket(RTMP *rtmp, RTMPPacket *packet, int queue)
{
	return RTMP_SendPacket(rtmp, packet, queue);
}


/*
 * @brief write video(H264/AVC) tag data
 *
 */
void rtmp_write_avc_data_tag(const uint8_t *body,
					const uint8_t *data,
					uint32_t data_len,
					int keyframe)
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

	memcpy(body, buf, (pbuf - buf));

	free(buf);

	return;
}

/*
 * @brief write audio(AAC) tag data
 *
 */
void rtmp_write_aac_data_tag(const uint8_t *body,
					const uint8_t *data, uint32_t data_len)
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

	memcpy(body, buf, (pbuf - buf));

	free(buf);

	return;
}

//void rtmp_write_video_data_tag(const uint8_t *body,
//					const uint8_t *data, uint32_t data_len,
//					uint32_t timestamp)
//{
//	  if (g_time_begin == 0)
//		g_time_begin = timestamp;
//
//	g_time_now = timestamp;
//
//	  rtmp_write_video_tag(file_hd, (uint8_t *) data, data_len, (timestamp - g_time_begin));
//}

/*
 * @brief write AVC sequence header in header of video tag data part, the first video tag
 * AVCDecoderConfigurationRecord
 */
void rtmp_write_avc_sequence_header_tag(const uint8_t *body,
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

	memcpy(body, buf, (pbuf - buf));

	free(buf);

	return;
}

/*
 * @brief write AAC pcm profile in header of audio tag data part, the first audio tag
 * AudioSpecificConfig
 */
void rtmp_write_aac_sequence_header_tag(const uint8_t *body,
						int sample_rate, int channels)
{
	uint8_t *buf = (uint8_t *) malloc(4);
	uint8_t *pbuf = buf;

	/* SoundFormat|SoundRate|SoundSize|SoundType:0xa0|0x0c|0x02|0x01*/
	pbuf = ui08_to_bytes(pbuf, 0xaf);
	pbuf = ui08_to_bytes(pbuf, 0); // AACPacketType: 0x00 - AAC sequence header

	/* TODO FIXME */
	pbuf = ui16_to_bytes(pbuf, 0x1590); // AudioSpecificConfig: 0x1590(AAC-LC;8000samplerate;stereo_out) //TODO FIXME

	memcpy(body, buf, (pbuf - buf));

	free(buf);

	return;
}
