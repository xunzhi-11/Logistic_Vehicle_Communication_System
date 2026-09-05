#include "stm32f4xx.h"                  // Device header
#include "stm32f4xx_ll_gpio.h"
#include "stm32f4xx_ll_bus.h"
#include "OLED_Font.h"
#include "oled.h"

void GPIO_WriteBit(GPIO_TypeDef *GPIOx, uint32_t PinMask, uint8_t value)
{
    if (value)
        LL_GPIO_SetOutputPin(GPIOx, PinMask);
    else
        LL_GPIO_ResetOutputPin(GPIOx, PinMask);
}

/*引脚配置*/
#define OLED_W_SCL(x)		GPIO_WriteBit(GPIOD, LL_GPIO_PIN_9, (uint8_t)(x))
#define OLED_W_SDA(x)		GPIO_WriteBit(GPIOD, LL_GPIO_PIN_10, (uint8_t)(x))

static uint8_t OLED_Buffer[8][128]; // 显存缓冲区，8页，每页128列
static uint8_t current_page = 0;    // 当前页
static uint8_t current_col = 0;     // 当前列
/*引脚初始化*/
void OLED_I2C_Init(void)
{
    LL_AHB1_GRP1_EnableClock(LL_AHB1_GRP1_PERIPH_GPIOD);
	LL_GPIO_InitTypeDef GPIO_InitStructure;
 	GPIO_InitStructure.Mode = LL_GPIO_MODE_OUTPUT;
    GPIO_InitStructure.OutputType = LL_GPIO_OUTPUT_OPENDRAIN;
    GPIO_InitStructure.Pull = LL_GPIO_PULL_UP;

	GPIO_InitStructure.Speed = LL_GPIO_SPEED_FREQ_VERY_HIGH;
	GPIO_InitStructure.Pin = LL_GPIO_PIN_10;
 	LL_GPIO_Init(GPIOD, &GPIO_InitStructure);
	GPIO_InitStructure.Pin = LL_GPIO_PIN_9;
 	LL_GPIO_Init(GPIOD, &GPIO_InitStructure);
	
	OLED_W_SCL(1);
	OLED_W_SDA(1);
}

/**
  * @brief  I2C开始
  * @param  无
  * @retval 无
  */
void OLED_I2C_Start(void)
{
	OLED_W_SDA(1);
	OLED_W_SCL(1);
	OLED_W_SDA(0);
	OLED_W_SCL(0);
}

/**
  * @brief  I2C停止
  * @param  无
  * @retval 无
  */
void OLED_I2C_Stop(void)
{
	OLED_W_SDA(0);
	OLED_W_SCL(1);
	OLED_W_SDA(1);
}

/**
  * @brief  I2C发送一个字节
  * @param  Byte 要发送的一个字节
  * @retval 无
  */
void OLED_I2C_SendByte(uint8_t Byte)
{
	uint8_t i;
	for (i = 0; i < 8; i++)
	{
		OLED_W_SDA(Byte & (0x80 >> i));
		OLED_W_SCL(1);
		OLED_W_SCL(0);
	}
	OLED_W_SCL(1);	//额外的一个时钟，不处理应答信号
	OLED_W_SCL(0);
}

/**
  * @brief  OLED写命令
  * @param  Command 要写入的命令
  * @retval 无
  */
void OLED_WriteCommand(uint8_t Command)
{
	OLED_I2C_Start();
	OLED_I2C_SendByte(0x78);		//从机地址
	OLED_I2C_SendByte(0x00);		//写命令
	OLED_I2C_SendByte(Command); 
	OLED_I2C_Stop();
}

/**
  * @brief  OLED写数据，并更新缓冲区及当前列
  * @param  Data 要写入的数据
  * @retval 无
  */
void OLED_WriteData(uint8_t Data)
{
    OLED_I2C_Start();
    OLED_I2C_SendByte(0x78); // 从机地址
    OLED_I2C_SendByte(0x40); // 写数据
    OLED_I2C_SendByte(Data);
    OLED_I2C_Stop();
    
    // 更新缓冲区并递增列
    OLED_Buffer[current_page][current_col] = Data;
    current_col++;
    if (current_col >= 128) {
        current_col = 0; // 列地址回绕
    }
}

/**
  * @brief  OLED设置光标位置，并更新当前页和列
  * @param  Y 页地址，0~7
  * @param  X 列地址，0~127
  * @retval 无
  */
void OLED_SetCursor(uint8_t Y, uint8_t X)
{
    current_page = Y;
    current_col = X;
    OLED_WriteCommand(0xB0 | Y);                // 设置页地址
    OLED_WriteCommand(0x10 | ((X & 0xF0) >> 4)); // 设置列地址高4位
    OLED_WriteCommand(0x00 | (X & 0x0F));         // 设置列地址低4位
}

/**
  * @brief  OLED清屏，并清空缓冲区
  * @param  无
  * @retval 无
  */
