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
#include "74AHC595.h"
#include "config.h"
#include "freertos/idf_additions.h"
#include "led.h"
#include "nvs.h"
#include "nvs_flash.h"

//Show dump of entire NVS
//#define NVS_DATA

extern const char *CONFIG_TAG;
status_t status = {};
SemaphoreHandle_t xNewDataSemaphore = NULL;
SemaphoreHandle_t xPeriodicSemaphore = NULL;
static TaskHandle_t xTimerTaskHandle = NULL;

void vDataProcessingTask(void *arg);
void vClockModeHandlingTask(void *arg);
void vTimerModeHandlingTask(void *arg);


#ifdef NVS_DATA
void print_nvs_stats(void)
{
    nvs_stats_t nvs_stats;
    esp_err_t err = nvs_get_stats(NULL, &nvs_stats);
    if (err != ESP_OK) {
        ESP_LOGI("NVS","Błąd przy pobieraniu statystyk NVS: %s\n", esp_err_to_name(err));
        return;
    }

    ESP_LOGI("NVS","  Used entry: %d", nvs_stats.used_entries);
    ESP_LOGI("NVS","  Free entry: %d", nvs_stats.free_entries);
    ESP_LOGI("NVS","  All entry: %d", nvs_stats.total_entries);
    
}

void dump_nvs_entries(void)
{
    nvs_iterator_t it = NULL;
    // Używamy nvs_entry_find() przekazując adres iteratora jako czwarty argument.
    esp_err_t err = nvs_entry_find("nvs", NULL, NVS_TYPE_ANY, &it);
    if (err != ESP_OK || it == NULL) {
        ESP_LOGI("NVS_DUMP", "Brak zapisanych wpisów.");
        return;
    }
    
    ESP_LOGI("NVS_DUMP", "Lista wpisów w NVS:");
    do {
        nvs_entry_info_t info;
        err = nvs_entry_info(it, &info);
        if (err == ESP_OK) {
            ESP_LOGI("NVS_DUMP", "Namespace: %s, Klucz: %s, Typ: %d",
                     info.namespace_name, info.key, info.type);
        } else {
            ESP_LOGE("NVS_DUMP", "Błąd pobierania informacji o wpisie: %s", esp_err_to_name(err));
            break;
        }
    } while (nvs_entry_next(&it) == ESP_OK && it != NULL);
    
    nvs_release_iterator(it);
}
#endif

void app_main(void)
{
	ESP_LOGI(FIRM, "Version: 0.1.1");
	
    /* Start all tasks */
    xTaskCreate(vDataProcessingTask, "vDataProcessingTask", 4096, NULL, 6, NULL);
    xTaskCreate(vClockModeHandlingTask, "vClockModeHandlingTask", 4096, NULL, 5, NULL);
    xTaskCreate(vTimerModeHandlingTask, "vTimerModeHandlingTask", 4096, NULL, 4, NULL);
    vTaskDelay(10/ portTICK_PERIOD_MS);
    
    xTaskCreate(RestfulServerTask, "RestfulServerTask", 4048, NULL, 10, NULL);
    xTaskCreate(mqtt_task, "MqttTask", 4096, NULL, 7, NULL);
    xTaskCreate(RTCHandlingTask, "RTCHandlingTask", 4096, NULL, 3, NULL);
    xTaskCreate(vLED_HandleTask, "vLED_HandleTask", 2096, NULL, 1, NULL);

#ifdef NVS_DATA    
    print_nvs_stats();
	dump_nvs_entries();
#endif
    
    shift_register_init();
    vTaskDelay(100/ portTICK_PERIOD_MS);
    
    detect_display_count();
	
	
	if (is_first_run())
	{
		ESP_LOGI(CONFIG_TAG, "Initialize `status` with default values...");
		
		//Default config
		status.groups[0].start_position = 0;
		status.groups[0].end_position = status.display_number-1;
		
		for(uint8_t i = 0; i < status.display_number; i++)
			status.groups[0].pattern[i] = 0;

		status.groups[0].separator = 0;
		status.groups[0].mode = 0;
		
		status.total_groups = 1;
		
		status.display_symbol_mode = SINGLE_SEGMENT;
		
		// Set dafault time
		uint8_t init_time[7] = {0,0,12,1,1,1,25}; // 12:00:00, 1 Jan 2025
		ds3231_init(init_time, CLOCK_RUN, FORCE_RESET);
	} 
	else 
	{
		ESP_LOGI(CONFIG_TAG, "Load `status` from NVS...");
		
	    load_config_from_nvs();
	}
	
	status.display_symbol_mode = SINGLE_MODUL;
	
	show_config();
	
	LED_set_color(YELLOW, 1);
	
    vTaskDelete(NULL);
}

