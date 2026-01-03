/*
 * ble_scoreboard.c
 *
 * BLE GATT server implementation for scoreboard control.
 * Uses NimBLE stack for Bluetooth Low Energy communication.
 *
 * SPDX-License-Identifier: MIT
 */

#include "ble_scoreboard.h"
#include "main.h"
#include "74AHC595.h"

#include "esp_log.h"
#include "esp_mac.h"
#include "nvs_flash.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "host/ble_gap.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

static const char *TAG = "BLE_SCOREBOARD";

// ============================================================================
// Global State
// ============================================================================
static ble_scoreboard_state_t g_state = {0};
static uint16_t g_conn_handle = BLE_HS_CONN_HANDLE_NONE;
static uint8_t g_own_addr_type;
static char g_hardware_id[BLE_HW_ID_LENGTH + 1] = {0};
static bool g_first_connection = false;

// Timer task handle for countdown
static TaskHandle_t g_timer_task_handle = NULL;

// External status for display control
extern status_t status;

// ============================================================================
// 7-Segment Patterns for Hardware ID Characters
// ============================================================================
// Segment bits: bit0=A, bit1=B, bit2=C, bit3=D, bit4=E, bit5=F, bit6=G
// Charset: "0123456789AbCdEFHJLnoPrtUy" (26 characters)
static const uint8_t g_hw_id_patterns[BLE_HW_ID_CHARSET_LEN] = {
    0x3F,  // 0: A B C D E F
    0x06,  // 1:   B C
    0x5B,  // 2: A B   D E   G
    0x4F,  // 3: A B C D     G
    0x66,  // 4:   B C     F G
    0x6D,  // 5: A   C D   F G
    0x7D,  // 6: A   C D E F G
    0x07,  // 7: A B C
    0x7F,  // 8: A B C D E F G
    0x6F,  // 9: A B C D   F G
    0x77,  // A: A B C   E F G
    0x7C,  // b:     C D E F G
    0x39,  // C: A     D E F
    0x5E,  // d:   B C D E   G
    0x79,  // E: A     D E F G
    0x71,  // F: A       E F G
    0x76,  // H:   B C   E F G
    0x1E,  // J:   B C D E
    0x38,  // L:       D E F
    0x54,  // n:     C   E   G
    0x5C,  // o:     C D E   G
    0x73,  // P: A B     E F G
    0x50,  // r:         E   G
    0x78,  // t:       D E F G
    0x3E,  // U:   B C D E F
    0x6E,  // y:   B C D   F G
};

// ============================================================================
// Forward Declarations
// ============================================================================
static void ble_scoreboard_advertise(void);
static void ble_scoreboard_on_sync(void);
static void ble_scoreboard_on_reset(int reason);
static int ble_scoreboard_gap_event(struct ble_gap_event *event, void *arg);
static int ble_scoreboard_gatt_access(uint16_t conn_handle, uint16_t attr_handle,
                                       struct ble_gatt_access_ctxt *ctxt, void *arg);
static void ble_scoreboard_enter_score_mode(void);
static void ble_scoreboard_enter_timer_mode(void);
static void ble_scoreboard_timer_task(void *arg);
static void ble_host_task(void *param);

// ============================================================================
// GATT Service Definition
// ============================================================================
static const ble_uuid128_t g_service_uuid =
    BLE_UUID128_INIT(BLE_SCOREBOARD_SERVICE_UUID_128);

static const ble_uuid128_t g_char_uuid =
    BLE_UUID128_INIT(BLE_SCOREBOARD_CHAR_UUID_128);

static uint16_t g_char_val_handle;

static const struct ble_gatt_svc_def g_gatt_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &g_service_uuid.u,
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                .uuid = &g_char_uuid.u,
                .access_cb = ble_scoreboard_gatt_access,
                .val_handle = &g_char_val_handle,
                .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_INDICATE,
            },
            { 0 }  // End of characteristics
        },
    },
    { 0 }  // End of services
};

