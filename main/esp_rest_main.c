/*
 * esp_rest_main.c
 *
 *  Created on: 21 mar 2025
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

#include "esp_rest_main.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_event.h"
#include "lwip/apps/netbiosns.h"
#include "mdns.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_system.h"
#include "esp_spiffs.h"

esp_err_t start_rest_server(const char *base_path);

#define DEFAULT_AP_SSID "7-Flip-HotSpot"
#define DEFAULT_AP_PASS "12345678"   // Default AP password (8+ chars for WPA2, empty = open network)

static void initialise_mdns(void)
{
    mdns_init();
    mdns_hostname_set(CONFIG_EXAMPLE_MDNS_HOST_NAME);
    mdns_instance_name_set(MDNS_INSTANCE);

    mdns_txt_item_t serviceTxtData[] = {
        {"board", "esp32"},
        {"path", "/"}
    };

    ESP_ERROR_CHECK(mdns_service_add("ESP32-WebServer", "_http", "_tcp", 80, serviceTxtData,
                                     sizeof(serviceTxtData) / sizeof(serviceTxtData[0])));
}

esp_err_t init_fs(void)
{
    // Read the active partition flag from NVS
    char active_www[8] = {0};
    nvs_handle_t nvs;
    if (nvs_open("storage", NVS_READONLY, &nvs) == ESP_OK) {
        size_t len = sizeof(active_www);
        if (nvs_get_str(nvs, "active_www", active_www, &len) != ESP_OK) {
            // No flag set – default to "www_0"
            strcpy(active_www, "www_0");
        }
        nvs_close(nvs);
    } else {
        // If there’s a problem with NVS, default to mounting "www_0"
        strcpy(active_www, "www_0");
    }

    ESP_LOGI(SERVER, "Mounting web partition: %s", active_www);

    esp_vfs_spiffs_conf_t conf = {
        .base_path = CONFIG_EXAMPLE_WEB_MOUNT_POINT,
        .partition_label = active_www,
        .max_files = 5,
        .format_if_mount_failed = true
    };
    esp_err_t ret = esp_vfs_spiffs_register(&conf);

    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE(SERVER, "Failed to mount or format filesystem");
        } else if (ret == ESP_ERR_NOT_FOUND) {
            ESP_LOGE(SERVER, "Failed to find SPIFFS partition");
        } else {
            ESP_LOGE(SERVER, "Failed to initialize SPIFFS (%s)", esp_err_to_name(ret));
        }
        return ESP_FAIL;
    }

    size_t total = 0, used = 0;
    ret = esp_spiffs_info(active_www, &total, &used);
    if (ret != ESP_OK) {
        ESP_LOGE(SERVER, "Failed to get SPIFFS partition information (%s)", esp_err_to_name(ret));
    } else {
        ESP_LOGI(SERVER, "Partition %s size: total: %d, used: %d", active_www, total, used);
    }
    return ESP_OK;
}

void RestfulServerTask(void *arg)
{
    ESP_LOGI(SERVER, "Initializing the server...");
    // Initialize NVS, network interfaces, and default event loop
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    initialise_mdns();
    netbiosns_init();
    netbiosns_set_name(CONFIG_EXAMPLE_MDNS_HOST_NAME);

    // Load Wi-Fi mode and credentials from NVS
    nvs_handle_t nvs;
    ESP_ERROR_CHECK(nvs_open("storage", NVS_READWRITE, &nvs));
    uint8_t mode = MODE_AP;
    esp_err_t err = nvs_get_u8(nvs, "mode", &mode);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(SERVER, "No Wi-Fi config in NVS, defaulting to Hotspot (AP) mode");
        mode = MODE_AP;
    } else if (err != ESP_OK) {
        ESP_LOGE(SERVER, "Error reading Wi-Fi mode from NVS: %s", esp_err_to_name(err));
        mode = MODE_AP;
    }
    char ssid[33] = {0};
    char password[65] = {0};
    if (mode == MODE_STA) {
        // Read stored SSID and password for Station mode
        size_t ssid_len = sizeof(ssid);
        size_t pass_len = sizeof(password);
        if (nvs_get_str(nvs, "ssid", ssid, &ssid_len) != ESP_OK ||
            nvs_get_str(nvs, "password", password, &pass_len) != ESP_OK) {
            ESP_LOGW(SERVER, "Wi-Fi credentials not found, switching to Hotspot mode");
            mode = MODE_AP;
        }
    }
    nvs_close(nvs);

    // Initialize Wi-Fi according to mode
    esp_netif_t *sta_netif = NULL;
    esp_netif_t *ap_netif  = NULL;
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    if (mode == MODE_STA) {
        sta_netif = esp_netif_create_default_wifi_sta();  // create Wi-Fi station interface
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
        // Configure Wi-Fi station with stored SSID/password
        wifi_config_t wifi_config = {};
        strlcpy((char*)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid));
        strlcpy((char*)wifi_config.sta.password, password, sizeof(wifi_config.sta.password));
        if (strlen(password) == 0) {
            // Allow connection to open network if password is empty
            wifi_config.sta.threshold.authmode = WIFI_AUTH_OPEN;
        } else {
            wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
        }
        wifi_config.sta.pmf_cfg.capable = true;
        wifi_config.sta.pmf_cfg.required = false;
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
        ESP_ERROR_CHECK(esp_wifi_start());
        ESP_ERROR_CHECK(esp_wifi_connect());

        // Wait up to 30 seconds for Wi-Fi connection (check for IP address)
        ESP_LOGI(SERVER, "Connecting to Wi-Fi: SSID=\"%s\"", ssid);
        bool connected = false;
        esp_netif_ip_info_t ip_info;
        for (int i = 0; i < 30; ++i) {
            vTaskDelay(1000 / portTICK_PERIOD_MS);
            ESP_ERROR_CHECK(esp_netif_get_ip_info(sta_netif, &ip_info));
            if (ip_info.ip.addr != 0) {
                connected = true;
                break;
            }
        }
        if (!connected) {
            ESP_LOGW(SERVER, "Failed to connect in STA mode, enabling Hotspot (AP) mode");
            ESP_ERROR_CHECK(esp_wifi_stop());
            // Switch to Access Point mode (Hotspot) after failed STA connect
            if (!ap_netif) {
                ap_netif = esp_netif_create_default_wifi_ap();  // create Wi-Fi AP interface
            }
            wifi_config_t ap_config = {};
            strlcpy((char*)ap_config.ap.ssid, DEFAULT_AP_SSID, sizeof(ap_config.ap.ssid));
            strlcpy((char*)ap_config.ap.password, DEFAULT_AP_PASS, sizeof(ap_config.ap.password));
            ap_config.ap.ssid_len = strlen(DEFAULT_AP_SSID);
            ap_config.ap.max_connection = 4;
            ap_config.ap.authmode = WIFI_AUTH_WPA_WPA2_PSK;
            if (strlen(DEFAULT_AP_PASS) == 0) {
                ap_config.ap.authmode = WIFI_AUTH_OPEN;
            }
            ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
            ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));
            ESP_ERROR_CHECK(esp_wifi_start());
            mode = MODE_AP;
            ESP_LOGI(SERVER, "Hotspot started with SSID: %s", DEFAULT_AP_SSID);
        } else {
            ESP_LOGI(SERVER, "Connected to Wi-Fi network, IP: " IPSTR, IP2STR(&ip_info.ip));
        }
    } else {
        // Start in Access Point mode (Hotspot) by default
        ap_netif = esp_netif_create_default_wifi_ap();
        wifi_config_t ap_config = {};
        strlcpy((char*)ap_config.ap.ssid, DEFAULT_AP_SSID, sizeof(ap_config.ap.ssid));
        strlcpy((char*)ap_config.ap.password, DEFAULT_AP_PASS, sizeof(ap_config.ap.password));
        ap_config.ap.ssid_len = strlen(DEFAULT_AP_SSID);
        ap_config.ap.max_connection = 4;
        ap_config.ap.authmode = WIFI_AUTH_WPA_WPA2_PSK;
        if (strlen(DEFAULT_AP_PASS) == 0) {
            ap_config.ap.authmode = WIFI_AUTH_OPEN;
        }
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));
        ESP_ERROR_CHECK(esp_wifi_start());
        ESP_LOGI(SERVER, "Hotspot started with SSID: %s", DEFAULT_AP_SSID);
    }

    // Initialize SPIFFS and start the RESTful web server (serving Vue app and API)
    ESP_ERROR_CHECK(init_fs());
    ESP_ERROR_CHECK(start_rest_server(CONFIG_EXAMPLE_WEB_MOUNT_POINT));
    ESP_LOGI(SERVER, "Server started");

    vTaskDelete(NULL);
}