void vDataProcessingTask(void *arg)
{
	xNewDataSemaphore = xSemaphoreCreateBinary();
    if (xNewDataSemaphore == NULL) {
        return;
    }
	
	while(1)
    {
		if (xSemaphoreTake(xNewDataSemaphore, portMAX_DELAY) == pdTRUE)
        {
			ESP_LOGI(CONFIG_TAG, "New DATA from APP");
			ESP_LOGI(CONFIG_TAG, "total_groups: %d", status.total_groups);
			
			for(uint8_t i = 0; i < status.total_groups; i++)
		    {
				uint8_t displays_number = status.groups[i].end_position - status.groups[i].start_position + 1;
				
				ESP_LOGI(CONFIG_TAG, "group: %d", i);
				ESP_LOGI(CONFIG_TAG, "start position: %d", status.groups[i].start_position);
		    	ESP_LOGI(CONFIG_TAG, "end position: %d", status.groups[i].end_position);
			    ESP_LOGI(CONFIG_TAG, "separator: %d", status.groups[i].separator);
			    	  
			    if(status.groups[i].mode == MODE_MQTT){
					ESP_LOGI(CONFIG_TAG, "mode: MQTT");
					ESP_LOGI(CONFIG_TAG, "topic: %s", status.groups[i].mqtt.topic);	
				}
				else if(status.groups[i].mode == MODE_TIMER){
					ESP_LOGI(CONFIG_TAG, "mode: TIMER");
					
					status.groups[i].timer.value = status.groups[i].timer.count_from;
					status.groups[i].timer.direction = (status.groups[i].timer.count_from < status.groups[i].timer.count_to) ? COUNT_UP : COUNT_DOWN;

//					if(status.groups[i].timer.count_from < status.groups[i].timer.count_to)
//						status.groups[i].timer.direction = COUNT_UP;	
//					else
// 						status.groups[i].timer.direction = COUNT_DOWN;
					
					DisplayNumber(status.groups[i].timer.value, i);
	 				
	 				// notify timer task (will either start or cancel current wait)
	 				xTaskNotifyGive(xTimerTaskHandle);
				}
				else if(status.groups[i].mode == MODE_CLOCK){
					ESP_LOGI(CONFIG_TAG, "mode: CLOCK");
					ESP_LOGI(CONFIG_TAG, "type: %d", status.groups[i].clock.type);
					
					xTaskNotifyGive(xTimerTaskHandle);

					read_time();
					
					if(status.groups[i].clock.time_format == FORMAT_12H)  //12H
					{
						if(status.rtc.hour <= 12)
							ESP_LOGI(RTC, "Time %02d:%02d:%02d AM  Data %d.%d.%d", status.rtc.hour, status.rtc.minute, status.rtc.second, status.rtc.day, status.rtc.month, status.rtc.year);
						else
							ESP_LOGI(RTC, "Time %02d:%02d:%02d PM  Data %d.%d.%d", status.rtc.hour - 12, status.rtc.minute, status.rtc.second, status.rtc.day, status.rtc.month, status.rtc.year);	
					}		
					else //24H
					{
						ESP_LOGI(RTC, "Time %02d:%02d:%02d  Data %d.%d.%d", status.rtc.hour, status.rtc.minute, status.rtc.second, status.rtc.day, status.rtc.month, status.rtc.year);
					}	
					
					if(status.groups[i].clock.type == RTC_SECONDS)
						DisplayNumber(status.rtc.second, i);
					else if(status.groups[i].clock.type == RTC_MINUTES)
						DisplayNumber(status.rtc.minute, i);
					else if(status.groups[i].clock.type == RTC_HOURS)
					{
						if(status.groups[i].clock.time_format == FORMAT_12H)
						{
							if(status.rtc.hour <= 12)
								DisplayNumber(status.rtc.hour, i);
							else
								DisplayNumber(status.rtc.hour - 12, i);
						}
						else 
						{
							DisplayNumber(status.rtc.hour, i);
						}
					}
					else if(status.groups[i].clock.type == RTC_DAY)
						DisplayNumber(status.rtc.day, i);
					else if(status.groups[i].clock.type == RTC_MONTCH)
						DisplayNumber(status.rtc.month, i);
					else if(status.groups[i].clock.type == RTC_YEAR)
						DisplayNumber(status.rtc.year, i);										
										
				}
				else if(status.groups[i].mode == MODE_MANUAL){
					ESP_LOGI(CONFIG_TAG, "mode: MANUAL");
					
					for(uint8_t l = status.groups[i].start_position; l <= status.groups[i].end_position; l++)
						DisplaySymbol(status.groups[i].pattern[l - status.groups[i].start_position], l);
				}
				else if(status.groups[i].mode == MODE_CUSTOM_API){ // TO DO
					ESP_LOGI(CONFIG_TAG, "mode: CUSTOM_API");
					DemoMode(1);
				}
				else{
					ESP_LOGI(CONFIG_TAG, "mode: NONE");
					
					for(uint8_t k = status.groups[i].start_position; k <= status.groups[i].end_position; k++)
						DisplayDigit(10, k);
				}
			    
			    
			    for(uint8_t j = 0; j < displays_number; j++)
					ESP_LOGI(CONFIG_TAG, "disp_%d: %d", j, status.groups[i].pattern[j]);
					
			}
			
			LED_set_color(RED, 1);
			
			save_config_to_nvs();
			
        }
	}
}

