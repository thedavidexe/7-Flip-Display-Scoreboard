/*
 * main.c
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

#include "main.h"
#include "mqtt_com.h"

status_t status = {};

void app_main(void)
{
	ESP_LOGI(FIRM, "Version: 0.0.5");
	
    /* Start all tasks */
    xTaskCreate(RestfulServerTask, "RestfulServerTask", 4048, NULL, 1, NULL);
    xTaskCreate(mqtt_task, "MqttTask", 4096, NULL, 5, NULL);
    
    shift_register_init();
    detect_display_count();
   
    //DisplayNumber(2);
    DisplaySymbol(0x159A, 1);
    DisplaySymbol(0x199A, 2);
    
    while(1)
    {

//		if(i>99) i = 0;

		vTaskDelay(100/ portTICK_PERIOD_MS);
	}

}