// ============================================================================
// Hardware ID Functions
// ============================================================================
void ble_scoreboard_generate_hardware_id(char *id_out)
{
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_BT);

    // Use last 3 bytes of MAC for uniqueness
    uint32_t hash_value = ((uint32_t)mac[3] << 16) |
                          ((uint32_t)mac[4] << 8) |
                          mac[5];

    for (int i = 0; i < BLE_HW_ID_LENGTH; i++) {
        id_out[i] = BLE_HW_ID_CHARSET[hash_value % BLE_HW_ID_CHARSET_LEN];
        hash_value /= BLE_HW_ID_CHARSET_LEN;
    }
    id_out[BLE_HW_ID_LENGTH] = '\0';

    ESP_LOGI(TAG, "Generated Hardware ID: %s", id_out);
}

void ble_scoreboard_display_hardware_id(void)
{
    ESP_LOGI(TAG, "Displaying Hardware ID: %s", g_hardware_id);

    // Map each character to its pattern and display
    for (int i = 0; i < BLE_HW_ID_LENGTH && i < status.display_number; i++) {
        char c = g_hardware_id[i];

        // Find character in charset
        const char *pos = strchr(BLE_HW_ID_CHARSET, c);
        if (pos != NULL) {
            int idx = pos - BLE_HW_ID_CHARSET;
            uint8_t pattern = g_hw_id_patterns[idx];
            DisplaySymbol(pattern, i);
        }
    }
}

// ============================================================================
// Bond Management
// ============================================================================
void ble_scoreboard_clear_bonds(void)
{
    int rc;
    int num_bonds = 0;

    // Get number of bonded peers
    ble_store_util_count(BLE_STORE_OBJ_TYPE_OUR_SEC, &num_bonds);

    if (num_bonds > 0) {
        // Clear all stored bonds
        rc = ble_store_clear();
        if (rc != 0) {
            ESP_LOGW(TAG, "Failed to clear bonds: %d", rc);
        } else {
            ESP_LOGI(TAG, "Cleared %d BLE bond(s)", num_bonds);
        }
    } else {
        ESP_LOGI(TAG, "No existing bonds to clear");
    }
}

// ============================================================================
// BLE Advertising
// ============================================================================
static void ble_scoreboard_advertise(void)
{
    struct ble_gap_adv_params adv_params;
    struct ble_hs_adv_fields fields;
    int rc;

    memset(&fields, 0, sizeof(fields));

    // Advertise flags
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;

    // Include TX power level
    fields.tx_pwr_lvl_is_present = 1;
    fields.tx_pwr_lvl = BLE_HS_ADV_TX_PWR_LVL_AUTO;

    // Include complete local name
    char device_name[32];
    snprintf(device_name, sizeof(device_name), "Scoreboard %s", g_hardware_id);
    fields.name = (uint8_t *)device_name;
    fields.name_len = strlen(device_name);
    fields.name_is_complete = 1;

    rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "Error setting advertisement data: rc=%d", rc);
        return;
    }

    // Set scan response data with service UUID
    struct ble_hs_adv_fields rsp_fields;
    memset(&rsp_fields, 0, sizeof(rsp_fields));

    rsp_fields.uuids128 = (ble_uuid128_t[]){ g_service_uuid };
    rsp_fields.num_uuids128 = 1;
    rsp_fields.uuids128_is_complete = 1;

    rc = ble_gap_adv_rsp_set_fields(&rsp_fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "Error setting scan response data: rc=%d", rc);
        return;
    }

    // Configure advertising parameters
    memset(&adv_params, 0, sizeof(adv_params));
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    adv_params.itvl_min = BLE_GAP_ADV_ITVL_MS(100);
    adv_params.itvl_max = BLE_GAP_ADV_ITVL_MS(150);

    // Start advertising
    rc = ble_gap_adv_start(g_own_addr_type, NULL, BLE_HS_FOREVER,
                           &adv_params, ble_scoreboard_gap_event, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "Error starting advertisement: rc=%d", rc);
        return;
    }

    ESP_LOGI(TAG, "BLE advertising started as '%s'", device_name);
}

