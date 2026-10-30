#include "dht11.h"


//DHT11初始化，返回0成功，1失败
u8 DHT11_Init()
{
	GPIO_InitTypeDef GPIO_InitStructure;

	RCC_APB2PeriphClockCmd(DHT11_PORT_RCC,ENABLE);

	//PB3复位后默认为JTAG引脚(JTDO)，必须关闭JTAG后才能当GPIO用；
	//工程在KEY_Init中已执行过此重映射（K2用PA14所需），这里重复执行一次，
	//保证DHT11模块自身不依赖初始化顺序
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_AFIO,ENABLE);
	GPIO_PinRemapConfig(GPIO_Remap_SWJ_Disable,ENABLE);

	GPIO_InitStructure.GPIO_Pin=DHT11;
	GPIO_InitStructure.GPIO_Mode=GPIO_Mode_Out_PP;	//配置为推挽输出，用于发送起始信号
	GPIO_InitStructure.GPIO_Speed=GPIO_Speed_50MHz;
	GPIO_Init(GPIO_DHT11,&GPIO_InitStructure);
	GPIO_SetBits(GPIO_DHT11,DHT11);	   //拉高数据线

	DHT11_Rst();	   //主机发送复位信号
	return DHT11_Check();	 //等待DHT11响应
}

//复位DHT11：主机拉低DQ至少18ms，然后释放20~40us等待DHT11响应
void DHT11_Rst()	   
{                 
	DHT11_IO_OUT(); 	//切换到输出模式
    DHT11_DQ_OUT=0; 	//拉低DQ
    delay_ms(20);    	//拉低至少18ms
    DHT11_DQ_OUT=1; 	//释放DQ，由上拉电阻拉高
	delay_us(30);     	//主机拉高20~40us后读取响应
}

//检测DHT11是否响应
//返回值：1-未检测到DHT11；0-检测到
u8 DHT11_Check() 	   
{   
	u8 retry=0;
	DHT11_IO_IN();//切换到输入模式等待响应
    while (DHT11_DQ_IN&&retry<100)//DHT11会拉低DQ 40~50us作为响应起始
	{
		retry++;
		delay_us(1);
	};	 
	if(retry>=100)return 1;	//超时未响应，DHT11不存在或损坏
	else retry=0;
    while (!DHT11_DQ_IN&&retry<100)//DHT11响应的低电平持续80us
	{
		retry++;
		delay_us(1);
	};
	if(retry>=100)return 1;	//响应结束后DHT11拉高80us
	return 0;			    //检测成功
}

//读取DHT11的一个数据位
//返回值：读到的值 1/0
u8 DHT11_Read_Bit(void) 			 
{
  	u8 retry=0;
	while(DHT11_DQ_IN&&retry<100)//等待DHT11拉低（50us开始）
	{
		retry++;
		delay_us(1);
	}
	retry=0;
	while(!DHT11_DQ_IN&&retry<100)//读取高电平持续时间：26-28us表示0，70us表示1
	{
		retry++;
		delay_us(1);
	}
	delay_us(40);//延时40us后判断
	if(DHT11_DQ_IN)return 1;
	else return 0;		   
}

//读取DHT11的一个字节（8位）
//返回值：读到的一个字节数据
u8 DHT11_Read_Byte(void)    
{        
    u8 i,dat;
    dat=0;
	for (i=0;i<8;i++) 
	{
   		dat<<=1; 
	    dat|=DHT11_Read_Bit();
    }						    
    return dat;
}

//读取DHT11的完整数据（40位）
//temp:温度值指针（范围 0~50°C）
//humi:湿度值指针（范围 20%~90%）
//返回值：0-读取成功；1-读取失败
u8 DHT11_Read_Data(u8 *temp,u8 *humi)    
{        
  	u8 buf[5];
	u8 i;
	DHT11_Rst();
	if(DHT11_Check()==0)
	{
		for(i=0;i<5;i++)//读取40位数据，存入buf[0]~buf[4]
		{
			buf[i]=DHT11_Read_Byte();
		}
		if((buf[0]+buf[1]+buf[2]+buf[3])==buf[4])	//校验和正确
		{
			*humi=buf[0];	//湿度整数
			*temp=buf[2];	//温度整数
		}
		
	}else return 1;
	return 0;	    
}

//DHT11 IO口切换为输出模式
void DHT11_IO_OUT()	
{
	GPIO_InitTypeDef GPIO_InitStructure;
	GPIO_InitStructure.GPIO_Pin=DHT11;
	GPIO_InitStructure.GPIO_Mode=GPIO_Mode_Out_PP;	 //推挽输出模式
	GPIO_InitStructure.GPIO_Speed=GPIO_Speed_50MHz;
	GPIO_Init(GPIO_DHT11,&GPIO_InitStructure);	
}

//DHT11 IO口切换为输入模式（上拉）
void DHT11_IO_IN()	
{
	GPIO_InitTypeDef GPIO_InitStructure;
	GPIO_InitStructure.GPIO_Pin=DHT11;
	GPIO_InitStructure.GPIO_Mode=GPIO_Mode_IPU;	 //上拉输入模式
	GPIO_Init(GPIO_DHT11,&GPIO_InitStructure);	
}
