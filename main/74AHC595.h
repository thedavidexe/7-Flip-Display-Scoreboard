/*
 * 74AHC595.h
 *
 *  Created on: 6 mar 2025
 *      Author: Sebastian Sokołowski
 *		Company: Smart Solutions for Home
 *
 * SPDX-License-Identifier: MIT
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is furnished
 * to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * This file is part of an open source project.
 * For more information, visit: https://smartsolutions4home.com/7-flip-display
 */

#ifndef MAIN_74AHC595_H_
#define MAIN_74AHC595_H_

#include "esp_err.h"
#include "hal/gpio_types.h"
#include <stdint.h>

#define DISP "DISP"

// Pin configuration for ESP32-C3-WROOM-02-H4
#define SHIFT_REG_DATA_PIN   	GPIO_NUM_7     // SR_DATA_IN (IO7)
#define SHIFT_REG_LATCH_PIN  	GPIO_NUM_19    // SR_LATCH   (IO19)
#define SHIFT_REG_CLOCK_PIN  	GPIO_NUM_10    // SR_CLK     (IO10)

#define DETECT_PIN 				GPIO_NUM_1     // SERIAL_OUT (IO1)
#define POWER_PIN				GPIO_NUM_3     // POWER      (IO3)
#define FEEDBACK_PIN         GPIO_NUM_18    // FEEDBACK   (IO18)

// Queue and task parameters
#define SR_QUEUE_LENGTH    10
#define SR_TASK_STACK_SIZE 2048
#define SR_TASK_PRIORITY   5

// Function prototype
void DetectDisplays(void);
esp_err_t shift_register_init(void);
void shift_register_send_word(uint16_t data);
void DisplayNumber(uint32_t number, uint8_t group);
esp_err_t gpio_init(gpio_int_type_t type, gpio_mode_t mode, gpio_pulldown_t pull_down, gpio_pullup_t pull_up, int no, uint8_t initial_state);
void DisplaySymbol(uint8_t pattern_raw, uint8_t target);
uint8_t detect_display_count(void);
void Send2Register(uint16_t data);
void DisplayDigit(uint8_t digit, uint8_t target);
void GenerateAlarm(uint8_t group);
void DemoMode(uint8_t mode);

#endif /* MAIN_74AHC595_H_ */

