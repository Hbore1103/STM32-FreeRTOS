#include "esp8266.h"
#include "string.h"

/*
 * 第一层：硬件/AT驱动层
 * 负责：USART3收发、AT指令交互、TCP透传
 * 不包含任何MQTT协议和OneNET平台内容
 */

u8 esp_rx_buf[ESP_RX_BUF_SIZE];
volatile u16 esp_rx_cnt=0;

/*******************************************************************************
* 函 数 名 : ESP8266_UART_Init
* 函数功能 : USART3初始化，PB10=TX(复用推挽) PB11=RX(浮空输入)
*******************************************************************************/
void ESP8266_UART_Init(u32 baud)
{
	GPIO_InitTypeDef GPIO_InitStructure;
	USART_InitTypeDef USART_InitStructure;
	NVIC_InitTypeDef NVIC_InitStructure;

	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB,ENABLE);
	RCC_APB1PeriphClockCmd(RCC_APB1Periph_USART3,ENABLE);

	//PB10 TX 复用推挽输出
	GPIO_InitStructure.GPIO_Pin=GPIO_Pin_10;
	GPIO_InitStructure.GPIO_Speed=GPIO_Speed_50MHz;
	GPIO_InitStructure.GPIO_Mode=GPIO_Mode_AF_PP;
	GPIO_Init(GPIOB,&GPIO_InitStructure);

	//PB11 RX 浮空输入
	GPIO_InitStructure.GPIO_Pin=GPIO_Pin_11;
	GPIO_InitStructure.GPIO_Mode=GPIO_Mode_IN_FLOATING;
	GPIO_Init(GPIOB,&GPIO_InitStructure);

	USART_InitStructure.USART_BaudRate=baud;
	USART_InitStructure.USART_WordLength=USART_WordLength_8b;
	USART_InitStructure.USART_StopBits=USART_StopBits_1;
	USART_InitStructure.USART_Parity=USART_Parity_No;
	USART_InitStructure.USART_HardwareFlowControl=USART_HardwareFlowControl_None;
	USART_InitStructure.USART_Mode=USART_Mode_Rx|USART_Mode_Tx;
	USART_Init(USART3,&USART_InitStructure);

	USART_Cmd(USART3,ENABLE);
	USART_ITConfig(USART3,USART_IT_RXNE,ENABLE);	//开启接收中断

	NVIC_InitStructure.NVIC_IRQChannel=USART3_IRQn;
	NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority=2;
	NVIC_InitStructure.NVIC_IRQChannelSubPriority=2;
	NVIC_InitStructure.NVIC_IRQChannelCmd=ENABLE;
	NVIC_Init(&NVIC_InitStructure);
}

//发送一个字节
void ESP8266_SendByte(u8 b)
{
	USART_SendData(USART3,b);
	while(USART_GetFlagStatus(USART3,USART_FLAG_TXE)==RESET);
}

//发送字符串
void ESP8266_SendString(u8 *s)
{
	while(*s)
	{
		ESP8266_SendByte(*s++);
	}
}

//清空接收缓冲
void ESP8266_ClearRx(void)
{
	esp_rx_cnt=0;
}

/*******************************************************************************
* 函 数 名 : ESP8266_Discard
* 函数功能 : 消费掉缓冲区最前面的n个字节（剩余字节前移）。
*            用于处理完一帧+IPD数据后释放空间，防止缓冲区堆满。
*            memmove期间短暂关闭接收中断，避免与接收中断竞争。
*******************************************************************************/
void ESP8266_Discard(u16 n)
{
	u16 left;

	if(n==0)
		return;

	USART_ITConfig(USART3,USART_IT_RXNE,DISABLE);	//关接收中断，保护memmove
	if(n>=esp_rx_cnt)
	{
		esp_rx_cnt=0;
	}
	else
	{
		left=esp_rx_cnt-n;
		memmove(esp_rx_buf,esp_rx_buf+n,left);
		esp_rx_cnt=left;
	}
	USART_ITConfig(USART3,USART_IT_RXNE,ENABLE);
}

/*******************************************************************************
* 函 数 名 : ESP8266_WaitAck
* 函数功能 : 在接收缓冲中查找应答字符串，超时返回1。
*            采用定长查找，绝不向接收缓冲区写0（避免与接收中断竞争覆盖新字节）
*******************************************************************************/
u8 ESP8266_WaitAck(u8 *ack,u16 timeout_ms)
{
	u16 wait=0;
	u16 alen;
	u16 i,j;

	alen=(u16)strlen((char*)ack);
	if(alen==0)
		return 1;

	while(wait<timeout_ms)
	{
		if(esp_rx_cnt>=alen)
		{
			u16 max=(esp_rx_cnt>=alen)?(u16)(esp_rx_cnt-alen):0;
			for(i=0;i<=max;i++)
			{
				for(j=0;j<alen;j++)
				{
					if(esp_rx_buf[i+j]!=ack[j])
						break;
				}
				if(j==alen)
					return 0;
			}
		}
		delay_ms(2);
		wait+=2;
	}
	return 1;
}

/*******************************************************************************
* 函 数 名 : ESP8266_SendATCmd
* 函数功能 : 发送一条AT指令(自动补\r\n)并等待应答
* 输    入 : cmd 指令内容(不含\r\n)；ack 期待应答；timeout_ms 超时
* 返 回 值 : 0成功 1失败
*******************************************************************************/
u8 ESP8266_SendATCmd(u8 *cmd,u8 *ack,u16 timeout_ms)
{
	ESP8266_ClearRx();
	ESP8266_SendString(cmd);
	ESP8266_SendString((u8*)"\r\n");
	return ESP8266_WaitAck(ack,timeout_ms);
}

/*******************************************************************************
* 函 数 名 : ESP8266_TcpSend
* 函数功能 : 在已建立的TCP连接上发送二进制数据（MQTT报文由此发送）
*            流程：AT+CIPSEND=<len> -> 等待'>' -> 发送原始字节 -> 等SEND OK
* 返 回 值 : 0成功 1失败
*******************************************************************************/
u8 ESP8266_TcpSend(u8 *data,u16 len)
{
	u8 cmd[32];
	u16 i;

	sprintf((char*)cmd,"AT+CIPSEND=%d",len);
	if(ESP8266_SendATCmd(cmd,(u8*)">",2000))	//等模块给出数据输入提示符
		return 1;

	ESP8266_ClearRx();
	for(i=0;i<len;i++)
	{
		ESP8266_SendByte(data[i]);
	}
	return ESP8266_WaitAck((u8*)"SEND OK",3000);
}

/*******************************************************************************
* 函 数 名 : USART3_IRQHandler
* 函数功能 : USART3接收中断，每收到一字节直接存入缓冲区
*            中断中只做最简单的存取，保证不丢字节
*******************************************************************************/
void USART3_IRQHandler(void)
{
	u8 r;

	if(USART_GetITStatus(USART3,USART_IT_RXNE)!=RESET)
	{
		r=USART_ReceiveData(USART3);
		if(esp_rx_cnt<ESP_RX_BUF_SIZE)
		{
			esp_rx_buf[esp_rx_cnt++]=r;
		}
	}
}
