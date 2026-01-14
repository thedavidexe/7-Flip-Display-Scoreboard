/*
 * ble_scoreboard.h
 *
 * BLE GATT server for scoreboard control via iOS app.
 * Implements a custom service with a single characteristic for
 * receiving score/timer commands as 5-byte packets.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef BLE_SCOREBOARD_H
#define BLE_SCOREBOARD_H

#include <stdint.h>
#include <stdbool.h>

// ============================================================================
// Debug Logging Feature Toggle
// ============================================================================
// Set to 0 to disable BLE debug logging and reduce firmware size (~4KB)
#ifndef DEBUG_BLE_LOGGING
#define DEBUG_BLE_LOGGING 1
#endif

// ============================================================================
// BLE UUIDs (128-bit, RFC4122-compliant random UUIDs)
// ============================================================================
// Service UUID:        7B5E4A8C-2D1F-4E3B-9A6C-8F0D1E2C3B4A
// Characteristic UUID: 7B5E4A8C-2D1F-4E3B-9A6C-8F0D1E2C3B4B
//
// NimBLE expects UUIDs in little-endian byte order

#define BLE_SCOREBOARD_SERVICE_UUID_128 \
    0x4a, 0x3b, 0x2c, 0x1e, 0x0d, 0x8f, 0x6c, 0x9a, \
    0x3b, 0x4e, 0x1f, 0x2d, 0x8c, 0x4a, 0x5e, 0x7b

#define BLE_SCOREBOARD_CHAR_UUID_128 \
    0x4b, 0x3b, 0x2c, 0x1e, 0x0d, 0x8f, 0x6c, 0x9a, \
    0x3b, 0x4e, 0x1f, 0x2d, 0x8c, 0x4a, 0x5e, 0x7b

// ============================================================================
// Packet Protocol
// ============================================================================
// 5-byte packet: [blueScore, redScore, timerMinutes, timerSeconds, flags]

#define BLE_PACKET_SIZE         5
#define BLE_PACKET_BLUE_SCORE   0
#define BLE_PACKET_RED_SCORE    1
#define BLE_PACKET_TIMER_MIN    2
#define BLE_PACKET_TIMER_SEC    3
#define BLE_PACKET_FLAGS        4

// Flags byte bit definitions
#define BLE_FLAG_TIMER_UPDATE_SLOW   0x01  // bit 0: 1=10s display updates, 0=1s updates
#define BLE_FLAG_FORCE_SEGMENT_UPDATE 0x02  // bit 1: 1=force all segments to update (for reset/decrement)

// ============================================================================
// Hardware ID Generation
// ============================================================================
// Character set for 7-segment displays (26 unambiguous characters)
#define BLE_HW_ID_CHARSET       "0123456789AbCdEFHJLnoPrtUy"
#define BLE_HW_ID_CHARSET_LEN   26
#define BLE_HW_ID_LENGTH        4

// ============================================================================
// Scoreboard State
// ============================================================================
typedef struct {
    uint8_t blue_score;      // 0-99
    uint8_t red_score;       // 0-99
    uint8_t timer_minutes;   // 0-99
    uint8_t timer_seconds;   // 0-59
    bool    slow_update;     // true = 10s display update interval
    bool    timer_active;    // true when in timer countdown mode
} ble_scoreboard_state_t;

// ============================================================================
// Display Group Indices
// ============================================================================
#define BLE_DISPLAY_GROUP_BLUE  0   // Group 0 = displays 0-1 (left, blue team)
#define BLE_DISPLAY_GROUP_RED   1   // Group 1 = displays 2-3 (right, red team)

// Display update delay (ms) - matches existing FULL_DISPLAY_RESET_TIME
#define BLE_DISPLAY_UPDATE_DELAY_MS  1500

// ============================================================================
// Public Functions
// ============================================================================

/**
 * Initialize the BLE scoreboard service.
 * - Clears existing bonds
 * - Generates and displays hardware ID on 7-segment display
 * - Starts BLE advertising
 */
void ble_scoreboard_init(void);

/**
 * Clear all BLE bonds from NVS storage.
 * Called on startup to require fresh pairing.
 */
void ble_scoreboard_clear_bonds(void);

/**
 * Generate a 4-character hardware ID from the ESP32 MAC address.
 * Uses the BLE_HW_ID_CHARSET for 7-segment compatible characters.
 *
 * @param id_out Buffer to store the 4-character ID (must be at least 5 bytes for null terminator)
 */
void ble_scoreboard_generate_hardware_id(char* id_out);

/**
 * Display the hardware ID on the 7-segment displays.
 * Shows ID on all 4 digits until first BLE connection.
 */
void ble_scoreboard_display_hardware_id(void);

/**
 * Get the current scoreboard state.
 *
 * @return Pointer to the global scoreboard state
 */
const ble_scoreboard_state_t* ble_scoreboard_get_state(void);

/**
 * Check if BLE is currently connected.
 *
 * @return true if a client is connected, false otherwise
 */
bool ble_scoreboard_is_connected(void);

#endif // BLE_SCOREBOARD_H
