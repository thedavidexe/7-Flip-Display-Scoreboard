/*
 * ota.c
 *
 *  Created on: 23 mar 2025
 *      Author: Sebastian Sokołowski
 *      Company: Smart Solutions for Home
 *
 * SPDX-License-Identifier: MIT
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

// Global variable to track OTA progress (0-100%)
volatile int ota_progress = 0;

// Helper function to extract version string from URL
// It searches for prefix and suffix and extracts the string in between.
static void extract_version_from_url(const char *url, const char *prefix, const char *suffix, char *version, size_t version_size) {
    const char *start = strstr(url, prefix);
    if (start) {
        start += strlen(prefix);
        const char *end = strstr(start, suffix);
        if (end && (end - start) < version_size) {
            strncpy(version, start, end - start);
            version[end - start] = '\0';
        }
    }
}

/**
 * @brief HTTP event handler for OTA update.
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
 * @brief OTA firmware update task.
 */
static void ota_firmware_task(void *pvParameter)
{
    char *ota_url = (char *)pvParameter;
    ESP_LOGI(TAG, "Starting firmware OTA update from URL: %s", ota_url);

    esp_http_client_config_t config = {
        .url = ota_url,
        .event_handler = ota_http_event_handler,
        .timeout_ms = 10000,
        .cert_pem = (char *)server_cert_pem_start,
    };

    esp_https_ota_config_t ota_config = {
        .http_config = &config,
    };

    esp_https_ota_handle_t https_ota_handle = NULL;
    esp_err_t ret = esp_https_ota_begin(&ota_config, &https_ota_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_https_ota_begin failed (%d)", ret);
        free(ota_url);
        vTaskDelete(NULL);
        return;
    }

    while (1) {
        ret = esp_https_ota_perform(https_ota_handle);
        if (ret == ESP_ERR_HTTPS_OTA_IN_PROGRESS) {
            size_t image_len_read = esp_https_ota_get_image_len_read(https_ota_handle);
            size_t image_size = esp_https_ota_get_image_size(https_ota_handle);
            if (image_size > 0) {
                ota_progress = (image_len_read * 100) / image_size;
            }
            vTaskDelay(100 / portTICK_PERIOD_MS);
        } else {
            break;
        }
    }

    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Firmware OTA update successful, finalizing update...");
        ret = esp_https_ota_finish(https_ota_handle);

        // Extract version from URL: assume URL contains "firmware_v<version>.bin"
        char new_version[32] = {0};
        extract_version_from_url(ota_url, "firmware_v", ".bin", new_version, sizeof(new_version));

        // Update firmware version in NVS
        nvs_handle_t nvs;
        if(nvs_open("storage", NVS_READWRITE, &nvs) == ESP_OK) {
            nvs_set_str(nvs, "firm_version", new_version);
            nvs_commit(nvs);
            nvs_close(nvs);
            ESP_LOGI(TAG, "Firmware version updated in NVS: %s", new_version);
        } else {
            ESP_LOGE(TAG, "Failed to open NVS to update firmware version");
        }

        free(ota_url);
        esp_restart();
    } else {
        ESP_LOGE(TAG, "Firmware OTA update failed with error: %d", ret);
    }
    free(ota_url);
    vTaskDelete(NULL);
}

/**
 * @brief OTA web update task.
 */
static void ota_web_app_task(void *pvParameter)
{
    char *ota_url = (char *)pvParameter;
    ESP_LOGI(TAG, "Starting web (www) OTA update from URL: %s", ota_url);

    char active_www[8] = {0};
    char passive_label[8] = {0};
    nvs_handle_t nvs = 0;
    if (nvs_open("storage", NVS_READWRITE, &nvs) == ESP_OK) {
        size_t len = sizeof(active_www);
        if (nvs_get_str(nvs, "active_www", active_www, &len) != ESP_OK) {
            strcpy(active_www, "www_0");
        }
        if (strcmp(active_www, "www_0") == 0) {
            strcpy(passive_label, "www_1");
        } else {
            strcpy(passive_label, "www_0");
        }
    } else {
        ESP_LOGW(TAG, "Failed to open NVS, defaulting passive partition to www_1");
        strcpy(passive_label, "www_1");
    }

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
        ota_progress = (total_written * 100) / content_length;
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

    esp_http_client_close(client);
    esp_http_client_cleanup(client);

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

    if (nvs) {
        err = nvs_set_str(nvs, "active_www", passive_label);
        if (err == ESP_OK) {
            nvs_commit(nvs);
            ESP_LOGI(TAG, "Active web partition updated to: %s", passive_label);
        } else {
            ESP_LOGE(TAG, "Failed to update active web partition flag in NVS");
        }
        // Extract web app version from URL: assume URL contains "web_app_v<version>.bin"
        char new_version[32] = {0};
        extract_version_from_url(ota_url, "web_app_v", ".bin", new_version, sizeof(new_version));
        err = nvs_set_str(nvs, "web_app_version", new_version);
        if (err == ESP_OK) {
            nvs_commit(nvs);
            ESP_LOGI(TAG, "Web app version updated in NVS: %s", new_version);
        } else {
            ESP_LOGE(TAG, "Failed to update web app version in NVS");
        }
        nvs_close(nvs);
    }

    free(ota_url);
    esp_restart();
    vTaskDelete(NULL);
}

