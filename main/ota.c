/*
 * ota.c
 *
 *  Created on: 23 mar 2025
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

#include "ota.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_https_ota.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_system.h"
#include <stdlib.h>
#include <string.h>

static const char *TAG = "OTA";
extern const uint8_t server_cert_pem_start[] asm("_binary_ca_cert_pem_start");
extern const uint8_t server_cert_pem_end[] asm("_binary_ca_cert_pem_end");

/**
 * @brief HTTP event handler for OTA update.
 *
 * This function logs HTTP events during the OTA process.
 *
 * @param evt Pointer to the HTTP client event structure.
 * @return esp_err_t ESP_OK.
 */
static esp_err_t ota_http_event_handler(esp_http_client_event_t *evt)
{
    switch (evt->event_id) {
    case HTTP_EVENT_ERROR:
        ESP_LOGE(TAG, "HTTP_EVENT_ERROR");
        break;
    case HTTP_EVENT_ON_CONNECTED:
        ESP_LOGI(TAG, "Connected to server");
        break;
    case HTTP_EVENT_HEADER_SENT:
        ESP_LOGI(TAG, "HTTP header sent");
        break;
    case HTTP_EVENT_ON_HEADER:
        ESP_LOGI(TAG, "Received header: key=%s, value=%s", evt->header_key, evt->header_value);
        break;
    case HTTP_EVENT_ON_DATA:
        ESP_LOGI(TAG, "Received data, length=%d", evt->data_len);
        break;
    case HTTP_EVENT_ON_FINISH:
        ESP_LOGI(TAG, "HTTP_EVENT_ON_FINISH");
        break;
    case HTTP_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "Disconnected from server");
        break;
    default:
        break;
    }
    return ESP_OK;
}

/**
 * @brief OTA update task.
 *
 * This task downloads the firmware from the given URL and performs the OTA update.
 *
 * @param pvParameter Pointer to a string containing the OTA URL.
 */
static void ota_task(void *pvParameter)
{
    char *ota_url = (char *)pvParameter;
    ESP_LOGI(TAG, "Starting OTA update from URL: %s", ota_url);

    esp_http_client_config_t config = {
        .url = ota_url,
        .event_handler = ota_http_event_handler,
        .timeout_ms = 10000,
        .cert_pem = (char *)server_cert_pem_start,
    };

    esp_https_ota_config_t ota_config = {
        .http_config = &config,
    };

    esp_err_t ret = esp_https_ota(&ota_config);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "OTA update successful, restarting...");
        free(ota_url);
        esp_restart();
    } else {
        ESP_LOGE(TAG, "OTA update failed with error: %d", ret);
    }
    free(ota_url);
    vTaskDelete(NULL);
}

/**
 * @brief Starts the OTA update process.
 *
 * This function duplicates the provided URL and creates a FreeRTOS task to perform the OTA update.
 *
 * @param url The URL of the firmware binary.
 * @return esp_err_t ESP_OK on successful task creation, error code otherwise.
 */
esp_err_t ota_start(const char *url)
{
    if (url == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    // Duplicate the URL string to pass it to the OTA task.
    char *ota_url = strdup(url);
    if (ota_url == NULL) {
        return ESP_ERR_NO_MEM;
    }
    // Create the OTA task.
    BaseType_t result = xTaskCreate(ota_task, "ota_task", 8192, ota_url, 3, NULL);
    if (result != pdPASS) {
        free(ota_url);
        return ESP_FAIL;
    }
    return ESP_OK;
}