// ============================================================================
// GAP Event Handler
// ============================================================================
static int ble_scoreboard_gap_event(struct ble_gap_event *event, void *arg)
{
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        ESP_LOGI(TAG, "Connection %s; status=%d",
                 event->connect.status == 0 ? "established" : "failed",
                 event->connect.status);

        if (event->connect.status == 0) {
            g_conn_handle = event->connect.conn_handle;

            // First connection - send initial state
            if (!g_first_connection) {
                g_first_connection = true;
                ESP_LOGI(TAG, "First connection - ready for commands");
            }
        } else {
            // Connection failed, restart advertising
            ble_scoreboard_advertise();
        }
        break;

    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG, "Disconnected; reason=%d", event->disconnect.reason);
        g_conn_handle = BLE_HS_CONN_HANDLE_NONE;

        // Resume advertising for reconnection
        ble_scoreboard_advertise();
        break;

    case BLE_GAP_EVENT_ADV_COMPLETE:
        ESP_LOGI(TAG, "Advertisement complete");
        ble_scoreboard_advertise();
        break;

    case BLE_GAP_EVENT_SUBSCRIBE:
        ESP_LOGI(TAG, "Subscribe event; cur_indicate=%d, val_handle=%d",
                 event->subscribe.cur_indicate, g_char_val_handle);
        break;

    case BLE_GAP_EVENT_MTU:
        ESP_LOGI(TAG, "MTU update event; conn_handle=%d, mtu=%d",
                 event->mtu.conn_handle, event->mtu.value);
        break;

    default:
        break;
    }

    return 0;
}

// ============================================================================
// GATT Access Callback
// ============================================================================
static int ble_scoreboard_gatt_access(uint16_t conn_handle, uint16_t attr_handle,
                                       struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    int rc;

    switch (ctxt->op) {
    case BLE_GATT_ACCESS_OP_WRITE_CHR:
        ESP_LOGI(TAG, "Write received, len=%d", OS_MBUF_PKTLEN(ctxt->om));

        // Read the packet data
        uint8_t packet[BLE_PACKET_SIZE];
        uint16_t len = OS_MBUF_PKTLEN(ctxt->om);

        if (len != BLE_PACKET_SIZE) {
            ESP_LOGW(TAG, "Invalid packet size: %d (expected %d)", len, BLE_PACKET_SIZE);
            return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
        }

        rc = ble_hs_mbuf_to_flat(ctxt->om, packet, len, NULL);
        if (rc != 0) {
            ESP_LOGE(TAG, "Failed to read packet data: %d", rc);
            return BLE_ATT_ERR_UNLIKELY;
        }

        // Process the packet
        g_state.blue_score = packet[BLE_PACKET_BLUE_SCORE] % 100;
        g_state.red_score = packet[BLE_PACKET_RED_SCORE] % 100;
        g_state.timer_minutes = packet[BLE_PACKET_TIMER_MIN];
        g_state.timer_seconds = packet[BLE_PACKET_TIMER_SEC];
        g_state.slow_update = (packet[BLE_PACKET_FLAGS] & BLE_FLAG_TIMER_UPDATE_SLOW) != 0;
        bool force_update = (packet[BLE_PACKET_FLAGS] & BLE_FLAG_FORCE_SEGMENT_UPDATE) != 0;

        ESP_LOGI(TAG, "Packet: Blue=%d, Red=%d, Timer=%02d:%02d, SlowUpdate=%d, ForceUpdate=%d",
                 g_state.blue_score, g_state.red_score,
                 g_state.timer_minutes, g_state.timer_seconds,
                 g_state.slow_update, force_update);

        // If force update flag is set, clear display state to ensure all segments refresh
        if (force_update) {
            ble_scoreboard_clear_display_state();
        }

        // Determine mode based on timer values
        if (g_state.timer_minutes > 0 || g_state.timer_seconds > 0) {
            ble_scoreboard_enter_timer_mode();
        } else {
            ble_scoreboard_enter_score_mode();
        }

        return 0;

    default:
        return BLE_ATT_ERR_UNLIKELY;
    }
}

