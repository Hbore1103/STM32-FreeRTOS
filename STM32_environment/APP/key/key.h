#ifndef _key_H
#define _key_H

#include "system.h"
#include "SysTick.h"

/*  K1独立按键引脚定义：PA15（JTDI）  */
#define K1_PIN			GPIO_Pin_15
#define K1_PORT			GPIOA
#define K1_PORT_RCC		RCC_APB2Periph_GPIOA

/*  K2独立按键引脚定义：PA14（SWCLK）  */
#define K2_PIN			GPIO_Pin_14
#define K2_PORT			GPIOA
#define K2_PORT_RCC		RCC_APB2Periph_GPIOA

/*  K3独立按键引脚定义：PA13（SWDIO）  */
#define K3_PIN			GPIO_Pin_13
#define K3_PORT			GPIOA
#define K3_PORT_RCC		RCC_APB2Periph_GPIOA

/*  位带读取引脚电平  */
#define K1				PAin(15)
#define K2				PAin(14)
#define K3				PAin(13)

/*  按键返回值  */
#define KEY_NONE		0
#define K1_PRESS		1
#define K2_PRESS		2
#define K3_PRESS		3

void KEY_Init(void);
u8 KEY_Scan(void);

#endif