void OLED_Clear(void)
{  
    uint8_t i, j;
    for (j = 0; j < 8; j++) {
        OLED_SetCursor(j, 0);
        for (i = 0; i < 128; i++) {
            OLED_WriteData(0x00); // 清空屏幕并更新缓冲区
        }
    }
}

/**
  * @brief  OLED显示一个字符
  * @param  Line 行位置，范围：1~4
  * @param  Column 列位置，范围：1~16
  * @param  Char 要显示的一个字符，范围：ASCII可见字符
  * @retval 无
  */
void OLED_ShowChar(uint8_t Line, uint8_t Column, char Char)
{      	
	uint8_t i;
	OLED_SetCursor((Line - 1) * 2, (Column - 1) * 8);		//设置光标位置在上半部分
	for (i = 0; i < 8; i++)
	{
		OLED_WriteData(OLED_F8x16[Char - ' '][i]);			//显示上半部分内容
	}
	OLED_SetCursor((Line - 1) * 2 + 1, (Column - 1) * 8);	//设置光标位置在下半部分
	for (i = 0; i < 8; i++)
	{
		OLED_WriteData(OLED_F8x16[Char - ' '][i + 8]);		//显示下半部分内容
	}
}

/**
  * @brief  OLED显示字符串
  * @param  Line 起始行位置，范围：1~4
  * @param  Column 起始列位置，范围：1~16
  * @param  String 要显示的字符串，范围：ASCII可见字符
  * @retval 无
  */
void OLED_ShowString(uint8_t Line, uint8_t Column, char *String)
{
	uint8_t i;
	for (i = 0; String[i] != '\0'; i++)
	{
		OLED_ShowChar(Line, Column + i, String[i]);
	}
}

/**
  * @brief  OLED次方函数
  * @retval 返回值等于X的Y次方
  */
uint32_t OLED_Pow(uint32_t X, uint32_t Y)
{
	uint32_t Result = 1;
	while (Y--)
	{
		Result *= X;
	}
	return Result;
}

/**
  * @brief  OLED显示数字（十进制，正数）
  * @param  Line 起始行位置，范围：1~4
  * @param  Column 起始列位置，范围：1~16
  * @param  Number 要显示的数字，范围：0~4294967295
  * @param  Length 要显示数字的长度，范围：1~10
  * @retval 无
  */
void OLED_ShowNum(uint8_t Line, uint8_t Column, uint32_t Number, uint8_t Length)
{
	uint8_t i;
	for (i = 0; i < Length; i++)							
	{
		OLED_ShowChar(Line, Column + i, Number / OLED_Pow(10, Length - i - 1) % 10 + '0');
	}
}

/**
  * @brief  OLED显示数字（十进制，带符号数）
  * @param  Line 起始行位置，范围：1~4
  * @param  Column 起始列位置，范围：1~16
  * @param  Number 要显示的数字，范围：-2147483648~2147483647
  * @param  Length 要显示数字的长度，范围：1~10
  * @retval 无
  */
void OLED_ShowSignedNum(uint8_t Line, uint8_t Column, int32_t Number, uint8_t Length)
{
	uint8_t i;
	uint32_t Number1;
	if (Number >= 0)
	{
		OLED_ShowChar(Line, Column, '+');
		Number1 = Number;
	}
	else
	{
		OLED_ShowChar(Line, Column, '-');
		Number1 = -Number;
	}
	for (i = 0; i < Length; i++)							
	{
		OLED_ShowChar(Line, Column + i + 1, Number1 / OLED_Pow(10, Length - i - 1) % 10 + '0');
	}
}

/**
  * @brief  OLED显示数字（十六进制，正数）
  * @param  Line 起始行位置，范围：1~4
  * @param  Column 起始列位置，范围：1~16
  * @param  Number 要显示的数字，范围：0~0xFFFFFFFF
  * @param  Length 要显示数字的长度，范围：1~8
  * @retval 无
  */
void OLED_ShowHexNum(uint8_t Line, uint8_t Column, uint32_t Number, uint8_t Length)
{
	uint8_t i, SingleNumber;
	for (i = 0; i < Length; i++)							
	{
		SingleNumber = Number / OLED_Pow(16, Length - i - 1) % 16;
		if (SingleNumber < 10)
		{
			OLED_ShowChar(Line, Column + i, SingleNumber + '0');
		}
		else
		{
			OLED_ShowChar(Line, Column + i, SingleNumber - 10 + 'A');
		}
	}
}

/**
  * @brief  OLED显示数字（二进制，正数）
  * @param  Line 起始行位置，范围：1~4
  * @param  Column 起始列位置，范围：1~16
  * @param  Number 要显示的数字，范围：0~1111 1111 1111 1111
  * @param  Length 要显示数字的长度，范围：1~16
  * @retval 无
  */
void OLED_ShowBinNum(uint8_t Line, uint8_t Column, uint32_t Number, uint8_t Length)
{
	uint8_t i;
	for (i = 0; i < Length; i++)							
	{
		OLED_ShowChar(Line, Column + i, Number / OLED_Pow(2, Length - i - 1) % 2 + '0');
	}
}

/**
  * @brief  OLED初始化
  * @param  无
  * @retval 无
  */