// ============================================================================
// Score/Timer Mode Handlers
// ============================================================================
static void ble_scoreboard_clear_display_state(void)
{
    // Clear current pattern state to force full refresh
    for (uint8_t i = 0; i < status.display_number && i < MAX_DISPLAYS; i++) {
        status.current_pattern[i] = 0;
    }
}

static void ble_scoreboard_enter_score_mode(void)
{
    g_state.timer_active = false;

    // Stop any running timer task
    if (g_timer_task_handle != NULL) {
        vTaskDelete(g_timer_task_handle);
        g_timer_task_handle = NULL;
    }

    ESP_LOGI(TAG, "Updating Score: Blue=%d, Red=%d",
             g_state.blue_score, g_state.red_score);

    // Display scores: group 0 = blue (left), group 1 = red (right)
    DisplayNumber(g_state.blue_score, BLE_DISPLAY_GROUP_BLUE);
    DisplayNumber(g_state.red_score, BLE_DISPLAY_GROUP_RED);
}

static void ble_scoreboard_enter_timer_mode(void)
{
    // Stop any existing timer task
    if (g_timer_task_handle != NULL) {
        vTaskDelete(g_timer_task_handle);
        g_timer_task_handle = NULL;
    }

    g_state.timer_active = true;

    ESP_LOGI(TAG, "Entering timer mode: %02d:%02d (slow_update=%d)",
             g_state.timer_minutes, g_state.timer_seconds, g_state.slow_update);

    // Clear display state for clean update
    ble_scoreboard_clear_display_state();

    // Create timer countdown task
    xTaskCreate(ble_scoreboard_timer_task, "ble_timer", 2048, NULL, 5, &g_timer_task_handle);
}

static void ble_scoreboard_timer_task(void *arg)
{
    uint8_t last_displayed_min = 0xFF;
    uint8_t last_displayed_sec = 0xFF;

    // Initial display
    DisplayNumber(g_state.timer_minutes, BLE_DISPLAY_GROUP_BLUE);
    vTaskDelay(pdMS_TO_TICKS(BLE_DISPLAY_UPDATE_DELAY_MS));
    DisplayNumber(g_state.timer_seconds, BLE_DISPLAY_GROUP_RED);
    last_displayed_min = g_state.timer_minutes;
    last_displayed_sec = g_state.timer_seconds;

    while (g_state.timer_active) {
        // Wait 1 second
        vTaskDelay(pdMS_TO_TICKS(1000));

        // Decrement timer
        if (g_state.timer_seconds > 0) {
            g_state.timer_seconds--;
        } else if (g_state.timer_minutes > 0) {
            g_state.timer_minutes--;
            g_state.timer_seconds = 59;
        } else {
            // Timer expired - return to score mode
            ESP_LOGI(TAG, "Timer expired, returning to score mode");
            g_state.timer_active = false;
            ble_scoreboard_enter_score_mode();
            g_timer_task_handle = NULL;
            vTaskDelete(NULL);
            return;
        }

        // Determine if we should update display
        bool should_update = false;

        if (g_state.slow_update) {
            // Update every 10 seconds
            if (g_state.timer_seconds % 10 == 0) {
                should_update = true;
            }
        } else {
            // Update every second
            should_update = true;
        }

        if (should_update) {
            // Update minutes display if changed
            if (g_state.timer_minutes != last_displayed_min) {
                DisplayNumber(g_state.timer_minutes, BLE_DISPLAY_GROUP_BLUE);
                last_displayed_min = g_state.timer_minutes;
                vTaskDelay(pdMS_TO_TICKS(BLE_DISPLAY_UPDATE_DELAY_MS));
            }

            // Update seconds display if changed
            if (g_state.timer_seconds != last_displayed_sec) {
                DisplayNumber(g_state.timer_seconds, BLE_DISPLAY_GROUP_RED);
                last_displayed_sec = g_state.timer_seconds;
            }
        }
    }

    g_timer_task_handle = NULL;
    vTaskDelete(NULL);
}

