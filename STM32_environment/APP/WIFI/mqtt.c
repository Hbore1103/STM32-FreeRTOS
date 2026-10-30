#include "mqtt.h"
#include "string.h"

/*
 * 第二层：MQTT 3.1.1 报文组包实现（通用，与平台无关）
 */

//编码MQTT可变长度字段(Remaining Length)，返回占用1~4个字节
static u8 mqtt_encode_len(u8 *buf,u32 len)
{
	u8 n=0;
	u8 b;

	do
	{
		b=len%128;
		len/=128;
		if(len>0)
			b|=0x80;
		buf[n++]=b;
	}while(len>0);

	return n;
}

//追加一个MQTT字符串字段：2字节长度(大端)+字符串内容，返回新的写入位置
static u8* mqtt_put_string(u8 *p,u8 *s)
{
	u16 l=(u16)strlen((char*)s);

	*p++=(u8)(l>>8);
	*p++=(u8)l;
	while(*s)
	{
		*p++=*s++;
	}
	return p;
}

/*******************************************************************************
* 函 数 名 : MQTT_BuildConnect
* 函数功能 : 拼装CONNECT报文（CleanSession，带用户名密码）
* 返 回 值 : 报文总长度
*******************************************************************************/
u16 MQTT_BuildConnect(u8 *buf,u8 *client_id,u8 *username,u8 *password,u16 keepalive)
{
	u8 *p;
	u8 lbuf[4];
	u8 ln,i;
	u32 rem;

	//计算剩余长度：可变头10字节 + ClientID + Username + Password
	rem=10;
	rem+=2+strlen((char*)client_id);
	rem+=2+strlen((char*)username);
	rem+=2+strlen((char*)password);

	p=buf;
	*p++=0x10;							//固定头：CONNECT
	ln=mqtt_encode_len(lbuf,rem);
	for(i=0;i<ln;i++)					//剩余长度
	{
		*p++=lbuf[i];
	}
	*p++=0x00;*p++=0x04;				//协议名 "MQTT"
	*p++='M';*p++='Q';*p++='T';*p++='T';
	*p++=0x04;							//协议级别：MQTT 3.1.1
	*p++=0xC2;							//连接标志：User+Password+CleanSession
	*p++=(u8)(keepalive>>8);			//心跳时间(大端)
	*p++=(u8)keepalive;

	p=mqtt_put_string(p,client_id);
	p=mqtt_put_string(p,username);
	p=mqtt_put_string(p,password);

	return (u16)(p-buf);
}

/*******************************************************************************
* 函 数 名 : MQTT_BuildPublish
* 函数功能 : 拼装PUBLISH报文（QoS0，用于属性上报）
*******************************************************************************/
u16 MQTT_BuildPublish(u8 *buf,u8 *topic,u8 *payload,u16 payload_len)
{
	u8 *p;
	u8 lbuf[4];
	u8 ln,i;
	u16 tl;
	u32 rem;

	tl=(u16)strlen((char*)topic);
	rem=2+tl+payload_len;				//主题长度+主题+负载

	p=buf;
	*p++=0x30;							//固定头：PUBLISH QoS0
	ln=mqtt_encode_len(lbuf,rem);
	for(i=0;i<ln;i++)
	{
		*p++=lbuf[i];
	}
	*p++=(u8)(tl>>8);					//主题
	*p++=(u8)tl;
	memcpy(p,topic,tl);
	p+=tl;
	memcpy(p,payload,payload_len);		//负载(JSON)
	p+=payload_len;

	return (u16)(p-buf);
}

/*******************************************************************************
* 函 数 名 : MQTT_BuildSubscribe
* 函数功能 : 拼装SUBSCRIBE报文（订阅一个主题，报文标识符固定1）
* 输    入 : topic_filter 主题；qos 请求的下发QoS等级（0或1，一般0）
*******************************************************************************/
u16 MQTT_BuildSubscribe(u8 *buf,u8 *topic_filter,u8 qos)
{
	u8 *p;
	u8 lbuf[4];
	u8 ln,i;
	u16 tl;
	u32 rem;

	tl=(u16)strlen((char*)topic_filter);
	rem=2+(u32)(2+tl)+1;				//报文标识符2 + 主题(2+长度) + QoS字节1

	p=buf;
	*p++=0x82;							//固定头：SUBSCRIBE(QoS1固定)
	ln=mqtt_encode_len(lbuf,rem);
	for(i=0;i<ln;i++)
	{
		*p++=lbuf[i];
	}
	*p++=0x00;*p++=0x01;				//报文标识符
	*p++=(u8)(tl>>8);					//主题过滤器
	*p++=(u8)tl;
	memcpy(p,topic_filter,tl);
	p+=tl;
	*p++=qos&0x03;						//希望平台按此QoS下发

	return (u16)(p-buf);
}

//PINGREQ心跳请求，固定2字节
u16 MQTT_BuildPingReq(u8 *buf)
{
	buf[0]=0xC0;
	buf[1]=0x00;
	return 2;
}

//DISCONNECT断开连接，固定2字节
u16 MQTT_BuildDisconnect(u8 *buf)
{
	buf[0]=0xE0;
	buf[1]=0x00;
	return 2;
}

//取报文类型（首字节高4位）
u8 MQTT_GetPacketType(u8 *buf)
{
	return buf[0]>>4;
}

//判断是否为CONNACK且返回码为0（接受连接）
u8 MQTT_IsConnAckOK(u8 *buf)
{
	if(buf[0]==0x20 && buf[1]==0x02 && buf[2]==0x00 && buf[3]==0x00)
		return 1;
	return 0;
}
