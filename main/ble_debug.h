/*
 * ble_debug.h
 *
 * BLE debug logging characteristic for streaming thermal, memory,
 * and task data to connected iOS app.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef BLE_DEBUG_H
#define BLE_DEBUG_H

#include <stdint.h>
#include <stdbool.h>

struct ble_gatt_access_ctxt;

// ============================================================================
// Feature Toggle
// ============================================================================
// Set to 0 to disable debug logging and reduce firmware size
#ifndef DEBUG_BLE_LOGGING
#define DEBUG_BLE_LOGGING 1
#endif

#if DEBUG_BLE_LOGGING

// ============================================================================
// BLE Debug Characteristic UUID
// ============================================================================
// UUID: 7B5E4A8C-2D1F-4E3B-9A6C-8F0D1E2C3B4C (ends in 4C)
// NimBLE expects UUIDs in little-endian byte order
#define BLE_DEBUG_CHAR_UUID_128 \
    0x4c, 0x3b, 0x2c, 0x1e, 0x0d, 0x8f, 0x6c, 0x9a, \
    0x3b, 0x4e, 0x1f, 0x2d, 0x8c, 0x4a, 0x5e, 0x7b

// ============================================================================
// Debug Packet Protocol
// ============================================================================
// 20-byte packet structure:
// [0]     Packet type (0x04 = full status)
// [1]     Sequence number (0-255, wraps)
// [2-3]   Uptime seconds (uint16_t, little-endian)
// [4-7]   Temperature (float, little-endian, Celsius)
// [8-11]  Free heap (uint32_t, little-endian)
// [12-15] Min free heap (uint32_t, little-endian)
// [16]    RSSI (int8_t, dBm)
// [17]    Task count (uint8_t)
// [18-19] Reserved

#define DEBUG_PACKET_SIZE       20
#define DEBUG_PKT_TYPE_FULL     0x04

// Debug update interval in milliseconds
#define DEBUG_UPDATE_INTERVAL_MS 5000

// ============================================================================
// Debug Status Structure
// ============================================================================
typedef struct {
    float    temperature;     // ESP32 internal temperature (Celsius)
    uint32_t free_heap;       // Current free heap bytes
    uint32_t min_heap;        // Minimum free heap since boot
    int8_t   rssi;            // BLE connection RSSI (dBm)
    uint8_t  task_count;      // Number of FreeRTOS tasks
    uint32_t uptime_sec;      // Seconds since boot
} ble_debug_status_t;

// ============================================================================
// Public Functions
// ============================================================================

/**
 * Initialize the debug logging subsystem.
 * Initializes temperature sensor and prepares debug characteristic.
 * Call this after BLE stack is initialized.
 */
void ble_debug_init(void);

/**
 * Start the debug logging task.
 * Called when a client subscribes to debug notifications.
 */
void ble_debug_start(void);

/**
 * Stop the debug logging task.
 * Called when client unsubscribes or disconnects.
 */
void ble_debug_stop(void);

/**
 * Check if debug notifications are currently enabled.
 *
 * @return true if a client is subscribed to debug notifications
 */
bool ble_debug_is_subscribed(void);

/**
 * Set the subscription state (called from GATT subscribe event).
 *
 * @param subscribed true if client subscribed, false if unsubscribed
 */
void ble_debug_set_subscribed(bool subscribed);

/**
 * Get current debug status snapshot.
 *
 * @param status Pointer to structure to fill with current status
 */
void ble_debug_get_status(ble_debug_status_t *status);

/**
 * GATT access callback for debug characteristic.
 * Handles subscription requests.
 */
int ble_debug_gatt_access(uint16_t conn_handle, uint16_t attr_handle,
                          struct ble_gatt_access_ctxt *ctxt, void *arg);

/**
 * Debug characteristic value handle.
 * Used by ble_scoreboard.c to set up the GATT service.
 */
extern uint16_t g_debug_char_val_handle;

#endif // DEBUG_BLE_LOGGING
#endif // BLE_DEBUG_H
