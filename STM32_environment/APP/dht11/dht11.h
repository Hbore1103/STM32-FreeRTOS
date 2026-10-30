#ifndef _dht11_H
#define _dht11_H

#include "system.h"
#include "SysTick.h"


#define DHT11 			GPIO_Pin_3
#define GPIO_DHT11 		GPIOB
#define DHT11_PORT_RCC	RCC_APB2Periph_GPIOB

#define DHT11_DQ_IN 	PBin(3)	//输入
#define DHT11_DQ_OUT 	PBout(3)  //输出

void DHT11_IO_OUT(void);
void DHT11_IO_IN(void);
u8 DHT11_Init(void);
void DHT11_Rst(void);
u8 DHT11_Check(void);
u8 DHT11_Read_Bit(void);
u8 DHT11_Read_Byte(void);
u8 DHT11_Read_Data(u8 *temp,u8 *humi);


#endif
