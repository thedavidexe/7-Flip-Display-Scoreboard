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
#include "driver/gpio.h"

//Show dump of entire NVS
//#define NVS_DATA

extern const char *CONFIG_TAG;
status_t status = {};
SemaphoreHandle_t xNewDataSemaphore = NULL;
SemaphoreHandle_t xPeriodicSemaphore = NULL;
static TaskHandle_t xTimerTaskHandle = NULL;
static TaskHandle_t xScoreATaskHandle = NULL;
static QueueHandle_t xScoreAQueue = NULL;
static const char *SCOREA_TAG = "SCORE_A";
static TaskHandle_t xScoreBTaskHandle = NULL;
static QueueHandle_t xScoreBQueue = NULL;
static const char *SCOREB_TAG = "SCORE_B";

// Player A input to increment score (remote control)
// Using GPIO32 (RTC-capable). Internal pull-up enabled for active-low.
#define SCORE_A_INPUT_PIN     GPIO_NUM_32
// Which display group to show the score on (0-based)
#define SCORE_A_GROUP_INDEX   0
// Debounce time for mechanical button (ms)
#define SCORE_DEBOUNCE_MS   50

// Player B input on GPIO4
#define SCORE_B_INPUT_PIN   GPIO_NUM_4
#define SCORE_B_GROUP_INDEX 1

static volatile uint32_t score_value_a = 0;
static volatile uint32_t score_value_b = 0;

static void IRAM_ATTR score_a_isr_handler(void* arg)
{
    uint32_t evt = 1;
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    if (xScoreAQueue) {
        xQueueSendFromISR(xScoreAQueue, &evt, &xHigherPriorityTaskWoken);
    }
    if (xHigherPriorityTaskWoken) {
        portYIELD_FROM_ISR();
    }
}

static void IRAM_ATTR score_b_isr_handler(void* arg)
{
    uint32_t evt = 1;
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    if (xScoreBQueue) {
        xQueueSendFromISR(xScoreBQueue, &evt, &xHigherPriorityTaskWoken);
    }
    if (xHigherPriorityTaskWoken) {
        portYIELD_FROM_ISR();
    }
}

static void init_score_a_input(void)
{
    ESP_LOGI(SCOREA_TAG, "Init score A input on GPIO%d (pullup=ON, intr=ANYEDGE)", (int)SCORE_A_INPUT_PIN);
    gpio_config_t io_conf = {
        .pin_bit_mask = 1ULL << SCORE_A_INPUT_PIN,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,    // GPIO32 supports internal pull-up
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_ANYEDGE       // Handle press and release with debounce
    };
    gpio_config(&io_conf);

    // Install ISR service if not already installed elsewhere
    esp_err_t r = gpio_install_isr_service(0);
    if (r != ESP_OK && r != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(FIRM, "gpio_install_isr_service failed: %s", esp_err_to_name(r));
    }
    else if (r == ESP_OK) {
        ESP_LOGI(SCOREA_TAG, "GPIO ISR service installed");
    } else {
        ESP_LOGI(SCOREA_TAG, "GPIO ISR service already installed");
    }
    gpio_isr_handler_add(SCORE_A_INPUT_PIN, score_a_isr_handler, NULL);
    ESP_LOGI(SCOREA_TAG, "ISR handler attached to GPIO%d", (int)SCORE_A_INPUT_PIN);
}

static void vScoreATask(void *arg)
{
    uint32_t dummy;
    // Initialize display with current score
	vTaskDelay(250 / portTICK_PERIOD_MS);
    DisplayNumber(score_value_a, SCORE_A_GROUP_INDEX);
    ESP_LOGI(SCOREA_TAG, "Score A task started (group=%d, initial=%u)", SCORE_A_GROUP_INDEX, (unsigned)score_value_a);
    bool pressed = false; // latched logical state
    int stable_level = gpio_get_level(SCORE_A_INPUT_PIN);
    pressed = (stable_level == 0);
    for(;;) {
        if (xQueueReceive(xScoreAQueue, &dummy, portMAX_DELAY) == pdTRUE) {
            // Sample level, wait debounce, sample again
            int l1 = gpio_get_level(SCORE_A_INPUT_PIN);
            vTaskDelay(pdMS_TO_TICKS(SCORE_DEBOUNCE_MS));
            int l2 = gpio_get_level(SCORE_A_INPUT_PIN);

            if (l1 == l2 && l2 != stable_level) {
                // Stable change detected
                stable_level = l2;
                if (stable_level == 0 && !pressed) {
                    // Confirmed press
                    pressed = true;
                    score_value_a++;
                    DisplayNumber(score_value_a, SCORE_A_GROUP_INDEX);
                    ESP_LOGI(SCOREA_TAG, "Score A press -> %u", (unsigned)score_value_a);
                } else if (stable_level == 1 && pressed) {
                    // Confirmed release
                    pressed = false;
                }
            }
        }
    }
}

static void init_score_b_input(void)
{
    ESP_LOGI(SCOREB_TAG, "Init score B input on GPIO%d (pullup=ON, intr=ANYEDGE)", (int)SCORE_B_INPUT_PIN);
    gpio_config_t io_conf = {
        .pin_bit_mask = 1ULL << SCORE_B_INPUT_PIN,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_ANYEDGE
    };
    gpio_config(&io_conf);

    // ISR service should already be installed by init_score_a_input(); tolerate duplicates
    esp_err_t r = gpio_install_isr_service(0);
    if (r != ESP_OK && r != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(FIRM, "gpio_install_isr_service (B) failed: %s", esp_err_to_name(r));
    }
    gpio_isr_handler_add(SCORE_B_INPUT_PIN, score_b_isr_handler, NULL);
    ESP_LOGI(SCOREB_TAG, "ISR handler attached to GPIO%d", (int)SCORE_B_INPUT_PIN);
}

