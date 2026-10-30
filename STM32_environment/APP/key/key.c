#include "key.h"

//独立按键初始化
//K1->PA15(JTDI)，K2->PA14(SWCLK)，K3->PA13(SWDIO)，均为低电平有效
//注意：PA13/PA14/PA15上电默认是调试接口，必须完全关闭SWJ(JTAG+SWD)才能当普通GPIO使用
//警告：关闭SWD后程序运行期间ST-Link将无法连接，再次烧录需按住复位键连接(connect under reset)
void KEY_Init(void)
{
	GPIO_InitTypeDef GPIO_InitStructure;

	RCC_APB2PeriphClockCmd(K1_PORT_RCC,ENABLE);

	//完全关闭JTAG和SWD，释放PA13/PA14/PA15/PB3/PB4
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_AFIO,ENABLE);
	GPIO_PinRemapConfig(GPIO_Remap_SWJ_Disable,ENABLE);

	//K1、K2、K3配置为上拉输入：未按下为高电平，按下为低电平
	GPIO_InitStructure.GPIO_Pin=K1_PIN|K2_PIN|K3_PIN;
	GPIO_InitStructure.GPIO_Mode=GPIO_Mode_IPU;	//上拉输入
	GPIO_InitStructure.GPIO_Speed=GPIO_Speed_50MHz;
	GPIO_Init(GPIOA,&GPIO_InitStructure);
}

//按键扫描（非阻塞，带10ms软件消抖）
//返回值：KEY_NONE 无按键；K1_PRESS/K2_PRESS/K3_PRESS 完成一次有效按下
//说明：检测到按下沿并消抖确认后返回一次，按住不放不会重复返回，松手后才能触发下一次
u8 KEY_Scan(void)
{
	static u8 key_up=1;	//按键全部松开标志

	if(key_up&&((K1==0)||(K2==0)||(K3==0)))	//检测到按下沿
	{
		delay_ms(10);	//软件消抖
		key_up=0;
		if(K1==0)return K1_PRESS;
		if(K2==0)return K2_PRESS;
		if(K3==0)return K3_PRESS;
	}
	else if((K1==1)&&(K2==1)&&(K3==1))
	{
		key_up=1;		//所有按键已松开，允许下次触发
	}
	return KEY_NONE;
}
