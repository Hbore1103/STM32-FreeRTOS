#include "onenet.h"
#include "onenet_token.h"
#include "esp8266.h"
#include "mqtt.h"
#include "usart.h"
#include "stdio.h"
#include "string.h"

/*
 * 第三层：OneNET连接状态机（框架）
 *
 * 调用方式：main函数里 OneNET_Init() 一次，主循环里反复调 OneNET_Task()
 * 状态流转：
 *   OS_INIT -> OS_AT_READY -> OS_JOIN_AP -> OS_TCP_CONN -> OS_MQTT_CONN -> OS_READY
 *   任一步骤失败停在原状态重试，上线后在OS_READY做保活和下发解析
 *
 * 说明：联网阶段每个AT步骤内部有超时等待（最多数秒），只发生在上电热机阶段；
 *       上线后OS_READY不再阻塞，不影响按键/OLED/DHT11的正常运行。
 */

static u8   onenet_state=OS_INIT;	//当前状态
static u16  step_tick=0;			//状态内计时（每拍约10ms）
static u16  keepalive_cnt=0;		//心跳计时（每拍约10ms）
static u8   online=0;				//上线标志
static u8   last_err=0;				//最近一次错误码：0无 1-5服务器CONNACK拒绝码 0xFF无应答/发送失败
static u16  mqtt_cd=0;				//MQTT连接重试冷却计数（约10ms/拍，300=3秒）
static u8   mqtt_txbuf[512];		//MQTT报文拼装缓冲
static u8   mqtt_token[200];		//自动生成的鉴权token
static u8   sub_done=0;			//是否已订阅property/set主题（重连后需重新订阅）
static volatile u8 ctrl_ready=0;	//收到一条云端下发命令待main取走
static OnenetCtrl ctrl_pending;	//待取走的命令内容

void OneNET_Init(void)
{
	ESP8266_UART_Init(ESP8266_BAUD);
	OneNET_BuildToken(mqtt_token,sizeof(mqtt_token));	//开机算一次token即可(2030年才到期)
	printf("[OneNET] token=%s\r\n",mqtt_token);	//核对用，应与参考token完全一致
	onenet_state=OS_INIT;
	step_tick=0;
	online=0;
	sub_done=0;
	ctrl_ready=0;
	ctrl_pending.has_fan=0;
	ctrl_pending.has_servo=0;
	ctrl_pending.has_beep=0;
}

u8 OneNET_IsOnline(void)
{
	return online;
}

//取最近一次MQTT连接错误码（供main显示/调试）：0无，1-5拒绝码，0xFF无应答
u8 OneNET_LastError(void)
{
	return last_err;
}

//在接收缓冲中安全查找ASCII子串（不修改缓冲区，避免和接收中断竞争）
static u8 onenet_contains(const char *s)
{
	u16 slen=(u16)strlen(s);
	u16 i,j;

	if(esp_rx_cnt<slen)
		return 0;
	for(i=0;i<=esp_rx_cnt-slen;i++)
	{
		for(j=0;j<slen;j++)
		{
			if(esp_rx_buf[i+j]!=(u8)s[j])
				break;
		}
		if(j==slen)
			return 1;
	}
	return 0;
}

//在接收缓冲中查找CONNACK帧(20 02 XX rc)
//返回0=未找到；1=找到且rc=0(成功)；2~6=找到且拒绝码为1~5，*rc输出返回码
static u8 onenet_find_connack(u8 *rc)
{
	u8 pat[3]={0x20,0x02,0x00};
	u16 i,j;

	if(esp_rx_cnt<4)
		return 0;
	for(i=0;i<=esp_rx_cnt-4;i++)
	{
		for(j=0;j<3;j++)
		{
			if(esp_rx_buf[i+j]!=pat[j])
				break;
		}
		if(j==3)
		{
			*rc=esp_rx_buf[i+3];
			return (*rc==0)?1:(u8)(*rc+1);
		}
	}
	return 0;
}

/*======================= 云端下行（属性设置 property/set） =======================*/

//在接收缓冲中定位字节串，返回下标，找不到返回0xFFFF
static u16 onenet_rx_pos(const u8 *needle,u16 nlen)
{
	u16 i,j;

	if(esp_rx_cnt<nlen)
		return 0xFFFF;
	for(i=0;i<=esp_rx_cnt-nlen;i++)
	{
		for(j=0;j<nlen;j++)
		{
			if(esp_rx_buf[i+j]!=needle[j])
				break;
		}
		if(j==nlen)
			return i;
	}
	return 0xFFFF;
}