void vClockModeHandlingTask(void *arg)
{
	xPeriodicSemaphore = xSemaphoreCreateBinary();
    if (xPeriodicSemaphore == NULL) {
        return;
    }
    
    ESP_LOGI(CONFIG_TAG, "CLOCK");
    
    ENABLE_ONE_SEC_ISR();
	
	while(1)
    {
		if (xSemaphoreTake(xPeriodicSemaphore, portMAX_DELAY) == pdTRUE)
        {
			CLEAR_ONE_SEC_FLAG();
			
			read_time();

			for(uint8_t i = 0; i < status.total_groups; i++)
			{
				/* CLOCK MODE */
				if(status.groups[i].mode == MODE_CLOCK)
				{		
					if(status.groups[i].clock.type == RTC_SECONDS)
					{
						DisplayNumber(status.rtc.second, i);
					}
					else if(status.groups[i].clock.type == RTC_MINUTES)
					{
						DisplayNumber(status.rtc.minute, i);
					}
					else if(status.groups[i].clock.type == RTC_HOURS)
					{
						if(status.groups[i].clock.time_format == FORMAT_12H)
						{
							if(status.rtc.hour <= 12)
								DisplayNumber(status.rtc.hour, i);
							else
								DisplayNumber(status.rtc.hour - 12, i);
						}
						else 
						{
							DisplayNumber(status.rtc.hour, i);
						}
					}
					else if(status.groups[i].clock.type == RTC_DAY)
					{
						DisplayNumber(status.rtc.day, i);
					}
					else if(status.groups[i].clock.type == RTC_MONTCH)
					{
						DisplayNumber(status.rtc.month, i);
					}
					else if(status.groups[i].clock.type == RTC_YEAR)
					{
						DisplayNumber(status.rtc.year, i);
					}
				}
			}
		}
	}
}

void vTimerModeHandlingTask(void *arg)
{
    // store own handle
    xTimerTaskHandle = xTaskGetCurrentTaskHandle();

    for( ;; )
    {
        // wait indefinitely for a “start/reset” notification
        ulTaskNotifyTake(pdFALSE, portMAX_DELAY);

    RESTART_TIMER:;

        // iterate over all groups in TIMER mode
        for(uint8_t i = 0; i < status.total_groups; i++)
        {
            if(status.groups[i].mode != MODE_TIMER ||
               status.groups[i].timer.direction == COUNT_OFF)
            {
                continue;
            }

            // compute tick interval from seconds (or whatever masz w interval_unit)
            TickType_t intervalTicks = pdMS_TO_TICKS(
                status.groups[i].timer.interval_unit * 1000UL
            );

            // make local copies of the state
            int32_t value     = status.groups[i].timer.value;
            int32_t target    = status.groups[i].timer.count_to;
            int32_t direction = status.groups[i].timer.direction;

            // loop until we hit target OR we get another notify
            while((direction == COUNT_UP && value < target) || (direction == COUNT_DOWN && value > target))
            {
                // wait for interval OR reset/cancel
                if( ulTaskNotifyTake(pdFALSE, intervalTicks) > 0 )
                {
                    // notification ⇒ user wants reset
                    goto RESTART_TIMER;
                }

                // timeout ⇒ update value i display
                if(direction == COUNT_UP)
                    value++;
                else
                    value--;

                status.groups[i].timer.value = value;
                DisplayNumber(value, i);
            }

            // Natural ending – disable timer
            status.groups[i].timer.direction = COUNT_OFF;
            
            if(status.groups[i].timer.alarm == true)
            	GenerateAlarm(i);
        }
    }
}