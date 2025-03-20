/*
 * main.h
 *
 *  Created on: 5 mar 2025
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

#ifndef MAIN_MAIN_H_
#define MAIN_MAIN_H_

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

//#include "rtc.h"
#include "status_led.h"
#include "74AHC595.h"

/* Macros for setting a key in a variable */
#define SET_KEY(state, key)    ((state) |= (key))      		/* Set bit to 1 */
#define CLEAR_KEY(state, key)  ((state) &= ~(key))    		/* Set bit to 0 */
#define CHECK_KEY(state, key)  (((state) & (key)) != 0)		/* Is bit set?	*/

enum  pp_alert_t{
	NONE = 0x0000,
	HARDWARE_PROBLEM = 0x0001,
	UPDATE  = 0xFFFF,
};

typedef struct {
	uint8_t second;
	uint8_t minute;
	uint8_t hour;
	uint8_t am_pm;
	uint8_t week_day;
	uint8_t day;
	uint8_t month;
	uint8_t year;
} rtc_t;

typedef struct {
	uint8_t display_number;
	rtc_t rtc;
	enum pp_alert_t alert;
} status_t;

#endif /* MAIN_MAIN_H_ */
