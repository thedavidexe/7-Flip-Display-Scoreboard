/*
 * mqtt_com.c
 *
 *  Created on: 22 mar 2025
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
#include "mqtt_client.h"    // Use your mqtt_client.h as available in your ESP-IDF version
#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h> // for snprintf
#include "74AHC595.h"

extern status_t status;
static const char *MQTT_TAG = "MQTT";

///* Hardcoded MQTT topics */
//static const char *hardcoded_topics[] = {
//    "mqtt-get-data",
//    "number"
//    // Add more topics as needed
//};
//static const int num_hardcoded_topics = sizeof(hardcoded_topics) / sizeof(hardcoded_topics[0]);

/** MQTT client handle */
static esp_mqtt_client_handle_t client = NULL;
/** Current MQTT connection status */
static bool mqtt_connected = false;
/** Task handle for MQTT FreeRTOS task */
TaskHandle_t mqtt_task_handle = NULL;

/** Maximum lengths for configuration strings */
#define MQTT_MAX_BROKER_LEN   100   // Maximum broker address length
#define MQTT_MAX_USER_LEN     64    // Maximum username length
#define MQTT_MAX_PASS_LEN     64    // Maximum password length
#define MQTT_MAX_TOPICS_LEN   256   // Maximum length for comma-separated topics

/** Structure holding MQTT configuration loaded from NVS */
typedef struct {
    bool     enabled;
    char     broker[MQTT_MAX_BROKER_LEN + 1];
    uint16_t port;
    char     username[MQTT_MAX_USER_LEN + 1];
    char     password[MQTT_MAX_PASS_LEN + 1];
    char     topics[MQTT_MAX_TOPICS_LEN + 1];
} mqtt_config_t;

static mqtt_config_t mqtt_cfg;

/**
 * Load MQTT configuration from NVS into mqtt_cfg.
 * If keys are missing, default values are used.
 */
static void mqtt_load_config_from_nvs(void) {
    nvs_handle_t nvs;
    esp_err_t err = nvs_open("storage", NVS_READONLY, &nvs);
    if (err != ESP_OK) {
        ESP_LOGE(MQTT_TAG, "Failed to open NVS for MQTT config (err=0x%x)", err);
        mqtt_cfg.enabled = false;
        mqtt_cfg.broker[0] = '\0';
        mqtt_cfg.port = 1883;
        mqtt_cfg.username[0] = '\0';
        mqtt_cfg.password[0] = '\0';
        strcpy(mqtt_cfg.topics, "mqtt-get-data");
        return;
    }
    uint8_t enabled_u8 = 0;
    err = nvs_get_u8(nvs, "mqtt_en", &enabled_u8);
    if (err == ESP_OK) {
        mqtt_cfg.enabled = (enabled_u8 != 0);
    } else {
        ESP_LOGW(MQTT_TAG, "MQTT enable flag not found in NVS, defaulting to disabled");
        mqtt_cfg.enabled = false;
    }
    size_t len = sizeof(mqtt_cfg.broker);
    err = nvs_get_str(nvs, "mqtt_host", mqtt_cfg.broker, &len);
    if (err != ESP_OK) {
        mqtt_cfg.broker[0] = '\0';
        ESP_LOGW(MQTT_TAG, "MQTT broker address not found, using empty string");
    }
    uint16_t port = 1883;
    err = nvs_get_u16(nvs, "mqtt_port", &port);
    if (err != ESP_OK) {
        ESP_LOGW(MQTT_TAG, "MQTT port not found, defaulting to 1883");
    }
    mqtt_cfg.port = port;
    len = sizeof(mqtt_cfg.username);
    err = nvs_get_str(nvs, "mqtt_user", mqtt_cfg.username, &len);
    if (err != ESP_OK) {
        mqtt_cfg.username[0] = '\0';
    }
    len = sizeof(mqtt_cfg.password);
    err = nvs_get_str(nvs, "mqtt_pass", mqtt_cfg.password, &len);
    if (err != ESP_OK) {
        mqtt_cfg.password[0] = '\0';
    }
}

/**
 * MQTT event handler callback.
 * This function is called by the ESP-MQTT library upon events such as connection,
 * disconnection, and data reception.
 */
