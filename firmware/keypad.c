/*
 *                  Copyright 2014 ARTaylor.co.uk
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * Author: Richard Taylor (richard@artaylor.co.uk)
 */

#include "stm32f10x.h"
#include "keypad.h"
#include "tasks.h"
#include "gui.h"

#define ROW_MASK       (0x07 << 12)
#define COL_MASK       (0x0F << 8)
#define ROW(n)         (1 << (12 + n))
#define COL(n)         (1 << (8 + n))

// Keys that have been pressed since the last check.
static uint32_t keys_pressed = 0;

/**
  * @brief  Scan the keypad and return the active key.
  * @note   Will only return the first key found if multiple keys pressed.
  * @param  None
  * @retval KEYPAD_KEY
  *   Returns the active key
  *     @arg KEY_xxx: The key that was pressed
  *     @arg KEY_NONE: No key was pressed
  */
static KEYPAD_KEY keypad_scan_keys(void)
{
	KEYPAD_KEY key = KEY_NONE;
	bool found = FALSE;
	uint16_t rows;
	uint8_t col;
	
	for (col = 0; col < 4; ++col)
	{
		// Walk a '0' down the cols.
		GPIO_SetBits(GPIOB, COL_MASK);
		GPIO_ResetBits(GPIOB, COL(col));
		
		// Allow some time for the GPIO to settle.
		delay_us(100);
		
		// The rows are pulled high externally.
		// Any '0' seen here is due to a switch connecting to our active '0' on a column.
		rows = GPIO_ReadInputData(GPIOB);
		if ((rows & ROW_MASK) != ROW_MASK)
		{
			// Only support one key pressed at a time.
			found = TRUE;
			break;
		}
	}

	// Set the cols to all '0'.
	GPIO_ResetBits(GPIOB, COL_MASK);

	if (found)
	{
		rows = ~rows & ROW_MASK;

		switch (col)
		{
			case 0:
				if ((rows & ROW(0)) != 0)
					key = KEY_CH1_UP;
				else if ((rows & ROW(1)) != 0)
					key = KEY_CH3_UP;
			break;
			
			case 1:
				if ((rows & ROW(0)) != 0)
					key = KEY_CH1_DN;
				else if ((rows & ROW(1)) != 0)
					key = KEY_CH3_DN;
				else if ((rows & ROW(2)) != 0)
					key = KEY_SEL;
			break;
			
			case 2:
				if ((rows & ROW(0)) != 0)
					key = KEY_CH2_UP;
				else if ((rows & ROW(1)) != 0)
					key = KEY_CH4_UP;
				else if ((rows & ROW(2)) != 0)
					key = KEY_OK;
			break;

			case 3:
				if ((rows & ROW(0)) != 0)
					key = KEY_CH2_DN;
				else if ((rows & ROW(1)) != 0)
					key = KEY_CH4_DN;
				else if ((rows & ROW(2)) != 0)
					key = KEY_CANCEL;
			break;

			default:
			break;
		}
	}

	return key;
}

/**
  * @brief  Process keys and drive updates through the system.
  * @note   Called from the scheduler.
  * @param  data: EXTI lines that triggered the update.
  * @retval None
  */
static void keypad_process(uint32_t data)
{
	KEYPAD_KEY key = keypad_scan_keys();
	
	// Data is used to send the rotary encoder data.
	if (data != 0)
	{
		switch (data)
		{
			case 1:
				key = KEY_RIGHT;
			break;
			case 2:
				key = KEY_LEFT;
			break;
			default:
			break;
		}
	}
	
	if (key != 0)
	{
		keys_pressed |= key;
		gui_input_key(key);
	}
}

/**
  * @brief  Initialise the keypad scanning pins.
  * @note   Row used as output, Col as input.
  * @param  None
  * @retval None
  */