static void vScoreBTask(void *arg)
{
    uint32_t dummy;
    // Initialize display for B with current score
    DisplayNumber(score_value_b, SCORE_B_GROUP_INDEX);
    ESP_LOGI(SCOREB_TAG, "Score B task started (group=%d, initial=%u)", SCORE_B_GROUP_INDEX, (unsigned)score_value_b);
    bool pressed = false; // latched logical state
    int stable_level = gpio_get_level(SCORE_B_INPUT_PIN);
    pressed = (stable_level == 0);
    for(;;) {
        if (xQueueReceive(xScoreBQueue, &dummy, portMAX_DELAY) == pdTRUE) {
            int l1 = gpio_get_level(SCORE_B_INPUT_PIN);
            vTaskDelay(pdMS_TO_TICKS(SCORE_DEBOUNCE_MS));
            int l2 = gpio_get_level(SCORE_B_INPUT_PIN);

            if (l1 == l2 && l2 != stable_level) {
                stable_level = l2;
                if (stable_level == 0 && !pressed) {
                    pressed = true;
                    score_value_b++;
                    DisplayNumber(score_value_b, SCORE_B_GROUP_INDEX);
                    ESP_LOGI(SCOREB_TAG, "Score B press -> %u", (unsigned)score_value_b);
                } else if (stable_level == 1 && pressed) {
                    pressed = false;
                }
            }
        }
    }
}

void vDataProcessingTask(void *arg);
void vClockModeHandlingTask(void *arg);
void vTimerModeHandlingTask(void *arg);

// Force two groups: [0..1] and [2..3]
static void hardcode_two_groups_01_and_23(void)
{
    if (status.display_number >= 4) {
        status.total_groups = 2;

        status.groups[0].start_position = 0;
        status.groups[0].end_position   = 1;
        status.groups[0].separator      = SEP_NULL;
        status.groups[0].mode           = MODE_NONE;

        status.groups[1].start_position = 2;
        status.groups[1].end_position   = 3;
        status.groups[1].separator      = SEP_NULL;
        status.groups[1].mode           = MODE_NONE;

        for (int i = 0; i < MAX_DISPLAYS; i++) {
            status.groups[0].pattern[i] = 0;
            status.groups[1].pattern[i] = 0;
        }

        ESP_LOGI(CONFIG_TAG, "Hardcoded groups: [0-1] and [2-3]");
    } else if (status.display_number >= 2) {
        status.total_groups = 1;
        status.groups[0].start_position = 0;
        status.groups[0].end_position   = 1;
        status.groups[0].separator      = SEP_NULL;
        status.groups[0].mode           = MODE_NONE;
        for (int i = 0; i < MAX_DISPLAYS; i++) status.groups[0].pattern[i] = 0;
        ESP_LOGI(CONFIG_TAG, "Hardcoded single group: [0-1] (only %u displays present)", status.display_number);
    } else {
        status.total_groups = 1;
        status.groups[0].start_position = 0;
        status.groups[0].end_position   = status.display_number ? (status.display_number - 1) : 0;
        status.groups[0].separator      = SEP_NULL;
        status.groups[0].mode           = MODE_NONE;
        for (int i = 0; i < MAX_DISPLAYS; i++) status.groups[0].pattern[i] = 0;
        ESP_LOGI(CONFIG_TAG, "Hardcoded minimal group: [0-%u]", status.groups[0].end_position);
    }
}

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
	
    // /* Start all tasks */
    // xTaskCreate(vDataProcessingTask, "vDataProcessingTask", 4096, NULL, 6, NULL);
    // xTaskCreate(vClockModeHandlingTask, "vClockModeHandlingTask", 4096, NULL, 5, NULL);
    // xTaskCreate(vTimerModeHandlingTask, "vTimerModeHandlingTask", 4096, NULL, 4, NULL);
    // vTaskDelay(10/ portTICK_PERIOD_MS);
    
    // xTaskCreate(RestfulServerTask, "RestfulServerTask", 4048, NULL, 10, NULL);
    // xTaskCreate(mqtt_task, "MqttTask", 4096, NULL, 7, NULL);
    // xTaskCreate(RTCHandlingTask, "RTCHandlingTask", 4096, NULL, 3, NULL);
    // xTaskCreate(vLED_HandleTask, "vLED_HandleTask", 2096, NULL, 1, NULL);

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

    // Hardcode groups: Group 0 = displays 0..1, Group 1 = displays 2..3
    hardcode_two_groups_01_and_23();
    // // Optional: persist to NVS so web UI reflects the hardcoded groups
    // save_config_to_nvs();
	
	show_config();
	
    LED_set_color(YELLOW, 1);

    // Setup score A input and task
    xScoreAQueue = xQueueCreate(10, sizeof(uint32_t));
    if (xScoreAQueue != NULL) {
        ESP_LOGI(SCOREA_TAG, "Score A queue created");
        init_score_a_input();
        xTaskCreate(vScoreATask, "vScoreATask", 2048, NULL, 8, &xScoreATaskHandle);
        ESP_LOGI(SCOREA_TAG, "Score A task created");
    } else {
        ESP_LOGE(FIRM, "Failed to create score A queue");
    }

    // Setup score B input and task
    xScoreBQueue = xQueueCreate(10, sizeof(uint32_t));
    if (xScoreBQueue != NULL) {
        ESP_LOGI(SCOREB_TAG, "Score B queue created");
        init_score_b_input();
        xTaskCreate(vScoreBTask, "vScoreBTask", 2048, NULL, 8, &xScoreBTaskHandle);
        ESP_LOGI(SCOREB_TAG, "Score B task created");
    } else {
        ESP_LOGE(FIRM, "Failed to create score B queue");
    }
    
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
