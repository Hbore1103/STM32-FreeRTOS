#ifndef _servo_H
#define _servo_H

#include "system.h"

/*  舵机PWM引脚定义：PA7 -> TIM3_CH2(默认映射)  */
#define SERVO_PIN			GPIO_Pin_7
#define SERVO_PORT			GPIOA
#define SERVO_PORT_RCC		RCC_APB2Periph_GPIOA

#define SERVO_TIM			TIM3
#define SERVO_TIM_RCC		RCC_APB1Periph_TIM3

void Servo_Init(void);
void Servo_SetAngle(u8 angle);

#endif