void OLED_Init(void)
{
	uint32_t i, j;
	
	for (i = 0; i < 1000; i++)			//上电延时
	{
		for (j = 0; j < 1000; j++);
	}
	
	OLED_I2C_Init();			//端口初始化
	
	OLED_WriteCommand(0xAE);	//关闭显示
	
	OLED_WriteCommand(0xD5);	//设置显示时钟分频比/振荡器频率
	OLED_WriteCommand(0x80);
	
	OLED_WriteCommand(0xA8);	//设置多路复用率
	OLED_WriteCommand(0x3F);
	
	OLED_WriteCommand(0xD3);	//设置显示偏移
	OLED_WriteCommand(0x00);
	
	OLED_WriteCommand(0x40);	//设置显示开始行
	
	OLED_WriteCommand(0xA1);	//设置左右方向，0xA1正常 0xA0左右反置
	
	OLED_WriteCommand(0xC8);	//设置上下方向，0xC8正常 0xC0上下反置

	OLED_WriteCommand(0xDA);	//设置COM引脚硬件配置
	OLED_WriteCommand(0x12);
	
	OLED_WriteCommand(0x81);	//设置对比度控制
	OLED_WriteCommand(0xCF);

	OLED_WriteCommand(0xD9);	//设置预充电周期
	OLED_WriteCommand(0xF1);

	OLED_WriteCommand(0xDB);	//设置VCOMH取消选择级别
	OLED_WriteCommand(0x30);

	OLED_WriteCommand(0xA4);	//设置整个显示打开/关闭

	OLED_WriteCommand(0xA6);	//设置正常/倒转显示

	OLED_WriteCommand(0x8D);	//设置充电泵
	OLED_WriteCommand(0x14);

	OLED_WriteCommand(0xAF);	//开启显示
		
	OLED_Clear();				//OLED清屏
}

/**
 * @brief       OLED显示汉字（支持正反显示）
 * @param       Line   : 行位置，1~4（每行对应16像素高度）
 * @param       Column : 列位置，1~8（每列对应16像素宽度）
 * @param       Num    : 字库中的汉字索引（0,1,...）
 * @param       mode   : 显示模式，1-正常显示，0-反向显示
 * @retval      无
 */
void OLED_ShowCN(uint8_t Line, uint8_t Column, uint8_t Num, uint8_t mode)  
{      
    uint8_t i;
    uint8_t page_start = (Line - 1) * 2;   // 计算起始页（汉字占2页）
    uint8_t col_start = (Column - 1) * 16; // 计算起始列（汉字占16列）

    // 显示上半部分（前16字节）
    OLED_SetCursor(page_start, col_start);
    for (i = 0; i < 16; i++) 
    {
        uint8_t data = OLED_F10x16[Num][i];
        OLED_WriteData(mode ? data : ~data); // 根据mode取反
    }

    // 显示下半部分（后16字节）
    OLED_SetCursor(page_start + 1, col_start);
    for (i = 16; i < 32; i++) 
    {
        uint8_t data = OLED_F10x16[Num][i];
        OLED_WriteData(mode ? data : ~data); // 根据mode取反
    }
}

/**
  * @brief  OLED画点函数
  * @param  x 横坐标，0~127
  * @param  y 纵坐标，0~63
  * @param  mode: 1 点亮像素，0 熄灭像素
  * @retval 无
  */
void OLED_DrawPoint(uint8_t x, uint8_t y, uint8_t mode)
{
    if (x >= 128 || y >= 64) return; // 坐标越界处理
    
    uint8_t page = y / 8;      // 计算页地址
    uint8_t bit_pos = y % 8;   // 计算页内位位置
    
    // 修改缓冲区对应位
    if (mode) {
        OLED_Buffer[page][x] |= (1 << bit_pos);  // 点亮像素
    } else {
        OLED_Buffer[page][x] &= ~(1 << bit_pos); // 熄灭像素
    }
    
    // 更新到OLED屏幕
    OLED_SetCursor(page, x);
    OLED_WriteData(OLED_Buffer[page][x]);
}

/**
  * @brief  OLED显示位图（支持任意位置和尺寸）
  * @param  x     起始列 (0~127)
  * @param  y     起始页 (0~7)
  * @param  width 图像宽度（像素，必须为8的倍数）
  * @param  height 图像高度（像素，必须为8的倍数）
  * @param  bmp   位图数据数组，大小为 (width/8)*height 字节
  * @retval 无
  */
void OLED_ShowBMP(uint8_t x, uint8_t y, uint8_t width, uint8_t height, const uint8_t *bmp, uint8_t mode) 
{
    uint8_t page_end = y + height / 8;
    uint8_t col_end = x + width;
    uint16_t index = 0;
    
    for (uint8_t page = y; page < page_end; page++) 
    {
        OLED_SetCursor(page, x);
        for (uint8_t col = x; col < col_end; col++) 
        {
            uint8_t data = bmp[index++];
            OLED_WriteData(mode ? data : ~data); // 根据mode取反
        }
    }
}