// ============================================================================
// BLE Stack Callbacks
// ============================================================================
static void ble_scoreboard_on_sync(void)
{
    int rc;

    // Determine best address type to use
    rc = ble_hs_util_ensure_addr(0);
    if (rc != 0) {
        ESP_LOGE(TAG, "Error ensuring address: %d", rc);
        return;
    }

    rc = ble_hs_id_infer_auto(0, &g_own_addr_type);
    if (rc != 0) {
        ESP_LOGE(TAG, "Error inferring address type: %d", rc);
        return;
    }

    // Print the Bluetooth address
    uint8_t addr[6] = {0};
    rc = ble_hs_id_copy_addr(g_own_addr_type, addr, NULL);
    if (rc == 0) {
        ESP_LOGI(TAG, "BLE Address: %02x:%02x:%02x:%02x:%02x:%02x",
                 addr[5], addr[4], addr[3], addr[2], addr[1], addr[0]);
    }

    // Clear existing bonds
    ble_scoreboard_clear_bonds();

    // Generate and display hardware ID
    ble_scoreboard_generate_hardware_id(g_hardware_id);
    ble_scoreboard_display_hardware_id();

    // Start advertising
    ble_scoreboard_advertise();
}

static void ble_scoreboard_on_reset(int reason)
{
    ESP_LOGE(TAG, "BLE host reset, reason=%d", reason);
}

static void ble_host_task(void *param)
{
    ESP_LOGI(TAG, "BLE host task started");
    nimble_port_run();
    nimble_port_freertos_deinit();
}

// ============================================================================
// Public Functions
// ============================================================================
void ble_scoreboard_init(void)
{
    int rc;

    ESP_LOGI(TAG, "Initializing BLE scoreboard service");

    // Initialize NimBLE
    rc = nimble_port_init();
    if (rc != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize NimBLE port: %d", rc);
        return;
    }

    // Configure the host
    ble_hs_cfg.reset_cb = ble_scoreboard_on_reset;
    ble_hs_cfg.sync_cb = ble_scoreboard_on_sync;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;

    // Enable bonding
    ble_hs_cfg.sm_bonding = 1;
    ble_hs_cfg.sm_mitm = 0;
    ble_hs_cfg.sm_sc = 1;
    ble_hs_cfg.sm_io_cap = BLE_SM_IO_CAP_NO_IO;
    ble_hs_cfg.sm_our_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;

    // Initialize GATT services
    ble_svc_gap_init();
    ble_svc_gatt_init();

    // Register custom GATT services
    rc = ble_gatts_count_cfg(g_gatt_svcs);
    if (rc != 0) {
        ESP_LOGE(TAG, "Error counting GATT services: %d", rc);
        return;
    }

    rc = ble_gatts_add_svcs(g_gatt_svcs);
    if (rc != 0) {
        ESP_LOGE(TAG, "Error adding GATT services: %d", rc);
        return;
    }

    // Set device name
    char device_name[32];
    ble_scoreboard_generate_hardware_id(g_hardware_id);
    snprintf(device_name, sizeof(device_name), "Scoreboard %s", g_hardware_id);
    rc = ble_svc_gap_device_name_set(device_name);
    if (rc != 0) {
        ESP_LOGW(TAG, "Failed to set device name: %d", rc);
    }

    // Start the BLE host task
    nimble_port_freertos_init(ble_host_task);

    ESP_LOGI(TAG, "BLE scoreboard service initialized");
}

const ble_scoreboard_state_t* ble_scoreboard_get_state(void)
{
    return &g_state;
}

bool ble_scoreboard_is_connected(void)
{
    return g_conn_handle != BLE_HS_CONN_HANDLE_NONE;
}
