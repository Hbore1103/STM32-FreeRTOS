#include "servo.h"

//舵机初始化：TIM3_CH2 -> PA7 输出50Hz PWM（周期20ms）
//定时器输入时钟72MHz：预分频72-1 -> 计数频率1MHz，即1个计数=1us
//自动重装20000-1 -> 周期20000us=20ms（50Hz）
//位置舵机靠脉宽控制角度（SG90等常见型号）：0.5ms->0度，2.5ms->180度
void Servo_Init(void)
{
	GPIO_InitTypeDef GPIO_InitStructure;
	TIM_TimeBaseInitTypeDef TIM_TimeBaseStructure;
	TIM_OCInitTypeDef TIM_OCInitStructure;

	RCC_APB2PeriphClockCmd(SERVO_PORT_RCC,ENABLE);
	RCC_APB1PeriphClockCmd(SERVO_TIM_RCC,ENABLE);

	//PA7配置为复用推挽输出（LED_Init曾把PA7配成普通输出，这里重新配置）
	GPIO_InitStructure.GPIO_Pin=SERVO_PIN;
	GPIO_InitStructure.GPIO_Mode=GPIO_Mode_AF_PP;
	GPIO_InitStructure.GPIO_Speed=GPIO_Speed_50MHz;
	GPIO_Init(SERVO_PORT,&GPIO_InitStructure);

	//定时基准：1MHz计数，20ms周期
	TIM_TimeBaseStructure.TIM_Prescaler=72-1;
	TIM_TimeBaseStructure.TIM_CounterMode=TIM_CounterMode_Up;
	TIM_TimeBaseStructure.TIM_Period=20000-1;
	TIM_TimeBaseStructure.TIM_ClockDivision=TIM_CKD_DIV1;
	TIM_TimeBaseInit(SERVO_TIM,&TIM_TimeBaseStructure);

	//通道2 PWM模式1，初始脉宽0.5ms对应0度
	TIM_OCInitStructure.TIM_OCMode=TIM_OCMode_PWM1;
	TIM_OCInitStructure.TIM_OutputState=TIM_OutputState_Enable;
	TIM_OCInitStructure.TIM_Pulse=500;
	TIM_OCInitStructure.TIM_OCPolarity=TIM_OCPolarity_High;
	TIM_OC2Init(SERVO_TIM,&TIM_OCInitStructure);

	TIM_OC2PreloadConfig(SERVO_TIM,TIM_OCPreload_Enable);
	TIM_ARRPreloadConfig(SERVO_TIM,ENABLE);

	TIM_Cmd(SERVO_TIM,ENABLE);	//持续输出PWM以保持舵机角度
}

//设置舵机角度（0~180度）
//比较值=脉宽us：0度500us，180度2500us，线性换算
void Servo_SetAngle(u8 angle)
{
	u16 pulse;

	if(angle>180)angle=180;
	pulse=500+angle*2000/180;
	TIM_SetCompare2(SERVO_TIM,pulse);
}
