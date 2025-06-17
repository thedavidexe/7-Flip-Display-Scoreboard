/*
 * led.c
 *
 *  Created on: May 22, 2025
 *      Author: Sebastian Sokolowski
 *      Company: Smart Solutions for Home
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

#include "led.h"
#include "driver/ledc.h"
#include "main.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include <math.h>

extern status_t status;

/**
 * @brief Initialize the LED PWM timer and channels for RGB control.
 *
 * @return ESP_OK on success, or an error code otherwise.
 */
static esp_err_t LED_Init(void)
{
    esp_err_t err;

    // 1) Configure the PWM timer
    ledc_timer_config_t timer_conf = {
        .speed_mode       = LEDC_MODE,
        .timer_num        = LEDC_TIMER,
        .duty_resolution  = LEDC_DUTY_RES,
        .freq_hz          = LEDC_FREQUENCY,
        .clk_cfg          = LEDC_AUTO_CLK
    };
    err = ledc_timer_config(&timer_conf);
    if (err != ESP_OK) {
        return err;
    }

    // 2) Configure the RED channel
    ledc_channel_config_t ch_conf = {
        .speed_mode     = LEDC_MODE,
        .channel        = LEDC_CHANNEL_R,
        .timer_sel      = LEDC_TIMER,
        .intr_type      = LEDC_INTR_DISABLE,
        .gpio_num       = LED_GPIO_RED,
        .duty           = 0,            // start at 0 duty
        .hpoint         = 0
    };
    err = ledc_channel_config(&ch_conf);
    if (err != ESP_OK) {
        return err;
    }

    // 3) Configure the GREEN channel
    ch_conf.channel  = LEDC_CHANNEL_G;
    ch_conf.gpio_num = LED_GPIO_GREEN;
    err = ledc_channel_config(&ch_conf);
    if (err != ESP_OK) {
        return err;
    }

    // 4) Configure the BLUE channel
    ch_conf.channel  = LEDC_CHANNEL_B;
    ch_conf.gpio_num = LED_GPIO_BLUE;
    err = ledc_channel_config(&ch_conf);
    if (err != ESP_OK) {
        return err;
    }

    return ESP_OK;
}

/**
 * @brief Set the RGB LED color with brightness scaling.
 *
 * @param r           Red component (0–255)
 * @param g           Green component (0–255)
 * @param b           Blue component (0–255)
 * @param brightness  Brightness percentage (0 = off, 100 = full brightness)
 */
void LED_set_color(uint8_t r, uint8_t g, uint8_t b, uint8_t brightness)
{
    uint8_t r_scaled = 0;
    uint8_t g_scaled = 0;
    uint8_t b_scaled = 0;
	
    // Clamp brightness to valid range
    if (brightness > 100) {
        brightness = 100;
    }
	
	if(status.led == true)
	{	
	    // Scale each color component by brightness percentage
	    r_scaled = (uint16_t)r * brightness / 100;
	    g_scaled = (uint16_t)g * brightness / 100;
	    b_scaled = (uint16_t)b * brightness / 100;
    }
	
    // Update PWM duty for each channel
    ledc_set_duty(LEDC_MODE, LEDC_CHANNEL_R, r_scaled);
    ledc_update_duty(LEDC_MODE, LEDC_CHANNEL_R);

    ledc_set_duty(LEDC_MODE, LEDC_CHANNEL_G, g_scaled);
    ledc_update_duty(LEDC_MODE, LEDC_CHANNEL_G);

    ledc_set_duty(LEDC_MODE, LEDC_CHANNEL_B, b_scaled);
    ledc_update_duty(LEDC_MODE, LEDC_CHANNEL_B);

}

/**
 * @brief FreeRTOS task to initialize and handle the LED.
 *
 * Sets up the LED driver, applies an initial color, and enters an infinite loop.
 */
void vLED_HandleTask(void *arg)
{
    // Initialize LED driver
    if (LED_Init() != ESP_OK) {
        // Initialization failed; delete this task
        vTaskDelete(NULL);
        return;
    }

	vTaskDelete(NULL);
}