/**
 * @brief Combined OTA update task for both web app and firmware.
 */
static void ota_both_task(void *pvParameter)
{
    ota_both_param_t *param = (ota_both_param_t *)pvParameter;
    char *web_url = param->web_app_url;
    char *firmware_url = param->firmware_url;
    int ret;
    free(param);

    ESP_LOGI(TAG, "Starting combined OTA update.");
    
    // ----------- Web App Update Phase (0-50% progress) -----------
    char active_www[8] = {0};
    char passive_label[8] = {0};
    nvs_handle_t nvs = 0;
    if (nvs_open("storage", NVS_READWRITE, &nvs) == ESP_OK) {
        size_t len = sizeof(active_www);
        if (nvs_get_str(nvs, "active_www", active_www, &len) != ESP_OK) {
            strcpy(active_www, "www_0");
        }
        if (strcmp(active_www, "www_0") == 0) {
            strcpy(passive_label, "www_1");
        } else {
            strcpy(passive_label, "www_0");
        }
    } else {
        ESP_LOGW(TAG, "Failed to open NVS, defaulting passive partition to www_1");
        strcpy(passive_label, "www_1");
    }

    const esp_partition_t *update_partition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, passive_label);
    if (update_partition == NULL) {
        ESP_LOGE(TAG, "Inactive web partition (%s) not found", passive_label);
        if (nvs) {
            nvs_close(nvs);
        }
        free(web_url);
        free(firmware_url);
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "Erasing inactive web partition: %s", update_partition->label);
    esp_err_t err = esp_partition_erase_range(update_partition, 0, update_partition->size);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to erase web partition: %s", esp_err_to_name(err));
        if (nvs) {
            nvs_close(nvs);
        }
        free(web_url);
        free(firmware_url);
        vTaskDelete(NULL);
        return;
    }

    esp_http_client_config_t config_web = {
        .url = web_url,
        .timeout_ms = 10000,
        .cert_pem = (char *)server_cert_pem_start,
        .method = HTTP_METHOD_GET,
    };

    esp_http_client_handle_t client_web = esp_http_client_init(&config_web);
    if (client_web == NULL) {
        ESP_LOGE(TAG, "Failed to initialise HTTP connection for web OTA");
        if (nvs) {
            nvs_close(nvs);
        }
        free(web_url);
        free(firmware_url);
        vTaskDelete(NULL);
        return;
    }

    err = esp_http_client_open(client_web, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open HTTP connection for web OTA: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client_web);
        if (nvs) {
            nvs_close(nvs);
        }
        free(web_url);
        free(firmware_url);
        vTaskDelete(NULL);
        return;
    }

    int content_length_web = esp_http_client_fetch_headers(client_web);
    if (content_length_web < 0) {
        ESP_LOGE(TAG, "Failed to fetch HTTP headers for web OTA");
        esp_http_client_close(client_web);
        esp_http_client_cleanup(client_web);
        if (nvs) {
            nvs_close(nvs);
        }
        free(web_url);
        free(firmware_url);
        vTaskDelete(NULL);
        return;
    }
    ESP_LOGI(TAG, "Web OTA content length: %d", content_length_web);

    int total_written_web = 0;
    char buffer_web[1024];
    int bytes_read_web = 0;
    while ((bytes_read_web = esp_http_client_read(client_web, buffer_web, sizeof(buffer_web))) > 0) {
        err = esp_partition_write(update_partition, total_written_web, buffer_web, bytes_read_web);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Error writing to web partition during combined OTA: %s", esp_err_to_name(err));
            esp_http_client_close(client_web);
            esp_http_client_cleanup(client_web);
            if (nvs) {
                nvs_close(nvs);
            }
            free(web_url);
            free(firmware_url);
            vTaskDelete(NULL);
            return;
        }
        total_written_web += bytes_read_web;
        // Scale progress for web OTA phase (0 to 50%)
        ota_progress = (total_written_web * 50) / content_length_web;
        ESP_LOGI(TAG, "Combined OTA - Web phase: Written %d bytes", total_written_web);
    }

    if (bytes_read_web < 0) {
        ESP_LOGE(TAG, "Error during receiving web update data in combined OTA");
        esp_http_client_close(client_web);
        esp_http_client_cleanup(client_web);
        if (nvs) {
            nvs_close(nvs);
        }
        free(web_url);
        free(firmware_url);
        vTaskDelete(NULL);
        return;
    }

    esp_http_client_close(client_web);
    esp_http_client_cleanup(client_web);

    ESP_LOGI(TAG, "Web OTA phase completed in combined update. Total written: %d bytes", total_written_web);

    // Update active web partition and web app version in NVS (but do not reboot yet)
    if (nvs) {
        err = nvs_set_str(nvs, "active_www", passive_label);
        if (err == ESP_OK) {
            nvs_commit(nvs);
            ESP_LOGI(TAG, "Active web partition updated to: %s", passive_label);
        } else {
            ESP_LOGE(TAG, "Failed to update active web partition flag in NVS during combined OTA");
        }
        char new_web_version[32] = {0};
        extract_version_from_url(web_url, "web_app_v", ".bin", new_web_version, sizeof(new_web_version));
        err = nvs_set_str(nvs, "web_app_version", new_web_version);
        if (err == ESP_OK) {
            nvs_commit(nvs);
            ESP_LOGI(TAG, "Web app version updated in NVS: %s", new_web_version);
        } else {
            ESP_LOGE(TAG, "Failed to update web app version in NVS during combined OTA");
        }
        nvs_close(nvs);
    }

    free(web_url);

    // ----------- Firmware Update Phase (progress from 50% to 100%) -----------
    ESP_LOGI(TAG, "Starting firmware OTA phase in combined update from URL: %s", firmware_url);

    esp_http_client_config_t config_fw = {
        .url = firmware_url,
        .event_handler = ota_http_event_handler,
        .timeout_ms = 10000,
        .cert_pem = (char *)server_cert_pem_start,
    };

    esp_https_ota_config_t ota_config_fw = {
        .http_config = &config_fw,
    };

    esp_https_ota_handle_t https_ota_handle = NULL;
    ret = esp_https_ota_begin(&ota_config_fw, &https_ota_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_https_ota_begin failed in combined OTA (%d)", ret);
        free(firmware_url);
        vTaskDelete(NULL);
        return;
    }

    while (1) {
        ret = esp_https_ota_perform(https_ota_handle);
        if (ret == ESP_ERR_HTTPS_OTA_IN_PROGRESS) {
            size_t image_len_read = esp_https_ota_get_image_len_read(https_ota_handle);
            size_t image_size = esp_https_ota_get_image_size(https_ota_handle);
            if (image_size > 0) {
                int fw_progress = (image_len_read * 50) / image_size;
                ota_progress = 50 + fw_progress;
            }
            vTaskDelay(100 / portTICK_PERIOD_MS);
        } else {
            break;
        }
    }

    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Firmware OTA phase completed in combined update.");
        ret = esp_https_ota_finish(https_ota_handle);

        char new_fw_version[32] = {0};
        extract_version_from_url(firmware_url, "firmware_v", ".bin", new_fw_version, sizeof(new_fw_version));
        nvs_handle_t nvs_fw;
        if(nvs_open("storage", NVS_READWRITE, &nvs_fw) == ESP_OK) {
            nvs_set_str(nvs_fw, "firm_version", new_fw_version);
            nvs_commit(nvs_fw);
            nvs_close(nvs_fw);
            ESP_LOGI(TAG, "Firmware version updated in NVS: %s", new_fw_version);
        } else {
            ESP_LOGE(TAG, "Failed to open NVS to update firmware version in combined OTA");
        }
        free(firmware_url);
        esp_restart();
    } else {
        ESP_LOGE(TAG, "Firmware OTA phase failed in combined update with error: %d", ret);
    }
    free(firmware_url);
    vTaskDelete(NULL);
}

