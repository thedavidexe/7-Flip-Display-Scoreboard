/*
 * power_manager.c
 *
 * Low power mode management for the scoreboard.
 * Monitors BLE activity and enters deep sleep after prolonged inactivity
 * to conserve battery power on single 18650 cell.
 *
 * Power consumption targets:
 * - Active (BLE advertising): ~80-100mA
 * - Deep sleep: ~10uA
 *
 * With 3000mAh 18650 at 10uA deep sleep:
 * - Theoretical standby: 300,000 hours (34+ years)
 * - Practical standby (with self-discharge): 6+ months easily achievable
 *
 * SPDX-License-Identifier: MIT
 */

#include "power_manager.h"

#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nimble/nimble_port.h"
#include "74AHC595.h"

static const char *TAG = "POWER_MGR";

// ============================================================================
// Global State
// ============================================================================

// Last activity timestamp in microseconds (from esp_timer_get_time)
static volatile int64_t g_last_activity_us = 0;

// Task handle for inactivity monitor
static TaskHandle_t g_monitor_task_handle = NULL;

// ============================================================================
// Private Functions
// ============================================================================

/**
 * Get current time in microseconds since boot.
 */
static inline int64_t get_time_us(void)
{
    return esp_timer_get_time();
}

/**
 * Convert microseconds to seconds.
 */
static inline uint32_t us_to_sec(int64_t us)
{
    return (uint32_t)(us / 1000000LL);
}

/**
 * Inactivity monitoring task.
 * Runs periodically to check if the inactivity timeout has been exceeded.
 */
static void power_manager_monitor_task(void *arg)
{
    ESP_LOGI(TAG, "Inactivity monitor started (timeout=%d sec, check interval=%d sec)",
             POWER_INACTIVITY_TIMEOUT_SEC, POWER_CHECK_INTERVAL_SEC);

    for (;;) {
        // Wait for check interval
        vTaskDelay(pdMS_TO_TICKS(POWER_CHECK_INTERVAL_SEC * 1000));

        // Calculate idle time
        int64_t now_us = get_time_us();
        int64_t idle_us = now_us - g_last_activity_us;
        uint32_t idle_sec = us_to_sec(idle_us);

        // Log status periodically (every 5 minutes worth of checks)
        static uint32_t log_counter = 0;
        log_counter++;
        if (log_counter % 5 == 0) {
            ESP_LOGI(TAG, "Idle for %lu seconds (timeout at %d sec)",
                     (unsigned long)idle_sec, POWER_INACTIVITY_TIMEOUT_SEC);
        }

        // Check if timeout exceeded
        if (idle_sec >= POWER_INACTIVITY_TIMEOUT_SEC) {
            ESP_LOGW(TAG, "Inactivity timeout reached (%lu sec) - entering deep sleep",
                     (unsigned long)idle_sec);
            power_manager_enter_deep_sleep();
            // Does not return
        }
    }
}

// ============================================================================
// Public Functions
// ============================================================================

void power_manager_init(void)
{
    ESP_LOGI(TAG, "Initializing power manager");

    // Record initial activity (boot time)
    g_last_activity_us = get_time_us();

    // Create monitoring task
    BaseType_t ret = xTaskCreate(
        power_manager_monitor_task,
        "pwr_monitor",
        4096,               // Stack size (needs headroom for BLE shutdown)
        NULL,               // Task parameter
        2,                  // Low priority (background task)
        &g_monitor_task_handle
    );

    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create power monitor task");
        return;
    }

    ESP_LOGI(TAG, "Power manager initialized - will sleep after %d seconds of inactivity",
             POWER_INACTIVITY_TIMEOUT_SEC);
}

void power_manager_record_activity(void)
{
    g_last_activity_us = get_time_us();
    ESP_LOGD(TAG, "Activity recorded");
}

uint32_t power_manager_get_idle_seconds(void)
{
    int64_t now_us = get_time_us();
    int64_t idle_us = now_us - g_last_activity_us;
    return us_to_sec(idle_us);
}

void power_manager_enter_deep_sleep(void)
{
    ESP_LOGW(TAG, "Entering deep sleep mode...");

    // Display "SLEP" on the first 4 displays before sleeping
    // S=0x6D, L=0x38, E=0x79, P=0x73
    DisplaySymbol(0x6D, 0); // S
    DisplaySymbol(0x38, 1); // L
    DisplaySymbol(0x79, 2); // E
    DisplaySymbol(0x73, 3); // P

    // Stop the NimBLE stack to cleanly shut down BLE
    ESP_LOGI(TAG, "Stopping BLE stack...");
    int rc = nimble_port_stop();
    if (rc == 0) {
        nimble_port_deinit();
        ESP_LOGI(TAG, "BLE stack stopped");
    } else {
        ESP_LOGW(TAG, "Failed to stop BLE stack cleanly: %d", rc);
    }

    // Small delay to allow logs to flush
    vTaskDelay(pdMS_TO_TICKS(100));

    ESP_LOGW(TAG, "Goodbye! Press RESET button to wake up.");

    // Enter deep sleep with no wakeup source configured
    // The only way to wake up is via:
    // - EN pin (reset button)
    // - Power cycle
    // This achieves minimum power consumption (~10uA)
    esp_deep_sleep_start();

    // This line is never reached
}
