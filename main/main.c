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
#include "driver/gpio.h"

status_t status = {};

//void print_nvs_stats(void)
//{
//    nvs_stats_t nvs_stats;
//    esp_err_t err = nvs_get_stats(NULL, &nvs_stats);
//    if (err != ESP_OK) {
//        ESP_LOGI("NVS","Błąd przy pobieraniu statystyk NVS: %s\n", esp_err_to_name(err));
//        return;
//    }
//
//    ESP_LOGI("NVS","  Used entry: %d", nvs_stats.used_entries);
//    ESP_LOGI("NVS","  Free entry: %d", nvs_stats.free_entries);
//    ESP_LOGI("NVS","  All entry: %d", nvs_stats.total_entries);
//    
//}
//
//void dump_nvs_entries(void)
//{
//    nvs_iterator_t it = NULL;
//    // Używamy nvs_entry_find() przekazując adres iteratora jako czwarty argument.
//    esp_err_t err = nvs_entry_find("nvs", NULL, NVS_TYPE_ANY, &it);
//    if (err != ESP_OK || it == NULL) {
//        ESP_LOGI("NVS_DUMP", "Brak zapisanych wpisów.");
//        return;
//    }
//    
//    ESP_LOGI("NVS_DUMP", "Lista wpisów w NVS:");
//    do {
//        nvs_entry_info_t info;
//        err = nvs_entry_info(it, &info);
//        if (err == ESP_OK) {
//            ESP_LOGI("NVS_DUMP", "Namespace: %s, Klucz: %s, Typ: %d",
//                     info.namespace_name, info.key, info.type);
//        } else {
//            ESP_LOGE("NVS_DUMP", "Błąd pobierania informacji o wpisie: %s", esp_err_to_name(err));
//            break;
//        }
//    } while (nvs_entry_next(&it) == ESP_OK && it != NULL);
//    
//    nvs_release_iterator(it);
//}


void app_main(void)
{
	ESP_LOGI(FIRM, "Version: 0.0.5");
	
    /* Start all tasks */
    xTaskCreate(RestfulServerTask, "RestfulServerTask", 4048, NULL, 1, NULL);
    xTaskCreate(mqtt_task, "MqttTask", 4096, NULL, 5, NULL);
    
    //print_nvs_stats();
	//dump_nvs_entries();
    
    shift_register_init();
    vTaskDelay(100/ portTICK_PERIOD_MS);
    
    detect_display_count();
    //status.display_number = 5;
 
 	//Display "HELLO"
//	DisplayDigit(10, 0);
//    DisplayDigit(10, 1);
//    DisplayDigit(10, 2);
//    DisplayDigit(10, 3);
//    DisplayDigit(10, 4);
//        
//	DisplaySymbol(0x76, 0);
//    DisplaySymbol(0x79, 1);
//    DisplaySymbol(0x38, 2);
//    DisplaySymbol(0x38, 3);
//    DisplaySymbol(0x3F, 4);
    
	
	//Default config
	status.groups[0].start_position = 0;
	status.groups[0].end_position = status.display_number-1;
	status.groups[0].pattern[0] = 0;
	status.groups[0].separator = 0;
	status.groups[0].mode = 0;
    
    while(1)
    {
		
	    vTaskDelay(1000/ portTICK_PERIOD_MS);
	}

}