//跳过空格/制表符/冒号/引号，指向真正的值字符
static u16 onenet_skip_punct(const u8 *p,u16 q,u16 plen)
{
	while(q<plen&&(p[q]==' '||p[q]=='\t'||p[q]==':'||p[q]=='"'))
		q++;
	return q;
}

//从q处解析一个十进制整数（允许负号），写*out，返回值后位置；失败返回0xFFFF
static u16 onenet_parse_int_at(const u8 *p,u16 q,u16 plen,int *out)
{
	int v=0;
	u8 neg=0;

	if(q>=plen)
		return 0xFFFF;
	if(p[q]=='-'){neg=1;q++;}
	if(q>=plen||p[q]<'0'||p[q]>'9')
		return 0xFFFF;
	while(q<plen&&p[q]>='0'&&p[q]<='9')
	{
		v=v*10+(p[q]-'0');
		q++;
	}
	*out=neg?-v:v;
	return q;
}

//在JSON负载[p,p+plen)内查找"key"，兼容两种平台格式：
//  格式A（物模型包装）："Fan":{"value":1}
//  格式B（应用模拟器直传）："Fan":1
static u8 onenet_json_int(const u8 *p,u16 plen,const char *key,int *out)
{
	u8 pat[20];
	u16 klen;
	u16 i,q,limit;

	klen=(u16)strlen(key);
	if(klen+2>sizeof(pat))
		return 0;
	pat[0]='"';
	memcpy(pat+1,key,klen);
	pat[klen+1]='"';
	klen=(u16)(klen+2);						//pat = "key"

	for(i=0;i+klen<=plen;i++)
	{
		if(memcmp(p+i,pat,klen)==0)
		{
			q=onenet_skip_punct(p,i+klen,plen);	//跳过 : 和空白，指向值
			if(q>=plen)
				return 0;

			//格式B：值直接是数字
			if(p[q]=='-'||(p[q]>='0'&&p[q]<='9'))
			{
				return onenet_parse_int_at(p,q,plen,out)!=0xFFFF;
			}

			//格式A：值是{"value":N}，只在本属性结束的'}'之前找"value"
			limit=q;
			while(limit<plen&&p[limit]!='}')
				limit++;
			for(;q+5<=limit;q++)
			{
				if(p[q]=='v'&&memcmp(p+q,"value",5)==0)
				{
					q=onenet_skip_punct(p,q+5,plen);
					return onenet_parse_int_at(p,q,plen,out)!=0xFFFF;
				}
			}
			return 0;
		}
	}
	return 0;
}

//提取"id":"xxx"中的xxx到out（最多31字符），找不到输出空串
static void onenet_json_id(const u8 *p,u16 plen,u8 *out)
{
	u16 i,q;
	u8 n=0;

	out[0]=0;
	for(i=0;i+4<=plen;i++)
	{
		if(p[i]=='"'&&memcmp(p+i,"\"id\"",4)==0)
		{
			q=i+4;
			while(q<plen&&p[q]!=':')
				q++;
			if(q>=plen) return;
			q++;
			while(q<plen&&p[q]!='"')			//跳到值的开引号（兼容空白）
				q++;
			if(q>=plen) return;
			q++;
			while(q<plen&&p[q]!='"'&&n<31)
			{
				out[n++]=p[q++];
			}
			out[n]=0;
			return;
		}
	}
}

