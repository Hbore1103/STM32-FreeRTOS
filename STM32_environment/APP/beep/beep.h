#ifndef _beep_H
#define _beep_H

#include "system.h"

/*  蜂鸣器控制引脚定义：PB0  */
#define BEEP_PORT			GPIOB
#define BEEP_PIN			GPIO_Pin_0
#define BEEP_PORT_RCC		RCC_APB2Periph_GPIOB

/*  位带操作控制引脚输出电平  */
#define BEEP				PBout(0)

void Beep_Init(void);
void Beep_On(void);
void Beep_Off(void);

#endif
