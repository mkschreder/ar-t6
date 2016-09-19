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

/* Description:
 *
 * This is an IRQ driven keypad driver.
 * Pressed keys are stored and scheduled for processing by the main loop.
 * This will then call into the mixer (for trim) and GUI.
 * GUI events are asynchronous, and will be processed on the next main loop cycle.
 *
 */

#include <stdbool.h>
#include <stm32f10x.h>
#include <stm32f10x_exti.h>
#include <stm32f10x_gpio.h>
#include <stm32f10x_misc.h>
#include <stm32f10x_rcc.h>

#include "keypad.h"
#include "mixer.h"
#include "tasks.h"
#include "gui.h"
#include "sound.h"
#include "myeeprom.h"

#define ROW_MASK       (0x07 << 12)
#define COL_MASK       (0x0F << 8)
#define ROW(n)         (1 << (12 + n))
#define COL(n)         (1 << (8 + n))

#define KEY_HOLDOFF			10
#define KEY_REPEAT_DELAY	500
#define KEY_REPEAT_TIME		100

// Keys that have been pressed since the last check.
static uint32_t keys_pressed = 0;
static uint32_t key_repeat = 0;
static uint32_t key_time = 0;

static void keypad_process(uint32_t data);

/**
 * @brief  Initialise the keypad scanning pins.
 * @note   Row used as output, Col as input.
 * @param  None
 * @retval None
 */
