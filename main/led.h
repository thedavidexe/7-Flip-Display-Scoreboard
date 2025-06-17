/*
 * led.h
 *
 *  Created on: 22 maj 2025
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

#ifndef MAIN_LED_H_
#define MAIN_LED_H_


#include <stdint.h>


#define LEDC_CHANNEL_R       0
#define LEDC_CHANNEL_G       1
#define LEDC_CHANNEL_B       2

// Zamień na swoje GPIO:
#define LED_GPIO_RED         GPIO_NUM_14
#define LED_GPIO_GREEN       GPIO_NUM_13
#define LED_GPIO_BLUE        GPIO_NUM_27

#define LEDC_MODE            LEDC_LOW_SPEED_MODE
#define LEDC_TIMER           LEDC_TIMER_0
#define LEDC_DUTY_RES        LEDC_TIMER_8_BIT  // 8-bit resolution
#define LEDC_FREQUENCY       5000              // 5 kHz

//Colors
#define RED       255,  0,   0
#define GREEN       0,255,   0
#define BLUE        0,  0, 255
#define YELLOW    255,255,   0
#define CYAN         0,255, 255
#define MAGENTA   255,  0, 255
#define WHITE     255,255,255
#define ORANGE    255,165,   0
#define PURPLE    128,  0, 128

void vLED_HandleTask(void *arg);
void LED_set_color(uint8_t r, uint8_t g, uint8_t b, uint8_t brightness);

#endif /* MAIN_LED_H_ */
