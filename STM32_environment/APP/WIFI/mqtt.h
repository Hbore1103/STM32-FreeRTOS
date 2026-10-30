#ifndef _mqtt_H
#define _mqtt_H

#include "system.h"

/*
 * 第二层：MQTT协议层（与硬件无关）
 * 只负责MQTT 3.1.1报文的拼装与解析，不关心串口和WiFi
 * 拼好的报文交给 esp8266 的 ESP8266_TcpSend 发出去
 */

//MQTT报文类型（固定首字节高4位）
#define MQTT_CONNECT			1
#define MQTT_CONNACK			2
#define MQTT_PUBLISH			3
#define MQTT_PUBACK				4
#define MQTT_SUBSCRIBE			8
#define MQTT_SUBACK				9
#define MQTT_PINGREQ			12
#define MQTT_PINGRESP			13
#define MQTT_DISCONNECT			14

u16 MQTT_BuildConnect(u8 *buf,u8 *client_id,u8 *username,u8 *password,u16 keepalive);
u16 MQTT_BuildPublish(u8 *buf,u8 *topic,u8 *payload,u16 payload_len);
u16 MQTT_BuildSubscribe(u8 *buf,u8 *topic_filter,u8 qos);	//订阅主题，qos一般填0
u16 MQTT_BuildPingReq(u8 *buf);
u16 MQTT_BuildDisconnect(u8 *buf);
u8  MQTT_GetPacketType(u8 *buf);	//取报文类型
u8  MQTT_IsConnAckOK(u8 *buf);		//是否为连接成功的CONNACK(返回码0)

#endif
