/*
 * esp_rest_main.c
 *
 *  Created on: 21 mar 2025
 *      Author: Sebastian Sokołowski
 *      Company: Smart Solutions for Home
 *
 * SPDX-License-Identifier: MIT
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
#include <string.h>
#include "led.h"

//#define PROGRAMMED_FROM_IDE
#define REST_TAG "REST"

static void init_version_info() {
    nvs_handle_t nvs;
    esp_err_t err = nvs_open("storage", NVS_READWRITE, &nvs);
    if (err == ESP_OK) {
        size_t len;
        char version_str[32] = {0};
        
        // Check web_app_version
        len = sizeof(version_str);
        err = nvs_get_str(nvs, "web_app_version", version_str, &len);
        if(err != ESP_OK) {
            ESP_LOGI("INIT", "web_app_version not found, setting default to 0.0.0 (error=%d)", err);
            nvs_set_str(nvs, "web_app_version", "0.0.0");
        } else {
            ESP_LOGI("INIT", "web_app_version found: %s", version_str);
        }
        
        // Check firm_version
        len = sizeof(version_str);
        err = nvs_get_str(nvs, "firm_version", version_str, &len);
        if(err != ESP_OK) {
            ESP_LOGI("INIT", "firm_version not found, setting default to 0.0.0 (error=%d)", err);
            nvs_set_str(nvs, "firm_version", "0.0.0");
        } else {
            ESP_LOGI("INIT", "firm_version found: %s", version_str);
        }
            
        
        nvs_commit(nvs);
        nvs_close(nvs);
    } else {
        ESP_LOGE("INIT", "Failed to open NVS for version initialization");
    }
}

esp_err_t start_rest_server(const char *base_path);

#define DEFAULT_AP_SSID "7-Flip-HotSpot"
#define DEFAULT_AP_PASS "12345678"   // Default AP password

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
        strcpy(active_www, "www_0");
    }

    ESP_LOGI(SERVER, "Mounting web partition: %s", active_www);

	static esp_err_t ret;
	
	if(strcmp(active_www, "www_1") == 0)
	{
	    esp_vfs_spiffs_conf_t conf = {
	        .base_path = CONFIG_WWW_1_MOUNT_POINT,
	        .partition_label = active_www,
	        .max_files = 5,
	        .format_if_mount_failed = true
	    };
	     ret = esp_vfs_spiffs_register(&conf);
    } else {
	    esp_vfs_spiffs_conf_t conf = {
	        .base_path = CONFIG_WWW_0_MOUNT_POINT,
	        .partition_label = active_www,
	        .max_files = 5,
	        .format_if_mount_failed = true
	    };
	     ret = esp_vfs_spiffs_register(&conf);
    }
   
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

// Force active web partition to "www_0" if it is set to "www_1"
// This should be executed only once after flashing from the IDE.
void SetDefaultPartition(void)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open("storage", NVS_READWRITE, &nvs);
    if (err == ESP_OK) {
        char active_www[8] = {0};
        size_t len = sizeof(active_www);
        // Try to read the active_www value from NVS
        if (nvs_get_str(nvs, "active_www", active_www, &len) == ESP_OK) {
            // If the value is "www_1", force it to "www_0"
            if (strcmp(active_www, "www_1") == 0) {
                ESP_LOGI(SERVER, "Forcing active web partition to www_0 after flash");
                nvs_set_str(nvs, "active_www", "www_0");
                nvs_commit(nvs);
            }
        } else {
            // If key not found, set default to "www_0"
            nvs_set_str(nvs, "active_www", "www_0");
            nvs_commit(nvs);
        }
        nvs_close(nvs);
    } else {
        ESP_LOGE(SERVER, "Failed to open NVS for active_www check");
    }	
}

void RestfulServerTask(void *arg)
{
    ESP_LOGI(SERVER, "Initializing the server...");
    
    // Initialize NVS, network interfaces, and default event loop
    ESP_ERROR_CHECK(nvs_flash_init());

#ifdef PROGRAMMED_FROM_IDE    
    SetDefaultPartition();
#endif
    
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // Initialize version information in NVS if not already set
    init_version_info();

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
        sta_netif = esp_netif_create_default_wifi_sta();
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
        wifi_config_t wifi_config = {};
        strlcpy((char*)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid));
        strlcpy((char*)wifi_config.sta.password, password, sizeof(wifi_config.sta.password));
        if (strlen(password) == 0) {
            wifi_config.sta.threshold.authmode = WIFI_AUTH_OPEN;
        } else {
            wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
        }
        wifi_config.sta.pmf_cfg.capable = true;
        wifi_config.sta.pmf_cfg.required = false;
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
        ESP_ERROR_CHECK(esp_wifi_start());
        ESP_ERROR_CHECK(esp_wifi_connect());

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
            if (!ap_netif) {
                ap_netif = esp_netif_create_default_wifi_ap();
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

    ESP_ERROR_CHECK(init_fs());
    
	char active_www[8] = {0};
	
	if (nvs_open("storage", NVS_READONLY, &nvs) == ESP_OK) {
	    size_t len = sizeof(active_www);
	    if (nvs_get_str(nvs, "active_www", active_www, &len) != ESP_OK) {
	        strcpy(active_www, "www_0");
	    }
	    nvs_close(nvs);
	} else {
	    strcpy(active_www, "www_0");
	}
	
	if (strcmp(active_www, "www_0") == 0) {
	    ESP_ERROR_CHECK(start_rest_server(CONFIG_WWW_0_MOUNT_POINT));
	} else {
	    ESP_ERROR_CHECK(start_rest_server(CONFIG_WWW_1_MOUNT_POINT));
	}

    ESP_LOGI(SERVER, "Server started");
    
    LED_set_color(RED, 1);

    vTaskDelete(NULL);
}
