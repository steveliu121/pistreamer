/*
 * @file myrtmp.h
 * @author Akagi201
 * @date 2015/02/04
 *
 * @forked  by Steve Liu <steveliu121@163.com>
 * @date 2019/01/15
 */

#ifndef MYRTMP_H_
#define MYRTMP_H_

#include <stdbool.h>
#include <stdint.h>
#include <librtmp/rtmp.h>
#include <librtmp/log.h>


void rtmp_logsetlevel(RTMP_LogLevel level);
RTMP *rtmp_alloc(void);
void rtmp_init(RTMP *rtmp);
void rtmp_close(RTMP *rtmp);
void rtmp_free(RTMP *rtmp);
int rtmppacket_alloc(RTMPPacket *packet, uint32_t size);
void rtmppacket_reset(RTMPPacket *packet);
void rtmppacket_free(RTMPPacket *packet);
int rtmp_setupurl(RTMP *rtmp, char *url);
int rtmp_connect(RTMP *rtmp, RTMPPacket *packet);
int rtmp_connectstream(RTMP *rtmp, int seekTime);
int rtmp_isconnected(RTMP *rtmp);
void rtmp_enablewrite(RTMP *rtmp);
int rtmp_sendpacket(RTMP *rtmp, RTMPPacket *packet, int queue);
void rtmp_write_avc_data_tag(const uint8_t *body,
					const uint8_t *data,
					uint32_t data_len,
					int keyframe);
void rtmp_write_aac_data_tag(const uint8_t *body,
					const uint8_t *data, uint32_t data_len);
void rtmp_write_avc_sequence_header_tag(const uint8_t *body,
					const uint8_t *sps, uint32_t sps_len,
					const uint8_t *pps, uint32_t pps_len);
void rtmp_write_aac_sequence_header_tag(const uint8_t *body,
						int sample_rate, int channels);

#endif // MYRTMP_H_