static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {
    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;
    switch (event->event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(MQTT_TAG, "Connected to MQTT broker");
            mqtt_connected = true;
//            // Subscribe to all topics from Hardcoded MQTT topics
//			for (int i = 0; i < num_hardcoded_topics; i++) {
//			    esp_mqtt_client_subscribe(client, hardcoded_topics[i], 1);
//			    ESP_LOGI(MQTT_TAG, "Subscribed to hardcoded topic: %s", hardcoded_topics[i]);
//			}
			    esp_mqtt_client_subscribe(client, "#", 1);
    			ESP_LOGI(MQTT_TAG, "Subscribed to all topics (#)");
            break;
        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGW(MQTT_TAG, "Disconnected from MQTT broker");
            mqtt_connected = false;
            break;
        case MQTT_EVENT_DATA: {
            char topic_buf[MQTT_MAX_TOPICS_LEN + 1];
            char data_buf[128];
            int tlen = (event->topic_len < MQTT_MAX_TOPICS_LEN) ? event->topic_len : MQTT_MAX_TOPICS_LEN;
            int dlen = (event->data_len < (int)sizeof(data_buf) - 1) ? event->data_len : (int)sizeof(data_buf) - 1;
            memcpy(topic_buf, event->topic, tlen);
            topic_buf[tlen] = '\0';
            memcpy(data_buf, event->data, dlen);
            data_buf[dlen] = '\0';
            ESP_LOGI(MQTT_TAG, "Received MQTT data on topic '%s': %s", topic_buf, data_buf);
            
            /* Check if any of the groups has MQTT mode selected */
			for(uint8_t i = 0; i < status.display_number; i++)
			{
				if(status.groups[i].mode == MODE_MQTT)
				{
					if(strcmp(topic_buf, status.groups[i].mqtt.topic) == 0) 
		            {
					    DisplayNumber((uint32_t)strtoul(data_buf, NULL, 10), i);
					}
				}
			}
            
            break;
        }
        case MQTT_EVENT_ERROR:
            ESP_LOGE(MQTT_TAG, "MQTT error occurred (event id %d)", event->event_id);
            break;
        case MQTT_EVENT_SUBSCRIBED:
            ESP_LOGI(MQTT_TAG, "Subscription acknowledged (msg_id=%d)", event->msg_id);
            break;
        case MQTT_EVENT_PUBLISHED:
            // Optionally log published message acknowledgements here.
            break;
        default:
            break;
    }
}

/**
 * Publish a message to the MQTT broker on the given topic.
 * Publishes only if MQTT is enabled and connected.
 *
 * @param topic   Topic to publish to.
 * @param payload Message payload (UTF-8 string).
 */
void mqtt_publish(const char *topic, const char *payload) {
    if (!mqtt_cfg.enabled || !mqtt_connected || client == NULL) {
        ESP_LOGW(MQTT_TAG, "Cannot publish: MQTT is disabled or not connected");
        return;
    }
    esp_err_t err = esp_mqtt_client_publish(client, topic, payload, 0, 1, 0);
    if (err == -1) {
        ESP_LOGE(MQTT_TAG, "Failed to publish message on topic %s", topic);
    }
}

/**
 * FreeRTOS task that manages the MQTT connection and subscriptions.
 * If MQTT is enabled and a broker is configured, the task initializes the MQTT client
 * using individual fields (hostname, port, credentials) rather than a complete URI.
 * When a configuration change is notified (via xTaskNotifyGive), the task stops
 * the current client, reloads configuration, and reconnects.
 */
void mqtt_task(void *pvParameters) {
    mqtt_task_handle = xTaskGetCurrentTaskHandle();
    mqtt_load_config_from_nvs();
    if (!mqtt_cfg.enabled) {
        ESP_LOGI(MQTT_TAG, "MQTT is disabled in configuration");
    }
    while (1) {
        if (mqtt_cfg.enabled && mqtt_cfg.broker[0] != '\0') {
            ESP_LOGI(MQTT_TAG, "Initializing MQTT client for broker: %s, port: %u", mqtt_cfg.broker, mqtt_cfg.port);
            esp_mqtt_client_config_t mqtt_client_cfg = {
                .broker = {
                    .address = {
                        .hostname = mqtt_cfg.broker,
                        .port = mqtt_cfg.port,
                        .transport = MQTT_TRANSPORT_OVER_TCP,
                        .uri = NULL,   // Do not use URI field to avoid conflicts
                        .path = NULL,
                    },
                },
                .credentials = {
                    .username = mqtt_cfg.username,
                    .client_id = NULL,       // Auto-generated client ID if NULL
                    .set_null_client_id = false,
                    .authentication = {
                        .password = mqtt_cfg.password,
                    },
                },
                .session = {
                    .protocol_ver = MQTT_PROTOCOL_V_5,
                },
            };

            client = esp_mqtt_client_init(&mqtt_client_cfg);
            if (client == NULL) {
                ESP_LOGE(MQTT_TAG, "Failed to initialize MQTT client");
            } else {
                esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
                esp_err_t err = esp_mqtt_client_start(client);
                if (err != ESP_OK) {
                    ESP_LOGE(MQTT_TAG, "MQTT client start failed: %s", esp_err_to_name(err));
                } else {
                    ESP_LOGI(MQTT_TAG, "MQTT client started (connecting to broker)");
                }
            }
        } else {
            if (!mqtt_cfg.enabled) {
                ESP_LOGI(MQTT_TAG, "MQTT disabled in configuration; waiting for update");
            } else if (mqtt_cfg.broker[0] == '\0') {
                ESP_LOGE(MQTT_TAG, "MQTT enabled but broker address is empty");
            }
        }
        // Wait indefinitely until notified of a configuration change (e.g., from REST endpoint)
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        if (client != NULL) {
            esp_mqtt_client_stop(client);
            esp_mqtt_client_destroy(client);
            client = NULL;
        }
        mqtt_connected = false;
        mqtt_load_config_from_nvs();
    }
}