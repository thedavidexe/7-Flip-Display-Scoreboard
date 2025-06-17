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
#include "esp_rest_main.h"

//#include "rtc.h"
#include "sdkconfig.h"
#include "status_led.h"
#include "74AHC595.h"
#include "rtc.h"
#include "mqtt_com.h"
#include "led.h"


/* Macros for setting a key in a variable */
#define SET_KEY(state, key)    ((state) |= (key))      		/* Set bit to 1 */
#define CLEAR_KEY(state, key)  ((state) &= ~(key))    		/* Set bit to 0 */
#define CHECK_KEY(state, key)  (((state) & (key)) != 0)		/* Is bit set?	*/


#define FIRM 				"FIRMWARE"
#define MAX_GROUPS 			15
#define MAX_DISPLAYS		15

/* General Settings */
enum pp_separator_t {
	SEP_NULL,
	SEP_SPACE,
	SEP_BLANK,
	SEP_COLON,
	SEP_DOT,
	SEP_DASH
};

enum pp_mode_t {
	MODE_NONE,
	MODE_MQTT,
	MODE_TIMER,
	MODE_CLOCK,
	MODE_MANUAL,
	MODE_CUSTOM_API
};

enum PP_display_symbol_mode {
	SINGLE_SEGMENT,
	SINGLE_MODUL,
	ALL_DISPLAY
};

/* Timer Settings */
enum pp_timer_type_t {
	TIMER_NONE,
	TIMER_SIMPLE,
	TIMER_ADVANCED
};

enum pp_timer_interval_unit_t {
	INTERVAL_SECONDS = 1,
	INTERVAL_MINUTES = 60,
	INTERVAL_HOURS = 3600,
	INTERVAL_DAYS = 86400
};

enum pp_timer_dir_t {
	COUNT_OFF,
	COUNT_UP,
	COUNT_DOWN
};

typedef struct {
	enum pp_timer_type_t type;
	enum pp_timer_interval_unit_t interval_unit;
	bool alarm;
	bool show_curr_cycle;
	uint16_t count_from;
	uint16_t count_to;
	uint16_t work_time;
	uint16_t  rest_time;
	uint16_t interval;
	uint8_t cycles;
	int value;
	enum pp_timer_dir_t direction;
} timer_settings_t;


/* Clock Settings */
enum pp_clock_type_t {
	RTC_NONE,
	RTC_SECONDS,
	RTC_MINUTES,
	RTC_HOURS,
	RTC_DAY,
	RTC_MONTCH,
	RTC_YEAR
};

enum pp_time_format_t {
	FORMAT_24H,
	FORMAT_12H
};

typedef struct {
	enum pp_clock_type_t type;
	enum pp_time_format_t time_format;
	int time_offset;
	bool time_tormat;
}clock_settings_t;


/* MQTT Settings */
typedef struct {
	char topic[10]; 
} mqtt_settings_t;


/* CUSTOM-API Settings */
enum pp_rest_method_t {
	POST,
	GET
};

enum pp_response_format_t {
	JSON,
	XML,
	TEXT
};

typedef struct {
	char url[150];
	char key_patch[50];
	char headers[150];
	enum pp_rest_method_t method;
	enum pp_response_format_t format;
	uint8_t pulling_interval;
} api_settings_t;

typedef struct {
    int start_position;     
    int end_position;           
    int pattern[MAX_DISPLAYS];           
    enum pp_separator_t separator;  
    enum pp_mode_t mode;               
    mqtt_settings_t mqtt;
    timer_settings_t timer;
    api_settings_t api;
    clock_settings_t clock;
} display_group_t;

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

enum  pp_alert_t {
	NONE = 0x0000,
	HARDWARE_PROBLEM = 0x0001,
	UPDATE  = 0xFFFF,
};

typedef struct {
	uint8_t display_number;
	rtc_t rtc;
	enum pp_alert_t alert;
	bool led;
	int total_groups;
	char timezone[100];
	uint8_t current_pattern[MAX_DISPLAYS];
	display_group_t groups[MAX_GROUPS];
	enum PP_display_symbol_mode display_symbol_mode;
} status_t;

#endif /* MAIN_MAIN_H_ */
