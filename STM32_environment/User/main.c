/*
深圳市普中科技有限公司（PRECHIN 普中）
技术支持：www.prechin.net
PRECHIN
 普中

实验名称：DHT11温湿度传感器实验

接线说明：DHT11温湿度模块-->STM32 IO
              VCC-->3V3
              GND-->GND
              OUT-->PB3

实验现象：程序下载成功后，插上OLED液晶，显示模块检测的温度和湿度值

注意事项：

*/

#include "system.h"
#include "SysTick.h"
#include "led.h"
#include "usart.h"
#include "dht11.h"
#include "oled.h"
#include "key.h"
#include "dc_motor.h"
#include "servo.h"
#include "beep.h"
#include "onenet.h"


u8 temp=0;
u8 humi=0;

void data_pros()   //数据处理函数
{
	DHT11_Read_Data(&temp,&humi);

	OLED_ShowNum(5*6,0,temp,2,12);
	OLED_ShowNum(5*6,12,humi,2,12);
	OLED_Refresh_Gram();
	printf("Temp=%dC  Humi=%d%%RH\r\n",temp,humi);
}

int main()
{
	u8 i=0;
	u8 key=0;
	u8 fan=0;
	u8 srv=0;
	u8 temp_alarm=0;	//温度报警锁存：温度超过35度置1，降回35度以下清0，防止持续高温反复响
	u8 beep_active=0;	//蜂鸣器正在响标志（自动或手动）
	u8 beep_manual=0;	//云端手动鸣响标志：1=云控开，不参与3秒自动倒计时
	u16 beep_timer=0;	//蜂鸣器剩余响的时间，单位10ms，300=3秒
	OnenetCtrl ctrl;	//云端下发的属性设置命令
	u8 net_prog=0;		//OneNET连接进度（上次显示值）：1开始 2连WiFi 3连平台 4成功
	u8 prog=0;			//OneNET当前连接进度
	u8 post_cnt=0;		//温湿度上报分频计数（200ms读一次，50次=10秒上报）
	SysTick_Init(72);
	NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2);  //中断优先级分组 分2组
	LED_Init();
	USART1_Init(115200);
	OLED_Init();
	KEY_Init();
	DC_Motor_Init();
	Servo_Init();
	Beep_Init();
	OneNET_Init();			//ESP8266(USART3)初始化

	OLED_ShowString(0,0,"DHT11 Init...",12);
	OLED_Refresh_Gram();
	while(DHT11_Init())     //检测DHT11是否存在
	{
		printf("DHT11 Check Error!\r\n");
		OLED_ShowString(0,0,"DHT11 Check Error!",12);
		OLED_Refresh_Gram();
		delay_ms(500);
	}
	printf("DHT11 Check OK!\r\n");
	OLED_Clear();
	OLED_ShowString(0,0,  "Temp:  C",12);
	OLED_ShowString(0,12, "Humi:  %",12);
	OLED_ShowString(0,24, "Fan:OFF",12);
	OLED_ShowString(0,36, "Srv:OFF",12);
	OLED_ShowString(0,48, "Beep:OFF",12);
	OLED_ShowString(15*6,0,"Net:",12);			//右上角联网进度标签
	OLED_ShowNum(19*6,0,1,1,12);				//上电即显示1：开始连接
	OLED_Refresh_Gram();

	while(1)
	{
		i++;
		if(i%20==0)
		{
			data_pros();     //读取一次DHT11数据最少要大于100ms
			//上线后每10秒上报一次全部状态（50次读取×200ms）
			if(OneNET_IsOnline())
			{
				post_cnt++;
				if(post_cnt>=50)
				{
					post_cnt=0;
					OneNET_PostData(temp,humi,fan,srv,beep_active);
				}
			}
			//温度超过35度的上升沿触发一次蜂鸣器，响3秒后自动关闭
			if(temp>35)
			{
				if(!temp_alarm)	//温度刚从35度以下升到35度以上
				{
					temp_alarm=1;
					if(!beep_manual)	//云端手动鸣响中，不抢控制权（保持手动长响）
					{
						beep_active=1;
						beep_timer=300;	//300*10ms=3秒
						Beep_On();
						OLED_ShowString(5*6,48,"ON ",12);
						OLED_Refresh_Gram();
						OneNET_PostData(temp,humi,fan,srv,beep_active);	//蜂鸣器响起，立即上报Beep=1
					}
				}
			}
			else
			{
				temp_alarm=0;	//温度降回35度以下，允许下次超限再响
			}
		}

		//蜂鸣器3秒非阻塞计时（云端手动鸣响时不倒计时，由云端Beep=0关闭）
		if(beep_active && !beep_manual)
		{
			beep_timer--;
			if(beep_timer==0)
			{
				beep_active=0;
				Beep_Off();
				OLED_ShowString(5*6,48,"OFF",12);
				OLED_Refresh_Gram();
				OneNET_PostData(temp,humi,fan,srv,beep_active);	//蜂鸣器停，立即上报Beep=0
			}
		}

		key=KEY_Scan();
		if(key==K1_PRESS)
		{
			fan=!fan;
			LED1=!fan;
			if(fan)
			{
				DC_Motor_On();
				OLED_ShowString(4*6,24,"ON ",12);
			}
			else
			{
				DC_Motor_Off();
				OLED_ShowString(4*6,24,"OFF",12);
			}
			OLED_Refresh_Gram();
			OneNET_PostData(temp,humi,fan,srv,beep_active);	//风扇开关变化，立即上报Fan状态
		}
		else if(key==K2_PRESS)
		{
			srv=!srv;
			LED2=!srv;
			if(srv)
			{
				Servo_SetAngle(180);
				OLED_ShowString(4*6,36,"ON ",12);
			}
			else
			{
				Servo_SetAngle(0);
				OLED_ShowString(4*6,36,"OFF",12);
			}
			OLED_Refresh_Gram();
			OneNET_PostData(temp,humi,fan,srv,beep_active);	//舵机开关变化，立即上报Servo状态
		}

		//ESP8266联网状态机（上电连网阶段内部有AT超时等待；上线后只做保活，不阻塞）
		OneNET_Task();
		prog=OneNET_GetProgress();
		if(prog!=net_prog)	//进度变化时才刷新右上角，不做无谓刷屏
		{
			OLED_ShowNum(19*6,0,prog,1,12);
			OLED_Refresh_Gram();
			net_prog=prog;
		}

		//云端属性设置：平台下发哪个字段就执行哪个，与本地按键共用fan/srv/beep状态
		if(OneNET_GetCtrl(&ctrl))
		{
			if(ctrl.has_fan)
			{
				fan=ctrl.fan?1:0;
				LED1=!fan;
				if(fan)
				{
					DC_Motor_On();
					OLED_ShowString(4*6,24,"ON ",12);
				}
				else
				{
					DC_Motor_Off();
					OLED_ShowString(4*6,24,"OFF",12);
				}
			}
			if(ctrl.has_servo)
			{
				srv=ctrl.servo?1:0;
				LED2=!srv;
				if(srv)
				{
					Servo_SetAngle(180);
					OLED_ShowString(4*6,36,"ON ",12);
				}
				else
				{
					Servo_SetAngle(0);
					OLED_ShowString(4*6,36,"OFF",12);
				}
			}
			if(ctrl.has_beep)
			{
				if(ctrl.beep)
				{
					beep_active=1;
					beep_manual=1;	//手动鸣响，不走3秒自动停
					Beep_On();
					OLED_ShowString(5*6,48,"ON ",12);
				}
				else
				{
					beep_active=0;
					beep_manual=0;
					Beep_Off();
					OLED_ShowString(5*6,48,"OFF",12);
				}
			}
			OLED_Refresh_Gram();
			OneNET_PostData(temp,humi,fan,srv,beep_active);	//回传执行后的真实状态，云端UI同步
		}

		delay_ms(10);
	}
}