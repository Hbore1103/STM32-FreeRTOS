#include "beep.h"

//硬件说明：板载/外接的是无源蜂鸣器，内部无振荡源，必须输入2~5kHz方波才会发声。
//若只给固定高电平，振膜只在通断瞬间动一下（"嗒"的开关声），不会持续鸣响。
//实现方式：TIM4每200us进一次更新中断，在中断里翻转PB0，
//中断频率5kHz -> 引脚方波频率2.5kHz（无源蜂鸣器常用谐振频率）。
//Beep_On开启定时器即发声，Beep_Off关闭定时器并拉低引脚。

//蜂鸣器初始化
void Beep_Init(void)
{
	GPIO_InitTypeDef GPIO_InitStructure;
	TIM_TimeBaseInitTypeDef TIM_TimeBaseStructure;
	NVIC_InitTypeDef NVIC_InitStructure;

	RCC_APB2PeriphClockCmd(BEEP_PORT_RCC,ENABLE);

	//PB0推挽输出，默认低电平
	GPIO_InitStructure.GPIO_Pin=BEEP_PIN;
	GPIO_InitStructure.GPIO_Mode=GPIO_Mode_Out_PP;	//推挽输出
	GPIO_InitStructure.GPIO_Speed=GPIO_Speed_50MHz;
	GPIO_Init(BEEP_PORT,&GPIO_InitStructure);
	GPIO_ResetBits(BEEP_PORT,BEEP_PIN);

	//TIM4定时基准：72MHz/72=1MHz计数，周期200 -> 200us中断一次
	RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM4,ENABLE);
	TIM_TimeBaseStructure.TIM_Prescaler=72-1;
	TIM_TimeBaseStructure.TIM_CounterMode=TIM_CounterMode_Up;
	TIM_TimeBaseStructure.TIM_Period=200-1;
	TIM_TimeBaseStructure.TIM_ClockDivision=TIM_CKD_DIV1;
	TIM_TimeBaseInit(TIM4,&TIM_TimeBaseStructure);

	TIM_ClearFlag(TIM4,TIM_FLAG_Update);	//清除初始化时产生的更新标志
	TIM_ITConfig(TIM4,TIM_IT_Update,ENABLE);

	//配置TIM4更新中断，优先级设低，不影响主循环和其他功能
	NVIC_InitStructure.NVIC_IRQChannel=TIM4_IRQn;
	NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority=2;
	NVIC_InitStructure.NVIC_IRQChannelSubPriority=2;
	NVIC_InitStructure.NVIC_IRQChannelCmd=ENABLE;
	NVIC_Init(&NVIC_InitStructure);
	//此时不启动定时器，调用Beep_On后才开始输出方波
}

//蜂鸣器响：启动TIM4，中断持续翻转PB0输出2.5kHz方波
void Beep_On(void)
{
	GPIO_ResetBits(BEEP_PORT,BEEP_PIN);	//从低电平开始翻转，保证占空比50%
	TIM_SetCounter(TIM4,0);
	TIM_Cmd(TIM4,ENABLE);
}

//蜂鸣器不响：停止TIM4并拉低PB0
void Beep_Off(void)
{
	TIM_Cmd(TIM4,DISABLE);
	GPIO_ResetBits(BEEP_PORT,BEEP_PIN);
}

//TIM4更新中断服务函数：每200us翻转一次PB0 -> 2.5kHz方波
void TIM4_IRQHandler(void)
{
	if(TIM_GetITStatus(TIM4,TIM_IT_Update)!=RESET)
	{
		TIM_ClearITPendingBit(TIM4,TIM_IT_Update);
		BEEP=!BEEP;
	}
}
