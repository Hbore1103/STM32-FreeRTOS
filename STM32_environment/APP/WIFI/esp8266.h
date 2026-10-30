#ifndef _esp8266_H
#define _esp8266_H

#include "system.h"
#include "SysTick.h"
#include "stdio.h"

/*
 * ESP8266-01S 硬件接线（USART3）
 *   ESP8266 TX    -> PB11 (USART3_RX)   注意交叉
 *   ESP8266 RX    -> PB10 (USART3_TX)
 *   ESP8266 VCC   -> 3.3V（发射峰值电流300mA以上，供电必须够）
 *   ESP8266 GND   -> GND（与STM32共地）
 *   ESP8266 EN/CH_PD -> 3.3V（必须拉高模块才能启动）
 *   IO0 悬空或拉高（拉低会进入烧录模式）
 */

#define ESP8266_BAUD			115200	//ESP8266出厂默认波特率，若你的模块是9600请改这里

#define ESP_RX_BUF_SIZE			1024	//接收缓冲区大小（AT应答和+IPD数据共用）

//接收缓冲区和计数，中断中写入，上层可直接读取解析
extern u8  esp_rx_buf[ESP_RX_BUF_SIZE];
extern volatile u16 esp_rx_cnt;

void ESP8266_UART_Init(u32 baud);								//USART3硬件初始化
void ESP8266_SendByte(u8 b);									//发送一个字节
void ESP8266_SendString(u8 *s);									//发送字符串
void ESP8266_ClearRx(void);										//清空接收缓冲区
void ESP8266_Discard(u16 n);									//从缓冲区头部丢弃n个字节（消费已处理的+IPD帧）
u8   ESP8266_WaitAck(u8 *ack, u16 timeout_ms);					//等待指定应答字符串，0=收到 1=超时
u8   ESP8266_SendATCmd(u8 *cmd, u8 *ack, u16 timeout_ms);		//发AT指令并等应答，0=成功 1=失败
u8   ESP8266_TcpSend(u8 *data, u16 len);						//TCP透传发送二进制数据(MQTT报文)

#endif
