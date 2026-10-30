#ifndef _onenet_H
#define _onenet_H

#include "system.h"

/*
 * 第三层：OneNET平台对接层
 * 负责：WiFi参数、平台三元组、连接状态机、保活、数据上报
 * 依赖：下层 esp8266(AT驱动) + mqtt(报文组包)
 */

/*==================== 用户配置区：拿到平台信息后只改这一段 ====================*/

//1. 路由器WiFi名称和密码（2.4G频段，ESP8266不支持5G）
#define WIFI_SSID				"\xE7\x91\x9C\xE7\x91\x9C\xE9\xB1\xBC"	//WiFi名"瑜瑜鱼"的UTF-8字节（中文SSID用hex转义，避免Keil按GBK编译导致连不上）
#define WIFI_PWD				"2750545870qwer"

//2. OneNET MQTT接入地址（新版物模型MQTT，端口1883，不用改）
#define ONENET_HOST				"mqtts.heclouds.com"
#define ONENET_PORT				1883

//3. OneNET平台"设备三元组"：在控制台创建设备后获得
#define ONENET_PRODUCT_ID		"za37194h86"	//产品ID(ProductId)
#define ONENET_DEVICE_NAME		"puzhong"		//设备名称(DeviceName)
#define ONENET_DEVICE_KEY		"OGhBVjBZUER4OFlNanluQjA0ZVRwclpKV2dKOTlGb3o="	//设备密钥(DeviceKey,Base64)

//4. 鉴权Token无需手填：OneNET_Init()时由 onenet_token.c 用设备密钥自动计算
//   （HMAC-SHA1 + Base64 + URL编码，过期时间写死2030年）

#define ONENET_KEEPALIVE		120		//MQTT心跳周期，单位秒，平台要求60~300

/*============================== 配置区结束 ==================================*/

//OneNET物模型Topic（C字符串自动拼接，无需修改）
#define ONENET_TOPIC_POST		"$sys/" ONENET_PRODUCT_ID "/" ONENET_DEVICE_NAME "/thing/property/post"		//属性上报
#define ONENET_TOPIC_POST_REPLY	"$sys/" ONENET_PRODUCT_ID "/" ONENET_DEVICE_NAME "/thing/property/post/reply"	//上报应答
#define ONENET_TOPIC_SET		"$sys/" ONENET_PRODUCT_ID "/" ONENET_DEVICE_NAME "/thing/property/set"			//属性设置下发
#define ONENET_TOPIC_SET_REPLY	"$sys/" ONENET_PRODUCT_ID "/" ONENET_DEVICE_NAME "/thing/property/set_reply"	//属性设置应答

//OneNET连接状态
typedef enum
{
	OS_INIT=0,		//上电，测试AT
	OS_AT_READY,	//AT正常，配置模块模式
	OS_JOIN_AP,		//连接路由器
	OS_TCP_CONN,	//建立TCP连接
	OS_MQTT_CONN,	//发送MQTT CONNECT
	OS_READY,		//已上线，正常通信
	OS_RECONNECT	//掉线重连
} OnenetState;

void OneNET_Init(void);				//初始化USART3和状态机
void OneNET_Task(void);				//主循环调用，约每10ms一次，非阻塞
u8   OneNET_IsOnline(void);			//返回1表示MQTT已上线
u8   OneNET_LastError(void);			//最近连接错误码：0无，1-5拒绝码，0xFE发送失败，0xFF无应答
u8   OneNET_GetProgress(void);		//返回连接进度 1开始 2连WiFi中 3连平台中 4成功
u8   OneNET_PostData(u8 temp,u8 humi,u8 fan,u8 servo,u8 beep);	//上报温湿度和风扇/舵机/蜂鸣器开关(0/1)

//云端"属性设置"下发命令（属性值1=开 0=关；舵机1=180° 0=0°）
typedef struct
{
	u8 fan;
	u8 servo;
	u8 beep;
	u8 has_fan;		//本次下发是否包含Fan字段
	u8 has_servo;	//本次下发是否包含Servo字段
	u8 has_beep;	//本次下发是否包含Beep字段
} OnenetCtrl;

u8   OneNET_GetCtrl(OnenetCtrl *c);	//主循环轮询：1=取到一条新命令（取走后自动清除，等待下一条）

#endif