//解析一帧已拷出的MQTT报文：只认set主题的PUBLISH，提取Fan/Servo/Beep并回复set_reply
static void onenet_parse_mqtt(u8 *fr,u16 flen)
{
	u16 pos;
	u16 tl;
	u16 plen;
	u16 pkt_len;
	u32 rem,mult;
	u8 nb;
	u8 qos;
	u8 got=0;
	int v;
	u8 idbuf[32];
	u8 rp[112];

	if(flen<2||(fr[0]&0xF0)!=0x30)
		return;								//只处理PUBLISH（PINGRESP/SUBACK等忽略）

	//解码Remaining Length
	pos=1;rem=0;mult=1;nb=0;
	do
	{
		if(pos>=flen||nb>=4)
			return;
		rem+=(u32)(fr[pos]&0x7F)*mult;
		if(!(fr[pos]&0x80))
		{
			pos++;
			break;
		}
		mult<<=7;
		pos++;
		nb++;
	}while(1);

	if((u32)pos+rem>(u32)flen)
		return;								//帧不完整
	if((u16)(pos+2)>flen)
		return;

	tl=(u16)(((u16)fr[pos]<<8)|fr[pos+1]);	//主题长度
	pos+=2;
	if((u16)(pos+tl)>flen)
		return;
	if(tl!=(u16)strlen(ONENET_TOPIC_SET)
		||memcmp(fr+pos,ONENET_TOPIC_SET,tl)!=0)
	{
		//只对含"set"的异常主题打印（post/reply等周期性PUBLISH保持静默）
		u16 a,b;
		for(a=0;a+3<=tl;a++)
		{
			if(memcmp(fr+pos+a,"set",3)==0)
			{
				printf("[OneNET] PUBLISH topic mismatch(%d): ",tl);
				for(b=0;b<tl&&b<80;b++)
					putchar(fr[pos+b]);
				printf("\r\n");
				break;
			}
		}
		return;
	}
	pos+=tl;

	qos=(fr[0]>>1)&0x03;
	if(qos==1)
	{
		u8 ack[4];
		if((u16)(pos+2)>flen)
			return;
		ack[0]=0x40;ack[1]=0x02;				//PUBACK固定头
		ack[2]=fr[pos];ack[3]=fr[pos+1];		//原报文标识符
		ESP8266_TcpSend(ack,4);					//QoS1必须回PUBACK，否则平台重发
		pos+=2;
	}
	else if(qos!=0)
	{
		return;								//QoS2不支持
	}

	plen=(u16)(rem-(u32)(2+tl)-(qos?2:0));
	if((u16)(pos+plen)>flen)
		return;

	//上一条命令main还没取走则合并到同一帧，否则重新开始
	if(!ctrl_ready)
	{
		ctrl_pending.has_fan=0;
		ctrl_pending.has_servo=0;
		ctrl_pending.has_beep=0;
	}

	//提取开关属性（平台可只下发其中一部分，用has标志区分）
	if(onenet_json_int(fr+pos,plen,"Fan",&v))
	{
		ctrl_pending.has_fan=1;
		ctrl_pending.fan=v?1:0;
		got=1;
	}
	if(onenet_json_int(fr+pos,plen,"Servo",&v))
	{
		ctrl_pending.has_servo=1;
		ctrl_pending.servo=v?1:0;
		got=1;
	}
	if(onenet_json_int(fr+pos,plen,"Beep",&v))
	{
		ctrl_pending.has_beep=1;
		ctrl_pending.beep=v?1:0;
		got=1;
	}

	if(!got)
	{
		u16 k;
		u16 n=(plen<120)?plen:120;
		//set主题但一个字段都没解析到：打印原始负载，定位JSON格式/标识符问题
		printf("[OneNET] set recv but no field parsed, payload(%d):\r\n",plen);
		for(k=0;k<n;k++)
		{
			u8 c=fr[pos+k];
			putchar((c>=32&&c<127)?c:'.');
		}
		printf("\r\n");
		return;
	}

	//必须回复set_reply(code=200)，平台才判定下发成功
	onenet_json_id(fr+pos,plen,idbuf);
	sprintf((char*)rp,
		"{\"id\":\"%s\",\"code\":200,\"msg\":\"success\"}",idbuf);
	pkt_len=MQTT_BuildPublish(mqtt_txbuf,(u8*)ONENET_TOPIC_SET_REPLY,
		rp,(u16)strlen((char*)rp));
	ESP8266_TcpSend(mqtt_txbuf,pkt_len);
	ctrl_ready=1;
	printf("[OneNET] ctrl down: Fan=%d Srv=%d Beep=%d (mask %d%d%d)\r\n",
		ctrl_pending.fan,ctrl_pending.servo,ctrl_pending.beep,
			ctrl_pending.has_fan,ctrl_pending.has_servo,ctrl_pending.has_beep);
}

