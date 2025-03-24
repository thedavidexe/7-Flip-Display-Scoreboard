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
#include "nvs_flash.h"
#include "nvs.h"
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
static void ota_firmware_task(void *pvParameter)
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
 * @brief OTA web update task.
 *
 * This task downloads the web application binary from the given URL and writes it to the inactive web partition.
 *
 * Note: Switching the active www partition is application-specific. You must implement the logic
 * to mark the updated partition as active.
 *
 * @param pvParameter Pointer to a string containing the OTA URL.
 */
static void ota_web_app_task(void *pvParameter)
{
    char *ota_url = (char *)pvParameter;
    ESP_LOGI(TAG, "Starting web (www) OTA update from URL: %s", ota_url);

    // Open NVS and read active partition flag
    char active_www[8] = {0};
    char passive_label[8] = {0};
    nvs_handle_t nvs = 0;
    if (nvs_open("storage", NVS_READWRITE, &nvs) == ESP_OK) {
        size_t len = sizeof(active_www);
        if (nvs_get_str(nvs, "active_www", active_www, &len) != ESP_OK) {
            // If no flag is found, assume www_0 is active by default
            strcpy(active_www, "www_0");
        }
        // Determine the inactive (passive) partition label
        if (strcmp(active_www, "www_0") == 0) {
            strcpy(passive_label, "www_1");
        } else {
            strcpy(passive_label, "www_0");
        }
    } else {
        ESP_LOGW(TAG, "Failed to open NVS, defaulting passive partition to www_1");
        strcpy(passive_label, "www_1");
    }

    // Find the inactive web partition using the passive label
    const esp_partition_t *update_partition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, passive_label);
    if (update_partition == NULL) {
        ESP_LOGE(TAG, "Inactive web partition (%s) not found", passive_label);
        if (nvs) {
            nvs_close(nvs);
        }
        free(ota_url);
        vTaskDelete(NULL);
        return;
    }

    // Erase the inactive partition before starting the HTTP connection
    ESP_LOGI(TAG, "Erasing inactive web partition: %s", update_partition->label);
    esp_err_t err = esp_partition_erase_range(update_partition, 0, update_partition->size);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to erase web partition: %s", esp_err_to_name(err));
        if (nvs) {
            nvs_close(nvs);
        }
        free(ota_url);
        vTaskDelete(NULL);
        return;
    }

    // Configure HTTP client without an event handler to avoid consuming data in the event callback
    esp_http_client_config_t config = {
        .url = ota_url,
        .timeout_ms = 10000,
        .cert_pem = (char *)server_cert_pem_start,
        .method = HTTP_METHOD_GET,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        ESP_LOGE(TAG, "Failed to initialise HTTP connection");
        if (nvs) {
            nvs_close(nvs);
        }
        free(ota_url);
        vTaskDelete(NULL);
        return;
    }

    err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open HTTP connection: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        if (nvs) {
            nvs_close(nvs);
        }
        free(ota_url);
        vTaskDelete(NULL);
        return;
    }

    // Optionally fetch headers to obtain content length
    int content_length = esp_http_client_fetch_headers(client);
    if (content_length < 0) {
        ESP_LOGE(TAG, "Failed to fetch HTTP headers");
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        if (nvs) {
            nvs_close(nvs);
        }
        free(ota_url);
        vTaskDelete(NULL);
        return;
    }
    ESP_LOGI(TAG, "Content length: %d", content_length);

    // Read data from HTTP and write to the inactive partition
    int total_written = 0;
    char buffer[1024];
    int bytes_read = 0;
    while ((bytes_read = esp_http_client_read(client, buffer, sizeof(buffer))) > 0) {
        err = esp_partition_write(update_partition, total_written, buffer, bytes_read);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Error writing to web partition: %s", esp_err_to_name(err));
            esp_http_client_close(client);
            esp_http_client_cleanup(client);
            if (nvs) {
                nvs_close(nvs);
            }
            free(ota_url);
            vTaskDelete(NULL);
            return;
        }
        total_written += bytes_read;
        ESP_LOGI(TAG, "Written %d bytes to web partition", total_written);
    }

    if (bytes_read < 0) {
        ESP_LOGE(TAG, "Error during receiving web update data");
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        if (nvs) {
            nvs_close(nvs);
        }
        free(ota_url);
        vTaskDelete(NULL);
        return;
    }

    // Clean up the HTTP client
    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    // Validate the downloaded image (ensure more than 0 bytes were written)
    if (total_written <= 0) {
        ESP_LOGE(TAG, "Downloaded image is invalid");
        if (nvs) {
            nvs_close(nvs);
        }
        free(ota_url);
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "Web OTA update successful. New web partition (%s) written (%d bytes).", update_partition->label, total_written);

    // Update the active partition flag in NVS to mark the newly written partition as active
    if (nvs) {
        err = nvs_set_str(nvs, "active_www", passive_label);
        if (err == ESP_OK) {
            nvs_commit(nvs);
            ESP_LOGI(TAG, "Active web partition updated to: %s", passive_label);
        } else {
            ESP_LOGE(TAG, "Failed to update active web partition flag in NVS");
        }
        nvs_close(nvs);
    }

    free(ota_url);
    // Restart the device to apply the new web application image
    esp_restart();
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
esp_err_t ota_start(const char *url, ota_type_t type)
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
    BaseType_t result;
    if (type == OTA_TYPE_FIRMWARE) {
        result = xTaskCreate(ota_firmware_task, "ota_firmware_task", 8192, ota_url, 3, NULL);
    } else if (type == OTA_TYPE_WEB_APP) {
        result = xTaskCreate(ota_web_app_task, "ota_web_app_task", 8192, ota_url, 3, NULL);
    } else {
        free(ota_url);
        return ESP_ERR_INVALID_ARG;
    }

    if (result != pdPASS) {
        free(ota_url);
        return ESP_FAIL;
    }
    return ESP_OK;
}
