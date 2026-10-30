#ifndef _dc_motor_H
#define _dc_motor_H

#include "system.h"

/*  直流电机(风扇)控制引脚定义：PB8  */
#define DC_MOTOR_PIN			GPIO_Pin_8
#define DC_MOTOR_PORT			GPIOB
#define DC_MOTOR_PORT_RCC		RCC_APB2Periph_GPIOB

/*  位带操作控制引脚输出电平  */
#define DC_MOTOR				PBout(8)

void DC_Motor_Init(void);
void DC_Motor_On(void);
void DC_Motor_Off(void);
void DC_Motor_Turn(void);

#endif