/*******************************************************************************
* 函 数 名 : OneNET_HandleDownlink
* 函数功能 : OS_READY每拍调用。扫描esp_rx_buf中的"+IPD,<len>:"，
*            等一帧TCP数据收齐后拷出MQTT报文并从缓冲区消费，再交协议解析。
*            拷贝到独立帧缓冲，不改写接收缓冲，避免与接收中断竞争。
*******************************************************************************/
void OneNET_HandleDownlink(void)
{
	static u8 frame[300];	//set下发报文通常<150字节，300足够；static避免占栈
	u16 i;
	u16 ipd_len;
	u16 dataoff;
	u8 ch;

	if(esp_rx_cnt<6)
		return;

	//1.定位"+IPD,"；没有则缓冲里只是AT回显文本，堆积过多时清空
	i=onenet_rx_pos((const u8*)"+IPD,",5);
	if(i==0xFFFF)
	{
		if(esp_rx_cnt>800)
			ESP8266_ClearRx();
		return;
	}
	if(i>0)
		ESP8266_Discard(i);					//丢掉帧前的OK/SEND OK等文本

	//2.解析数据长度，兼容单连接"+IPD,<len>:"和多连接"+IPD,<id>,<len>:"
	i=5;
	ipd_len=0;
	while(i<esp_rx_cnt)
	{
		ch=esp_rx_buf[i];
		if(ch>='0'&&ch<='9')
		{
			ipd_len=ipd_len*10+(ch-'0');
			i++;
		}
		else
			break;
	}
	if(i>=esp_rx_cnt)
		return;								//长度字段没收全，等下一拍
	if(esp_rx_buf[i]==',')
	{
		i++;
		ipd_len=0;							//第一段是连接号，重解析长度
		while(i<esp_rx_cnt)
		{
			ch=esp_rx_buf[i];
			if(ch>='0'&&ch<='9')
			{
				ipd_len=ipd_len*10+(ch-'0');
				i++;
			}
			else
				break;
		}
	}
	if(i>=esp_rx_cnt)
		return;
	if(esp_rx_buf[i]!=':')
	{
		ESP8266_Discard(5);					//头部畸形，丢弃后重新同步
		return;
	}
	dataoff=i+1;

	if(ipd_len==0||ipd_len>sizeof(frame))
	{
		printf("[OneNET] bad IPD len=%d, drop\r\n",ipd_len);
		ESP8266_ClearRx();					//异常长度无法定位边界，整体清空重同步
		return;
	}

	//3.等数据部分收齐（模块声明多少字节就应收到多少）
	if(esp_rx_cnt<(u16)(dataoff+ipd_len))
		return;

	//4.拷出→消费缓冲→解析（必须先拷贝再Discard，前移后地址内容会变）
	memcpy(frame,esp_rx_buf+dataoff,ipd_len);
	ESP8266_Discard((u16)(dataoff+ipd_len));
	onenet_parse_mqtt(frame,(u16)ipd_len);
}

/*******************************************************************************
* 函 数 名 : OneNET_GetProgress
* 函数功能 : 返回连接进度，供OLED显示
*            1=开始连接(上电/AT测试) 2=连WiFi中 3=连OneNET平台中 4=上线成功
*******************************************************************************/
u8 OneNET_GetProgress(void)
{
	switch(onenet_state)
	{
		case OS_INIT:
		case OS_RECONNECT:
			return 1;
		case OS_AT_READY:
		case OS_JOIN_AP:
			return 2;
		case OS_TCP_CONN:
		case OS_MQTT_CONN:
			return 3;
		case OS_READY:
			return 4;
		default:
			return 1;
	}
}

