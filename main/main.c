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
#include "freertos/timers.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "driver/gpio.h"
#include "ble_scoreboard.h"
#include "power_manager.h"

//Show dump of entire NVS
//#define NVS_DATA

const char *CONFIG_TAG = "CONFIG";
status_t status = {};
SemaphoreHandle_t xNewDataSemaphore = NULL;
SemaphoreHandle_t xPeriodicSemaphore = NULL;
static TaskHandle_t xTimerTaskHandle = NULL;

#define FULL_DISPLAY_RESET_TIME 1500  // 100ms * 7segs * 2digits + 100 buffer


// below are the tasks for the original display, no longer using these.
// void vDataProcessingTask(void *arg);
// void vClockModeHandlingTask(void *arg);
// void vTimerModeHandlingTask(void *arg);

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
	}
	else
	{
		ESP_LOGI(CONFIG_TAG, "Load `status` from NVS...");

	    load_config_from_nvs();
	}

    status.display_symbol_mode = SINGLE_MODUL;

    // Hardcode groups: Group 0 = displays 0..1, Group 1 = displays 2..3
    hardcode_two_groups_01_and_23();

	show_config();

    // Initialize BLE scoreboard service
    // - Clears existing bonds (requires re-pairing after each boot)
    // - Generates and displays hardware ID on 7-segment display
    // - Starts BLE advertising
    ble_scoreboard_init();

    // Initialize power manager
    // - Monitors BLE activity and enters deep sleep after 1 hour of inactivity
    // - Reduces power draw to ~10uA in deep sleep (6+ months on single 18650)
    // - Wake up via reset button
    power_manager_init();

    vTaskDelete(NULL);
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
