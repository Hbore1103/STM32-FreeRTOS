#include "dc_motor.h"

//直流电机(风扇)初始化
//PB8 推挽输出，默认低电平（风扇停止）
void DC_Motor_Init(void)
{
	GPIO_InitTypeDef GPIO_InitStructure;	//定义GPIO初始化结构体

	RCC_APB2PeriphClockCmd(DC_MOTOR_PORT_RCC,ENABLE);

	GPIO_InitStructure.GPIO_Pin=DC_MOTOR_PIN;  	//选择要配置的IO口
	GPIO_InitStructure.GPIO_Mode=GPIO_Mode_Out_PP;	 //设置推挽输出模式
	GPIO_InitStructure.GPIO_Speed=GPIO_Speed_50MHz;	  //设置传输速率为50MHz
	GPIO_Init(DC_MOTOR_PORT,&GPIO_InitStructure); 	   /* 初始化GPIO */
	GPIO_ResetBits(DC_MOTOR_PORT,DC_MOTOR_PIN);		//默认低电平，风扇处于停止状态
}

//风扇转动（输出高电平）
void DC_Motor_On(void)
{
	GPIO_SetBits(DC_MOTOR_PORT,DC_MOTOR_PIN);
}

//风扇停止（输出低电平）
void DC_Motor_Off(void)
{
	GPIO_ResetBits(DC_MOTOR_PORT,DC_MOTOR_PIN);
}

//翻转风扇状态：转->停，停->转
void DC_Motor_Turn(void)
{
	DC_MOTOR=!DC_MOTOR;
}