void OneNET_Task(void)
{
	u16 len;
	u8 cmd[96];

	switch(onenet_state)
	{
		case OS_INIT:
			//上电先等500ms让ESP8266启动，再发AT测试
			if(step_tick>=50)
			{
				if(ESP8266_SendATCmd((u8*)"AT",(u8*)"OK",1000)==0)
				{
					printf("[WiFi] AT OK\r\n");
					onenet_state=OS_AT_READY;
				}
				else
				{
					printf("[WiFi] ESP8266 no response, check wiring/EN\r\n");
				}
				step_tick=0;
			}
			break;

		case OS_AT_READY:
			//关闭回显，设为Station单模式（失败不致命，继续往下走）
			ESP8266_SendATCmd((u8*)"ATE0",(u8*)"OK",1000);
			ESP8266_SendATCmd((u8*)"AT+CWMODE=1",(u8*)"OK",1000);
			printf("[WiFi] joining AP...\r\n");
			onenet_state=OS_JOIN_AP;
			step_tick=0;
			break;

		case OS_JOIN_AP:
			//连接路由器，上电首次关联AP可能要十几秒，超时设15s
			sprintf((char*)cmd,"AT+CWJAP=\"%s\",\"%s\"",WIFI_SSID,WIFI_PWD);
			if(ESP8266_SendATCmd(cmd,(u8*)"WIFI GOT IP",15000)==0
				|| ESP8266_WaitAck((u8*)"OK",1000)==0)
			{
				printf("[WiFi] AP joined\r\n");
				onenet_state=OS_TCP_CONN;
			}
			else
			{
				printf("[WiFi] join AP fail, retry\r\n");
			}
			step_tick=0;
			break;

		case OS_TCP_CONN:
			//建立到OneNET的TCP连接
			sprintf((char*)cmd,"AT+CIPSTART=\"TCP\",\"%s\",%d",ONENET_HOST,ONENET_PORT);
			if(ESP8266_SendATCmd(cmd,(u8*)"CONNECT",8000)==0
				|| ESP8266_WaitAck((u8*)"OK",1000)==0)
			{
				printf("[WiFi] TCP connected\r\n");
				mqtt_cd=0;
				onenet_state=OS_MQTT_CONN;
			}
			else
			{
				printf("[WiFi] TCP fail, retry\r\n");
			}
			step_tick=0;
			break;

		case OS_MQTT_CONN:
			//cd为0时发起一次CONNECT，失败后置300（约3秒）冷却，避免高频重发
			if(mqtt_cd==0)
			{
				u8 rc=0;
				u8 ack;
				u16 w;
				u8 send_ok;

				//发送MQTT CONNECT报文（新版OneNET物模型鉴权三要素）：
				//  client_id = 设备名称   username = 产品ID   password = 自动生成的token
				len=MQTT_BuildConnect(mqtt_txbuf,
					(u8*)ONENET_DEVICE_NAME,
					(u8*)ONENET_PRODUCT_ID,
					mqtt_token,
					ONENET_KEEPALIVE);
				printf("[OneNET] CONNECT pkt len=%d\r\n",len);

				send_ok=(ESP8266_TcpSend(mqtt_txbuf,len)==0)?1:0;
				if(!send_ok)
				{
					last_err=0xFE;
					printf("[OneNET] CIPSEND failed(no '>' or no SEND OK)\r\n");
				}
				else
				{
					//等待并解析CONNACK：20 02 00 rc，rc=0成功，其余为拒绝原因
					ack=0;
					for(w=0;w<3500;w+=2)
					{
						ack=onenet_find_connack(&rc);
						if(ack)
							break;
						delay_ms(2);
					}
					if(ack==1)
					{
						online=1;
						last_err=0;
						keepalive_cnt=0;
						onenet_state=OS_READY;
						printf("[OneNET] MQTT online (CONNACK rc=0)\r\n");
						step_tick=0;
						break;
					}
					if(ack>=2)
					{
						//服务器明确拒绝：1协议版本 2 clientid非法 3服务不可用 4用户名密码错 5未授权
						last_err=rc;
						printf("[OneNET] rejected, CONNACK rc=%d\r\n",rc);
					}
					else
					{
						//没等到任何CONNACK：打印模块原始回显帮助定位（含+IPD/ERROR/CLOSED）
						last_err=0xFF;
						printf("[OneNET] no CONNACK, esp_rx_cnt=%d, raw:\r\n",esp_rx_cnt);
						{
							u16 k;
							u16 n=(esp_rx_cnt<200)?esp_rx_cnt:200;
							for(k=0;k<n;k++)
							{
								u8 c=esp_rx_buf[k];
								putchar((c>=32&&c<127)?c:'.');
							}
							printf("\r\n");
						}
					}
				}
				mqtt_cd=300;	//启动约3秒冷却
			}
			else
			{
				mqtt_cd--;
				//冷却期间模块若主动上报TCP已被关闭，则退回重建TCP
				if(mqtt_cd==0 && esp_rx_cnt>0
					&& (onenet_contains("CLOSED")||onenet_contains("link is not")))
				{
					printf("[OneNET] TCP closed, rebuild\r\n");
					onenet_state=OS_TCP_CONN;
					step_tick=0;
				}
			}
			break;

		case OS_READY:
			//上线后先订阅属性设置下发主题（失败下一拍重试，成功后不再发）
			if(!sub_done)
			{
				ESP8266_ClearRx();	//清掉CONNACK及AT回显残留
				len=MQTT_BuildSubscribe(mqtt_txbuf,(u8*)ONENET_TOPIC_SET,0);
				if(ESP8266_TcpSend(mqtt_txbuf,len)==0)
				{
					sub_done=1;
					printf("[OneNET] subscribed property/set\r\n");
				}
				keepalive_cnt=0;
				break;
			}
			//解析云端下发（+IPD中的PUBLISH帧），有命令则挂入ctrl_pending
			OneNET_HandleDownlink();
			//心跳：到达keepalive周期发送PINGREQ
			keepalive_cnt++;
			if(keepalive_cnt>=(u16)ONENET_KEEPALIVE*100)	//节拍10ms：120s=12000拍
			{
				len=MQTT_BuildPingReq(mqtt_txbuf);
				if(ESP8266_TcpSend(mqtt_txbuf,len)==0)
				{
					printf("[OneNET] ping\r\n");
				}
				else
				{
					//发送失败视为掉线，回初始化重连
					online=0;
					sub_done=0;
					onenet_state=OS_INIT;
				}
				keepalive_cnt=0;
			}
			//云端命令由OneNET_HandleDownlink解析，main用OneNET_GetCtrl取走执行
			break;

		case OS_RECONNECT:
			//预留：检测到掉线时的重连状态
			online=0;
			sub_done=0;
			onenet_state=OS_INIT;
			step_tick=0;
			break;
	}

	step_tick++;
}