/**
 * @brief Starts the OTA update process.
 */
esp_err_t ota_start(const char *url, ota_type_t type)
{
    if (url == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    char *ota_url = strdup(url);
    if (ota_url == NULL) {
        return ESP_ERR_NO_MEM;
    }
    BaseType_t result;
    if (type == OTA_TYPE_FIRMWARE) {
        result = xTaskCreate(ota_firmware_task, "ota_firmware_task", 8192, ota_url, 3, NULL);
    } else if (type == OTA_TYPE_WEB_APP) {
        result = xTaskCreate(ota_web_app_task, "ota_web_app_task", 8192, ota_url, 3, NULL);
    } else if (type == OTA_TYPE_BOTH) {
        free(ota_url);
        return ESP_ERR_INVALID_ARG;
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

/**
 * @brief Starts the combined OTA update process for both firmware and web app.
 */
esp_err_t ota_start_both(const char *firmware_url, const char *web_app_url)
{
    if (firmware_url == NULL || web_app_url == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    ota_both_param_t *param = malloc(sizeof(ota_both_param_t));
    if (!param) {
        return ESP_ERR_NO_MEM;
    }
    param->firmware_url = strdup(firmware_url);
    param->web_app_url = strdup(web_app_url);
    if (!param->firmware_url || !param->web_app_url) {
        free(param->firmware_url);
        free(param->web_app_url);
        free(param);
        return ESP_ERR_NO_MEM;
    }
    BaseType_t result = xTaskCreate(ota_both_task, "ota_both_task", 10240, param, 3, NULL);
    if (result != pdPASS) {
        free(param->firmware_url);
        free(param->web_app_url);
        free(param);
        return ESP_FAIL;
    }
    return ESP_OK;
}