void keypad_init(void) {
	GPIO_InitTypeDef gpioInit;
	EXTI_InitTypeDef extiInit;
	NVIC_InitTypeDef nvicInit;

	// Enable the GPIO block clocks and setup the pins.
	RCC_APB2PeriphClockCmd(
			RCC_APB2Periph_GPIOB | RCC_APB2Periph_GPIOC | RCC_APB2Periph_AFIO,
			ENABLE);

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
 * @retval bool: true if pressed, false if not.
 */
bool keypad_get_pressed(KEYPAD_KEY key) {
	if ((keys_pressed & key) != 0) {
		keys_pressed &= ~key;
		return true;
	}
	return false;
}

/**
 * @brief  Scan the switches' state
 * @note
 * @param  None
 * @retval uint8_t: Bitmask of the switches
 */
uint8_t keypad_get_switches(void) {
	uint8_t switches = 0;
	if (!GPIO_ReadInputDataBit(GPIOB, GPIO_Pin_0))
		switches |= SWITCH_SWA;

	if (!GPIO_ReadInputDataBit(GPIOB, GPIO_Pin_1))
		switches |= SWITCH_SWB;

	if (!GPIO_ReadInputDataBit(GPIOB, GPIO_Pin_5))
		switches |= SWITCH_SWC;

	if (!GPIO_ReadInputDataBit(GPIOC, GPIO_Pin_13))
		switches |= SWITCH_SWD;

	return switches;
}

/**
 * @brief  Check a specific switch
 * @note  sw==0 always on!
 * @param  sw: The Switch to check. sw==0 always on
 * @retval bool: true if on, false if off.
 */
bool keypad_get_switch(KEYPAD_SWITCH sw) {
	return sw == 0 || (keypad_get_switches() & sw);
}

/**
 * @brief  Abort the key repeat loop
 * @note
 * @param  None
 * @retval None
 */
void keypad_cancel_repeat(void) {
	key_repeat = 0;
	key_time = 0;
	task_deschedule(TASK_PROCESS_KEYPAD);
}

/**
 * @brief  Process keys and drive the GUI.
 * @note   Called from the scheduler.
 * @param  data: EXTI lines that triggered the update.
 * @retval None
 */
static void keypad_process(uint32_t data) {
	KEYPAD_KEY key;

	// Debouncing.
	if (key_time != 0 && key_time + KEY_HOLDOFF > system_ticks) {
		task_schedule(TASK_PROCESS_KEYPAD, 0, KEY_HOLDOFF);
		return;
	}

	// Scan the keys.
	key = keypad_scan_keys();
	// Scanning the keys causes the IRQ to fire, so de-schedule for now.
	task_deschedule(TASK_PROCESS_KEYPAD);

	// Cancel the repeat if we see a different key.
	if (key == KEY_NONE) {
		key_repeat = 0;
		key_time = 0;
	}

	// Data is used to send the rotary encoder data.
	if (data != 0) {
		switch (data) {
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

	if (key != 0) {
		// Re-schedule to check the keys again.
		task_schedule(TASK_PROCESS_KEYPAD, 0, KEY_REPEAT_TIME);

		if (key_time != 0) {
			// A key has been pressed previously, check for repeat.

			if ((key_repeat == 0)
					&& key_time + KEY_REPEAT_DELAY > system_ticks) {
				// Not repeating yet, and haven't passed the delay period.
				return;
			} else {
				// Delay period passed, decide how to behave...
				if (key & KEY_SEL) {
					// After repeat delay, send only one KEY_MENU press from KEY_SEL.
					if (key_repeat == 0)
						key = KEY_MENU;
					else
						return;
				} else if (!(key & TRIM_KEYS)) {
					// For non-trim keys, don't repeat.
					return;
				}

				// For trim keys, repeat at KEY_REPEAT_TIME intervals.
				key_repeat = key;
			}
		}

		// Add the key to the pressed list.
		keys_pressed |= key;

		// Record the key time.
		key_time = system_ticks;

		// Play the key tone.
		if (g_eeGeneral.beeperVal > BEEPER_NOKEY)
			sound_play_tone(500, 10);

		// Send the key to the UI.
		gui_input_key(key);
	}
}

/**
 * @brief  Scan the keypad and return the active key.
 * @note   Will only return the first key found if multiple keys pressed.
 * @param  None
 * @retval KEYPAD_KEY
 *   Returns the active key
 *     @arg KEY_xxx: The key that was pressed
 *     @arg KEY_NONE: No key was pressed
 */
KEYPAD_KEY keypad_scan_keys(void) {
	KEYPAD_KEY key = KEY_NONE;
	bool found = false;
	uint16_t rows;
	uint8_t col;

	for (col = 0; col < 4; ++col) {
		// Walk a '0' down the cols.
		GPIO_SetBits(GPIOB, COL_MASK);
		GPIO_ResetBits(GPIOB, COL(col));

		// Allow some time for the GPIO to settle.
		delay_us(100);

		// The rows are pulled high externally.
		// Any '0' seen here is due to a switch connecting to our active '0' on a column.
		rows = GPIO_ReadInputData(GPIOB);
		if ((rows & ROW_MASK) != ROW_MASK) {
			// Only support one key pressed at a time.
			found = true;
			break;
		}
	}

	// Set the cols to all '0'.
	GPIO_ResetBits(GPIOB, COL_MASK);

	if (found) {
		rows = ~rows & ROW_MASK;

		switch (col) {
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
 * @brief  ExtI IRQ handler
 * @note   Handles IRQ on GPIO lines 10-15.
 * @param  None
 * @retval None
 */
void EXTI15_10_IRQHandler(void) {
	uint32_t flags = EXTI->PR;

	if ((flags & KEYPAD_EXTI_LINES) != 0) {
		// Clear the IRQ
		EXTI->PR = KEYPAD_EXTI_LINES;

		// Schedule the keys to be scanned.
		task_schedule(TASK_PROCESS_KEYPAD, 0, 0);
	}

	if ((flags & ROTARY_EXTI_LINES) != 0) {
		// Clear the IRQ
		EXTI->PR = ROTARY_EXTI_LINES;

		// Read the encoder lines
		uint16_t gpio = GPIO_ReadInputData(GPIOC);

		if ((gpio & (1 << 15)) == 0) {
			// Falling edge
			if ((gpio & (1 << 14)) == 0)
				task_schedule(TASK_PROCESS_KEYPAD, 1, 0);
			else
				task_schedule(TASK_PROCESS_KEYPAD, 2, 0);
		} else {
			// Rising edge
			if ((gpio & (1 << 14)) == 0)
				task_schedule(TASK_PROCESS_KEYPAD, 2, 0);
			else
				task_schedule(TASK_PROCESS_KEYPAD, 1, 0);
		}
	}
}