/*******************************************************************************
* 函 数 名 : OneNET_PostData
* 函数功能 : 属性上报（物模型 property/post）
*            上报温湿度以及风扇/舵机/蜂鸣器开关状态（0关 1开）
*            注意：JSON中的标识符必须和OneNET控制台"物模型"中创建的属性
*            标识符完全一致（大小写敏感）：Temperature/Humidity/Fan/Servo/Beep
* 返 回 值 : 0发送成功 1未上线或发送失败
*******************************************************************************/
u8 OneNET_PostData(u8 temp,u8 humi,u8 fan,u8 servo,u8 beep)
{
	u8 json[180];
	u16 pkt_len;

	if(!online)
		return 1;

	sprintf((char*)json,
		"{\"id\":\"1\",\"version\":\"1.0\",\"params\":{"
		"\"Temperature\":{\"value\":%d},"
		"\"Humidity\":{\"value\":%d},"
		"\"Fan\":{\"value\":%d},"
		"\"Servo\":{\"value\":%d},"
		"\"Beep\":{\"value\":%d}}}",
		temp,humi,fan?1:0,servo?1:0,beep?1:0);

	pkt_len=MQTT_BuildPublish(mqtt_txbuf,(u8*)ONENET_TOPIC_POST,json,(u16)strlen((char*)json));
	if(ESP8266_TcpSend(mqtt_txbuf,pkt_len)==0)
	{
		printf("[OneNET] post T=%d H=%d Fan=%d Srv=%d Beep=%d\r\n",
			temp,humi,fan?1:0,servo?1:0,beep?1:0);
		return 0;
	}
	return 1;
}

/*******************************************************************************
* 函 数 名 : OneNET_GetCtrl
* 函数功能 : main主循环轮询取走一条云端"属性设置"命令
* 返 回 值 : 1=取到新命令（内容写入*c后自动清除，等下一条）；0=无命令
*******************************************************************************/
u8 OneNET_GetCtrl(OnenetCtrl *c)
{
	if(!ctrl_ready)
		return 0;

	c->fan=ctrl_pending.fan;
	c->servo=ctrl_pending.servo;
	c->beep=ctrl_pending.beep;
	c->has_fan=ctrl_pending.has_fan;
	c->has_servo=ctrl_pending.has_servo;
	c->has_beep=ctrl_pending.has_beep;

	ctrl_ready=0;
	ctrl_pending.has_fan=0;
	ctrl_pending.has_servo=0;
	ctrl_pending.has_beep=0;
	return 1;
}
