/*
 * ble_debug.c
 *
 * BLE debug logging implementation for streaming thermal, memory,
 * and task data to connected iOS app.
 *
 * SPDX-License-Identifier: MIT
 */

#include "ble_debug.h"

#if DEBUG_BLE_LOGGING

#include "esp_log.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/temperature_sensor.h"
#include "host/ble_hs.h"
#include "host/ble_gap.h"

static const char *TAG = "BLE_DEBUG";

// ============================================================================
// Global State
// ============================================================================
static bool g_subscribed = false;
static TaskHandle_t g_debug_task_handle = NULL;
static temperature_sensor_handle_t g_temp_sensor = NULL;
static uint8_t g_sequence = 0;
static uint16_t g_debug_char_val_handle = 0;

// Connection handle from ble_scoreboard.c
extern bool ble_scoreboard_is_connected(void);

// ============================================================================
// Temperature Sensor Initialization
// ============================================================================
static void ble_debug_init_temp_sensor(void)
{
    temperature_sensor_config_t temp_config = TEMPERATURE_SENSOR_CONFIG_DEFAULT(-10, 80);

    esp_err_t ret = temperature_sensor_install(&temp_config, &g_temp_sensor);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to install temperature sensor: %s", esp_err_to_name(ret));
        g_temp_sensor = NULL;
        return;
    }

    ret = temperature_sensor_enable(g_temp_sensor);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to enable temperature sensor: %s", esp_err_to_name(ret));
        temperature_sensor_uninstall(g_temp_sensor);
        g_temp_sensor = NULL;
        return;
    }

    ESP_LOGI(TAG, "Temperature sensor initialized");
}

// ============================================================================
// Debug Status Collection
// ============================================================================
void ble_debug_get_status(ble_debug_status_t *status)
{
    // Get temperature
    if (g_temp_sensor != NULL) {
        float temp = 0;
        if (temperature_sensor_get_celsius(g_temp_sensor, &temp) == ESP_OK) {
            status->temperature = temp;
        } else {
            status->temperature = -999.0f;  // Error indicator
        }
    } else {
        status->temperature = -999.0f;  // Sensor not available
    }

    // Get heap info
    status->free_heap = esp_get_free_heap_size();
    status->min_heap = esp_get_minimum_free_heap_size();

    // Get task count
    status->task_count = (uint8_t)uxTaskGetNumberOfTasks();

    // Get uptime
    status->uptime_sec = (uint32_t)(esp_timer_get_time() / 1000000);

    // RSSI - would need connection handle, set to 0 for now
    // Could be enhanced to query actual RSSI via ble_gap_conn_rssi()
    status->rssi = 0;
}

// ============================================================================
// Debug Packet Building
// ============================================================================
static void ble_debug_build_packet(uint8_t *packet, const ble_debug_status_t *status)
{
    // Clear packet
    memset(packet, 0, DEBUG_PACKET_SIZE);

    // Byte 0: Packet type
    packet[0] = DEBUG_PKT_TYPE_FULL;

    // Byte 1: Sequence number
    packet[1] = g_sequence++;

    // Bytes 2-3: Uptime (uint16_t, little-endian, wraps at 65535)
    uint16_t uptime16 = (uint16_t)(status->uptime_sec & 0xFFFF);
    packet[2] = uptime16 & 0xFF;
    packet[3] = (uptime16 >> 8) & 0xFF;

    // Bytes 4-7: Temperature (float, little-endian)
    memcpy(&packet[4], &status->temperature, sizeof(float));

    // Bytes 8-11: Free heap (uint32_t, little-endian)
    packet[8]  = status->free_heap & 0xFF;
    packet[9]  = (status->free_heap >> 8) & 0xFF;
    packet[10] = (status->free_heap >> 16) & 0xFF;
    packet[11] = (status->free_heap >> 24) & 0xFF;

    // Bytes 12-15: Min heap (uint32_t, little-endian)
    packet[12] = status->min_heap & 0xFF;
    packet[13] = (status->min_heap >> 8) & 0xFF;
    packet[14] = (status->min_heap >> 16) & 0xFF;
    packet[15] = (status->min_heap >> 24) & 0xFF;

    // Byte 16: RSSI (int8_t)
    packet[16] = (uint8_t)status->rssi;

    // Byte 17: Task count
    packet[17] = status->task_count;

    // Bytes 18-19: Reserved (already zeroed)
}

