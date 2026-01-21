/*
 * power_manager.h
 *
 * Low power mode management for the scoreboard.
 * Enters deep sleep after 1 hour of BLE inactivity to conserve battery.
 * Wake up via reset button (EN pin or power cycle).
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef POWER_MANAGER_H
#define POWER_MANAGER_H

#include <stdint.h>

// ============================================================================
// Configuration
// ============================================================================

// Inactivity timeout before entering deep sleep (in seconds)
// 1 hour = 3600 seconds
#define POWER_INACTIVITY_TIMEOUT_SEC    3600

// Check interval for inactivity (in seconds)
// Check every 60 seconds to balance responsiveness and overhead
#define POWER_CHECK_INTERVAL_SEC        60

// ============================================================================
// Public Functions
// ============================================================================

/**
 * Initialize the power manager.
 * - Starts the inactivity monitoring task
 * - Records initial activity timestamp
 *
 * Call this after BLE initialization in app_main().
 */
void power_manager_init(void);

/**
 * Record BLE activity to reset the inactivity timer.
 * Call this whenever:
 * - A BLE command is received
 * - A BLE connection is established
 * - Any other BLE activity occurs
 *
 * This function is safe to call from any task context.
 */
void power_manager_record_activity(void);

/**
 * Get seconds since last activity.
 *
 * @return Number of seconds since last recorded activity
 */
uint32_t power_manager_get_idle_seconds(void);

/**
 * Manually enter deep sleep mode.
 * - Stops BLE
 * - Enters ESP32 deep sleep
 * - Device will wake on reset button press
 *
 * This function does not return.
 */
void power_manager_enter_deep_sleep(void);

#endif // POWER_MANAGER_H