void keypad_init(void)
{
	GPIO_InitTypeDef gpioInit;
	EXTI_InitTypeDef extiInit;
	NVIC_InitTypeDef nvicInit;

	// Enable the GPIO block clocks and setup the pins.
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB | RCC_APB2Periph_GPIOC | RCC_APB2Periph_AFIO, ENABLE);

	gpioInit.GPIO_Speed = GPIO_Speed_2MHz;

	// Configure the Column pins
	gpioInit.GPIO_Pin = COL_MASK;
	gpioInit.GPIO_Mode = GPIO_Mode_Out_OD;
	GPIO_ResetBits(GPIOB, COL_MASK);
	GPIO_Init(GPIOB, &gpioInit);

	// Configure the Row pins and SWA, SWB, SWC.
	gpioInit.GPIO_Pin = ROW_MASK | GPIO_Pin_0 | GPIO_Pin_1 | GPIO_Pin_5;
	gpioInit.GPIO_Mode = GPIO_Mode_IPU;
	GPIO_Init(GPIOB, &gpioInit);

	// Configure SWD.
	gpioInit.GPIO_Pin = GPIO_Pin_13;
	gpioInit.GPIO_Mode = GPIO_Mode_IPU;
	GPIO_Init(GPIOC, &gpioInit);

	// Set the cols as Ext. Interrupt sources.
    GPIO_EXTILineConfig(GPIO_PortSourceGPIOB, 12);
    GPIO_EXTILineConfig(GPIO_PortSourceGPIOB, 13);
    GPIO_EXTILineConfig(GPIO_PortSourceGPIOB, 14);


	gpioInit.GPIO_Pin = 1 << 15;
	gpioInit.GPIO_Mode = GPIO_Mode_IN_FLOATING;
	GPIO_Init(GPIOC, &gpioInit);

	// Set the rotary encoder as Ext. Interrupt source.
    GPIO_EXTILineConfig(GPIO_PortSourceGPIOC, 15);

	// Configure keypad lines as falling edge IRQs
    extiInit.EXTI_Mode = EXTI_Mode_Interrupt;
    extiInit.EXTI_Trigger = EXTI_Trigger_Falling;
    extiInit.EXTI_LineCmd = ENABLE;
    extiInit.EXTI_Line = KEYPAD_EXTI_LINES;
    EXTI_Init(&extiInit);

	// Configure rotary line as rising + falling edge IRQ
    extiInit.EXTI_Trigger = EXTI_Trigger_Rising_Falling;
    extiInit.EXTI_Line = ROTARY_EXTI_LINES;
    EXTI_Init(&extiInit);

    // Configure the Interrupt to the lowest priority
    nvicInit.NVIC_IRQChannelPreemptionPriority = 0x0F;
    nvicInit.NVIC_IRQChannelSubPriority = 0x0F;
    nvicInit.NVIC_IRQChannelCmd = ENABLE;
    nvicInit.NVIC_IRQChannel = EXTI15_10_IRQn;
    NVIC_Init(&nvicInit);

	task_register(TASK_PROCESS_KEYPAD, keypad_process);
}

/**
  * @brief  Poll to see if a specific key has been pressed
  * @note
  * @param  key: Key to check.
  * @retval bool: TRUE if pressed, FALSE if not.
  */
bool keypad_get_pressed(KEYPAD_KEY key)
{
	if ((keys_pressed & key) != 0)
	{
		keys_pressed &= ~key;
		return TRUE;
	}
	return FALSE;
}

/**
  * @brief  Scan the switches' state
  * @note
  * @param  None
  * @retval uint8_t: Bitmask of the switches
  */
uint8_t keypad_get_switches(void)
{
	uint8_t switches = 0;
	if (GPIO_ReadInputDataBit(GPIOB, GPIO_Pin_0))
		switches |= SWITCH_SWA;

	if (GPIO_ReadInputDataBit(GPIOB, GPIO_Pin_1))
		switches |= SWITCH_SWB;

	if (GPIO_ReadInputDataBit(GPIOB, GPIO_Pin_5))
		switches |= SWITCH_SWC;

	if (GPIO_ReadInputDataBit(GPIOC, GPIO_Pin_13))
		switches |= SWITCH_SWD;

	return switches;
}