// ============================================================================
// Debug Task
// ============================================================================
static void ble_debug_task(void *arg)
{
    ESP_LOGI(TAG, "Debug logging task started");

    ble_debug_status_t status;
    uint8_t packet[DEBUG_PACKET_SIZE];

    while (g_subscribed && ble_scoreboard_is_connected()) {
        // Collect status
        ble_debug_get_status(&status);

        // Build packet
        ble_debug_build_packet(packet, &status);

        // Log locally for debugging
        ESP_LOGI(TAG, "Debug: T=%.1fC, Heap=%lu (min=%lu), Tasks=%d, Up=%lus",
                 status.temperature,
                 (unsigned long)status.free_heap,
                 (unsigned long)status.min_heap,
                 status.task_count,
                 (unsigned long)status.uptime_sec);

        // Send notification
        struct os_mbuf *om = ble_hs_mbuf_from_flat(packet, DEBUG_PACKET_SIZE);
        if (om != NULL) {
            // Get connection handle - assume single connection
            uint16_t conn_handle = 0;
            struct ble_gap_conn_desc desc;
            int rc = ble_gap_conn_find_by_addr(NULL, &desc);
            if (rc == 0) {
                conn_handle = desc.conn_handle;
            }

            if (conn_handle != 0) {
                rc = ble_gatts_notify_custom(conn_handle, g_debug_char_val_handle, om);
                if (rc != 0) {
                    ESP_LOGW(TAG, "Failed to send debug notification: %d", rc);
                }
            } else {
                os_mbuf_free_chain(om);
            }
        }

        // Wait for next interval
        vTaskDelay(pdMS_TO_TICKS(DEBUG_UPDATE_INTERVAL_MS));
    }

    ESP_LOGI(TAG, "Debug logging task stopped");
    g_debug_task_handle = NULL;
    vTaskDelete(NULL);
}

// ============================================================================
// GATT Access Callback
// ============================================================================
int ble_debug_gatt_access(uint16_t conn_handle, uint16_t attr_handle,
                          struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    // Debug characteristic is notify-only, no read/write needed
    return BLE_ATT_ERR_UNLIKELY;
}

// ============================================================================
// Public Functions
// ============================================================================
void ble_debug_init(void)
{
    ESP_LOGI(TAG, "Initializing BLE debug logging");

    // Initialize temperature sensor
    ble_debug_init_temp_sensor();

    g_subscribed = false;
    g_sequence = 0;

    ESP_LOGI(TAG, "BLE debug logging initialized");
}

void ble_debug_start(void)
{
    if (g_debug_task_handle != NULL) {
        ESP_LOGW(TAG, "Debug task already running");
        return;
    }

    ESP_LOGI(TAG, "Starting debug logging task");
    xTaskCreate(ble_debug_task, "ble_debug", 3072, NULL, 4, &g_debug_task_handle);
}

void ble_debug_stop(void)
{
    if (g_debug_task_handle == NULL) {
        return;
    }

    ESP_LOGI(TAG, "Stopping debug logging task");
    g_subscribed = false;

    // Task will exit on its own when it sees g_subscribed = false
    // Wait a bit for clean exit
    vTaskDelay(pdMS_TO_TICKS(100));

    if (g_debug_task_handle != NULL) {
        vTaskDelete(g_debug_task_handle);
        g_debug_task_handle = NULL;
    }
}

bool ble_debug_is_subscribed(void)
{
    return g_subscribed;
}

void ble_debug_set_subscribed(bool subscribed)
{
    bool was_subscribed = g_subscribed;
    g_subscribed = subscribed;

    ESP_LOGI(TAG, "Debug subscription changed: %d -> %d", was_subscribed, subscribed);

    if (subscribed && !was_subscribed) {
        ble_debug_start();
    } else if (!subscribed && was_subscribed) {
        ble_debug_stop();
    }
}

uint16_t* ble_debug_get_val_handle(void)
{
    return &g_debug_char_val_handle;
}

#endif // DEBUG_BLE_LOGGING
