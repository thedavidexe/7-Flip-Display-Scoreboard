/*
 * main.c
 *
 *  Created on: 5 mar 2025
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
#include "74AHC595.h"
#include "config.h"
#include "freertos/idf_additions.h"
#include "freertos/timers.h"
#include "led.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "driver/gpio.h"

//Show dump of entire NVS
//#define NVS_DATA

extern const char *CONFIG_TAG;
status_t status = {};
SemaphoreHandle_t xNewDataSemaphore = NULL;
SemaphoreHandle_t xPeriodicSemaphore = NULL;
static TaskHandle_t xTimerTaskHandle = NULL;
static TaskHandle_t xScoreATaskHandle = NULL;
static QueueHandle_t xScoreAQueue = NULL;
static const char *SCOREA_TAG = "SCORE_A";
static TimerHandle_t xScoreAHoldTimer = NULL;
static TimerHandle_t xScoreAResetTimer = NULL;

enum {
    BUTTON_PRESSED = 0,
    BUTTON_NOT_PRESSED = 1
};

enum {
    SCORE_A_EVENT_EDGE = 1,
    SCORE_A_EVENT_HOLD = 2,
    SCORE_A_EVENT_RESET = 3
};
static TaskHandle_t xScoreBTaskHandle = NULL;
static QueueHandle_t xScoreBQueue = NULL;
static const char *SCOREB_TAG = "SCORE_B";
static TimerHandle_t xScoreBHoldTimer = NULL;
static TimerHandle_t xScoreBResetTimer = NULL;

enum {
    SCORE_B_EVENT_EDGE = 1,
    SCORE_B_EVENT_HOLD = 2,
    SCORE_B_EVENT_RESET = 3
};

// Player A input to increment score (remote control)
// Using GPIO32 (RTC-capable). Internal pull-up enabled for active-low.
#define SCORE_A_INPUT_PIN     GPIO_NUM_32
// Which display group to show the score on (0-based)
#define SCORE_A_GROUP_INDEX   0
// Debounce time for mechanical button (ms)
#define SCORE_DEBOUNCE_MS   50

#define FULL_DISPLAY_RESET_TIME 1500  // 100ms * 7segs * 2digits + 100 buffer

#define REMOTE_DECREMENT_HOLD_TIME 1500  //hold remote 1.5s to decrement
#define REMOTE_FULL_RESET_HOLD_TIME 3000  // hold remote for 3s to reset all

// Player B input on GPIO4
#define SCORE_B_INPUT_PIN   GPIO_NUM_4
#define SCORE_B_GROUP_INDEX 1

static volatile uint32_t score_value_a = 0;
static volatile uint32_t score_value_b = 0;

static void IRAM_ATTR score_a_isr_handler(void* arg)
{
    const uint32_t evt = SCORE_A_EVENT_EDGE;
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    if (xScoreAQueue) {
        xQueueSendFromISR(xScoreAQueue, &evt, &xHigherPriorityTaskWoken);
    }
    if (xHigherPriorityTaskWoken) {
        portYIELD_FROM_ISR();
    }
}

static void score_a_hold_timer_callback(TimerHandle_t xTimer)
{
    (void)xTimer;
    if (!xScoreAQueue) {
        return;
    }

    const uint32_t hold_evt = SCORE_A_EVENT_HOLD;
    if (xQueueSend(xScoreAQueue, &hold_evt, 0) != pdPASS) {
        ESP_LOGW(SCOREA_TAG, "Hold event queue full");
    }
}

static void score_a_reset_timer_callback(TimerHandle_t xTimer)
{
    (void)xTimer;
    if (!xScoreAQueue) {
        return;
    }

    const uint32_t reset_evt = SCORE_A_EVENT_RESET;
    if (xQueueSend(xScoreAQueue, &reset_evt, 0) != pdPASS) {
        ESP_LOGW(SCOREA_TAG, "Reset event queue full");
    }
}

static void IRAM_ATTR score_b_isr_handler(void* arg)
{
    const uint32_t evt = SCORE_B_EVENT_EDGE;
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    if (xScoreBQueue) {
        xQueueSendFromISR(xScoreBQueue, &evt, &xHigherPriorityTaskWoken);
    }
    if (xHigherPriorityTaskWoken) {
        portYIELD_FROM_ISR();
    }
}

static void score_b_hold_timer_callback(TimerHandle_t xTimer)
{
    (void)xTimer;
    if (!xScoreBQueue) {
        return;
    }

    const uint32_t hold_evt = SCORE_B_EVENT_HOLD;
    if (xQueueSend(xScoreBQueue, &hold_evt, 0) != pdPASS) {
        ESP_LOGW(SCOREB_TAG, "Hold event queue full");
    }
}

static void score_b_reset_timer_callback(TimerHandle_t xTimer)
{
    (void)xTimer;
    if (!xScoreBQueue) {
        return;
    }

    const uint32_t reset_evt = SCORE_B_EVENT_RESET;
    if (xQueueSend(xScoreBQueue, &reset_evt, 0) != pdPASS) {
        ESP_LOGW(SCOREB_TAG, "Reset event queue full");
    }
}

static void clear_teamA_disp_state(void)
{
    for (uint8_t i = 0; i < 2; i++) {
        status.current_pattern[i] = 0;
    }

}

static void clear_teamB_disp_state(void)
{
    for (uint8_t i = 2; i < 4; i++) {
        status.current_pattern[i] = 0;
    }

}

static void init_score_a_input(void)
{
    ESP_LOGI(SCOREA_TAG, "Init score A input on GPIO%d (pullup=ON, intr=ANYEDGE)", (int)SCORE_A_INPUT_PIN);
    gpio_config_t io_conf = {
        .pin_bit_mask = 1ULL << SCORE_A_INPUT_PIN,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,    // GPIO32 supports internal pull-up
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_ANYEDGE       // Handle press and release with debounce
    };
    gpio_config(&io_conf);

    // Install ISR service if not already installed elsewhere
    esp_err_t r = gpio_install_isr_service(0);
    if (r != ESP_OK && r != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(FIRM, "gpio_install_isr_service failed: %s", esp_err_to_name(r));
    }
    else if (r == ESP_OK) {
        ESP_LOGI(SCOREA_TAG, "GPIO ISR service installed");
    } else {
        ESP_LOGI(SCOREA_TAG, "GPIO ISR service already installed");
    }
    gpio_isr_handler_add(SCORE_A_INPUT_PIN, score_a_isr_handler, NULL);
    ESP_LOGI(SCOREA_TAG, "ISR handler attached to GPIO%d", (int)SCORE_A_INPUT_PIN);
}

static void vScoreATask(void *arg)
{
    uint32_t evt;
    // Initialize display with current score
	vTaskDelay(pdMS_TO_TICKS(FULL_DISPLAY_RESET_TIME));  // this allows the other score to init first to avoid updating too many displays at once
    DisplayNumber(score_value_a, SCORE_A_GROUP_INDEX);
    ESP_LOGI(SCOREA_TAG, "Score A task started (group=%d, initial=%u)", SCORE_A_GROUP_INDEX, (unsigned)score_value_a);
    // false means button is not pressed (value of 1), true means button pressed (gpio value of 0)
    bool previous_button_state = BUTTON_NOT_PRESSED;
    bool current_button_state = BUTTON_NOT_PRESSED;
    bool hold_decremented = false;
    bool hold_reset = false;
    for(;;) {
        if (xQueueReceive(xScoreAQueue, &evt, portMAX_DELAY) == pdTRUE) {
            // for this interrupt get current button state
            current_button_state = gpio_get_level(SCORE_A_INPUT_PIN);
            if (evt == SCORE_A_EVENT_EDGE) {
                if (previous_button_state == BUTTON_PRESSED && current_button_state == BUTTON_NOT_PRESSED) {
                    // just now released
                    if (xScoreAHoldTimer && xTimerIsTimerActive(xScoreAHoldTimer) != pdFALSE) {
                        if (xTimerStop(xScoreAHoldTimer, 0) != pdPASS) {
                            ESP_LOGW(SCOREA_TAG, "Failed to stop hold timer on release");
                        }
                    }
                    if (xScoreAResetTimer && xTimerIsTimerActive(xScoreAResetTimer) != pdFALSE) {
                        if (xTimerStop(xScoreAResetTimer, 0) != pdPASS) {
                            ESP_LOGW(SCOREA_TAG, "Failed to stop reset timer on release");
                        }
                    }
                    if (!hold_decremented && !hold_reset) {
                        score_value_a++;
                        DisplayNumber(score_value_a, SCORE_A_GROUP_INDEX);
                        ESP_LOGI(SCOREA_TAG, "Score A increment on release -> %u", (unsigned)score_value_a);
                    } else {
                        hold_decremented = false;
                        hold_reset = false;
                    }
                } else if (previous_button_state == BUTTON_NOT_PRESSED && current_button_state == BUTTON_PRESSED) {
                    // just now pressed
                    hold_decremented = false;
                    hold_reset = false;
                    if (xScoreAHoldTimer) {
                        if (xTimerIsTimerActive(xScoreAHoldTimer) != pdFALSE) {
                            if (xTimerStop(xScoreAHoldTimer, 0) != pdPASS) {
                                ESP_LOGW(SCOREA_TAG, "Failed to stop hold timer before restart");
                            }
                        }
                        if (xTimerStart(xScoreAHoldTimer, 0) != pdPASS) {
                            ESP_LOGW(SCOREA_TAG, "Failed to start hold timer");
                        }
                    }
                    if (xScoreAResetTimer) {
                        if (xTimerIsTimerActive(xScoreAResetTimer) != pdFALSE) {
                            if (xTimerStop(xScoreAResetTimer, 0) != pdPASS) {
                                ESP_LOGW(SCOREA_TAG, "Failed to stop reset timer before restart");
                            }
                        }
                        if (xTimerStart(xScoreAResetTimer, 0) != pdPASS) {
                            ESP_LOGW(SCOREA_TAG, "Failed to start reset timer");
                        }
                    }
                }
            } else if (evt == SCORE_A_EVENT_HOLD) {
                if (current_button_state == BUTTON_PRESSED && !hold_decremented) {
                    if (score_value_a > 0) {
                        score_value_a--;
                        // clear current display state so all coils fire in case they got jumbled
                        clear_teamA_disp_state();
                        DisplayNumber(score_value_a, SCORE_A_GROUP_INDEX);
                        ESP_LOGI(SCOREA_TAG, "Score A decrement on hold -> %u", (unsigned)score_value_a);
                    }
                    hold_decremented = true;
                }
            } else if (evt == SCORE_A_EVENT_RESET) {
                if (current_button_state == BUTTON_PRESSED && !hold_reset) {
                    score_value_a = 0;
                    score_value_b = 0;
                    // clear current display state so all coils fire in case they got jumbled
                    clear_teamA_disp_state();
                    clear_teamB_disp_state();
                    DisplayNumber(score_value_a, SCORE_A_GROUP_INDEX);
                    vTaskDelay(pdMS_TO_TICKS(FULL_DISPLAY_RESET_TIME));
                    DisplayNumber(score_value_b, SCORE_B_GROUP_INDEX);
                    ESP_LOGI(SCOREA_TAG, "Scores reset to 0 after 5s hold");
                    hold_reset = true;
                    hold_decremented = true;
                }
            }
            previous_button_state = current_button_state;
        }
    }
}

static void init_score_b_input(void)
{
    ESP_LOGI(SCOREB_TAG, "Init score B input on GPIO%d (pullup=ON, intr=ANYEDGE)", (int)SCORE_B_INPUT_PIN);
    gpio_config_t io_conf = {
        .pin_bit_mask = 1ULL << SCORE_B_INPUT_PIN,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_ANYEDGE
    };
    gpio_config(&io_conf);

    // ISR service should already be installed by init_score_a_input(); tolerate duplicates
    esp_err_t r = gpio_install_isr_service(0);
    if (r != ESP_OK && r != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(FIRM, "gpio_install_isr_service (B) failed: %s", esp_err_to_name(r));
    }
    gpio_isr_handler_add(SCORE_B_INPUT_PIN, score_b_isr_handler, NULL);
    ESP_LOGI(SCOREB_TAG, "ISR handler attached to GPIO%d", (int)SCORE_B_INPUT_PIN);
}

static void vScoreBTask(void *arg)
{
    uint32_t evt;
    // Initialize display for B with current score
    DisplayNumber(score_value_b, SCORE_B_GROUP_INDEX);
    ESP_LOGI(SCOREB_TAG, "Score B task started (group=%d, initial=%u)", SCORE_B_GROUP_INDEX, (unsigned)score_value_b);
    // false means button is not pressed (value of 1), true means button pressed (gpio value of 0)
    bool previous_button_state = BUTTON_NOT_PRESSED;
    bool current_button_state = BUTTON_NOT_PRESSED;
    bool hold_decremented = false;
    bool hold_reset = false;
    for(;;) {
        if (xQueueReceive(xScoreBQueue, &evt, portMAX_DELAY) == pdTRUE) {
            // for this interrupt get current button state
            current_button_state = gpio_get_level(SCORE_B_INPUT_PIN);
            if (evt == SCORE_B_EVENT_EDGE) {
                if (previous_button_state == BUTTON_PRESSED && current_button_state == BUTTON_NOT_PRESSED) {
                    // just now released
                    if (xScoreBHoldTimer && xTimerIsTimerActive(xScoreBHoldTimer) != pdFALSE) {
                        if (xTimerStop(xScoreBHoldTimer, 0) != pdPASS) {
                            ESP_LOGW(SCOREB_TAG, "Failed to stop hold timer on release");
                        }
                    }
                    if (xScoreBResetTimer && xTimerIsTimerActive(xScoreBResetTimer) != pdFALSE) {
                        if (xTimerStop(xScoreBResetTimer, 0) != pdPASS) {
                            ESP_LOGW(SCOREB_TAG, "Failed to stop reset timer on release");
                        }
                    }
                    if (!hold_decremented && !hold_reset) {
                        score_value_b++;
                        DisplayNumber(score_value_b, SCORE_B_GROUP_INDEX);
                        ESP_LOGI(SCOREB_TAG, "Score B increment on release -> %u", (unsigned)score_value_b);
                    } else {
                        hold_decremented = false;
                        hold_reset = false;
                    }
                } else if (previous_button_state == BUTTON_NOT_PRESSED && current_button_state == BUTTON_PRESSED) {
                    // just now pressed
                    hold_decremented = false;
                    hold_reset = false;
                    if (xScoreBHoldTimer) {
                        if (xTimerIsTimerActive(xScoreBHoldTimer) != pdFALSE) {
                            if (xTimerStop(xScoreBHoldTimer, 0) != pdPASS) {
                                ESP_LOGW(SCOREB_TAG, "Failed to stop hold timer before restart");
                            }
                        }
                        if (xTimerStart(xScoreBHoldTimer, 0) != pdPASS) {
                            ESP_LOGW(SCOREB_TAG, "Failed to start hold timer");
                        }
                    }
                    if (xScoreBResetTimer) {
                        if (xTimerIsTimerActive(xScoreBResetTimer) != pdFALSE) {
                            if (xTimerStop(xScoreBResetTimer, 0) != pdPASS) {
                                ESP_LOGW(SCOREB_TAG, "Failed to stop reset timer before restart");
                            }
                        }
                        if (xTimerStart(xScoreBResetTimer, 0) != pdPASS) {
                            ESP_LOGW(SCOREB_TAG, "Failed to start reset timer");
                        }
                    }
                }
            } else if (evt == SCORE_B_EVENT_HOLD) {
                if (current_button_state == BUTTON_PRESSED && !hold_decremented) {
                    if (score_value_b > 0) {
                        score_value_b--;
                        // clear current display state so all coils fire in case they got jumbled
                        clear_teamB_disp_state();
                        DisplayNumber(score_value_b, SCORE_B_GROUP_INDEX);
                        ESP_LOGI(SCOREB_TAG, "Score B decrement on hold -> %u", (unsigned)score_value_b);
                    }
                    hold_decremented = true;
                }
            } else if (evt == SCORE_B_EVENT_RESET) {
                if (current_button_state == BUTTON_PRESSED && !hold_reset) {
                    // clear current display state so all coils fire in case they got jumbled
                    clear_teamA_disp_state();
                    clear_teamB_disp_state();
                    score_value_a = 0;
                    score_value_b = 0;
                    DisplayNumber(score_value_a, SCORE_A_GROUP_INDEX);
                    vTaskDelay(pdMS_TO_TICKS(FULL_DISPLAY_RESET_TIME));
                    DisplayNumber(score_value_b, SCORE_B_GROUP_INDEX);
                    ESP_LOGI(SCOREB_TAG, "Scores reset to 0 after 5s hold");
                    hold_reset = true;
                    hold_decremented = true;
                }
            }
            previous_button_state = current_button_state;
        }
    }
}

void vDataProcessingTask(void *arg);
void vClockModeHandlingTask(void *arg);
void vTimerModeHandlingTask(void *arg);

// Force two groups: [0..1] and [2..3]
static void hardcode_two_groups_01_and_23(void)
{
    if (status.display_number >= 4) {
        status.total_groups = 2;

        status.groups[0].start_position = 0;
        status.groups[0].end_position   = 1;
        status.groups[0].separator      = SEP_NULL;
        status.groups[0].mode           = MODE_NONE;

        status.groups[1].start_position = 2;
        status.groups[1].end_position   = 3;
        status.groups[1].separator      = SEP_NULL;
        status.groups[1].mode           = MODE_NONE;

        for (int i = 0; i < MAX_DISPLAYS; i++) {
            status.groups[0].pattern[i] = 0;
            status.groups[1].pattern[i] = 0;
        }

        ESP_LOGI(CONFIG_TAG, "Hardcoded groups: [0-1] and [2-3]");
    } else if (status.display_number >= 2) {
        status.total_groups = 1;
        status.groups[0].start_position = 0;
        status.groups[0].end_position   = 1;
        status.groups[0].separator      = SEP_NULL;
        status.groups[0].mode           = MODE_NONE;
        for (int i = 0; i < MAX_DISPLAYS; i++) status.groups[0].pattern[i] = 0;
        ESP_LOGI(CONFIG_TAG, "Hardcoded single group: [0-1] (only %u displays present)", status.display_number);
    } else {
        status.total_groups = 1;
        status.groups[0].start_position = 0;
        status.groups[0].end_position   = status.display_number ? (status.display_number - 1) : 0;
        status.groups[0].separator      = SEP_NULL;
        status.groups[0].mode           = MODE_NONE;
        for (int i = 0; i < MAX_DISPLAYS; i++) status.groups[0].pattern[i] = 0;
        ESP_LOGI(CONFIG_TAG, "Hardcoded minimal group: [0-%u]", status.groups[0].end_position);
    }
}

#ifdef NVS_DATA
void print_nvs_stats(void)
{
    nvs_stats_t nvs_stats;
    esp_err_t err = nvs_get_stats(NULL, &nvs_stats);
    if (err != ESP_OK) {
        ESP_LOGI("NVS","Błąd przy pobieraniu statystyk NVS: %s\n", esp_err_to_name(err));
        return;
    }

    ESP_LOGI("NVS","  Used entry: %d", nvs_stats.used_entries);
    ESP_LOGI("NVS","  Free entry: %d", nvs_stats.free_entries);
    ESP_LOGI("NVS","  All entry: %d", nvs_stats.total_entries);
    
}

void dump_nvs_entries(void)
{
    nvs_iterator_t it = NULL;
    // Używamy nvs_entry_find() przekazując adres iteratora jako czwarty argument.
    esp_err_t err = nvs_entry_find("nvs", NULL, NVS_TYPE_ANY, &it);
    if (err != ESP_OK || it == NULL) {
        ESP_LOGI("NVS_DUMP", "Brak zapisanych wpisów.");
        return;
    }
    
    ESP_LOGI("NVS_DUMP", "Lista wpisów w NVS:");
    do {
        nvs_entry_info_t info;
        err = nvs_entry_info(it, &info);
        if (err == ESP_OK) {
            ESP_LOGI("NVS_DUMP", "Namespace: %s, Klucz: %s, Typ: %d",
                     info.namespace_name, info.key, info.type);
        } else {
            ESP_LOGE("NVS_DUMP", "Błąd pobierania informacji o wpisie: %s", esp_err_to_name(err));
            break;
        }
    } while (nvs_entry_next(&it) == ESP_OK && it != NULL);
    
    nvs_release_iterator(it);
}
#endif

void app_main(void)
{
	ESP_LOGI(FIRM, "Version: 0.1.1");
	
    // /* Start all tasks */
    // xTaskCreate(vDataProcessingTask, "vDataProcessingTask", 4096, NULL, 6, NULL);
    // xTaskCreate(vClockModeHandlingTask, "vClockModeHandlingTask", 4096, NULL, 5, NULL);
    // xTaskCreate(vTimerModeHandlingTask, "vTimerModeHandlingTask", 4096, NULL, 4, NULL);
    // vTaskDelay(10/ portTICK_PERIOD_MS);
    
    // xTaskCreate(RestfulServerTask, "RestfulServerTask", 4048, NULL, 10, NULL);
    // xTaskCreate(mqtt_task, "MqttTask", 4096, NULL, 7, NULL);
    // xTaskCreate(RTCHandlingTask, "RTCHandlingTask", 4096, NULL, 3, NULL);
    // xTaskCreate(vLED_HandleTask, "vLED_HandleTask", 2096, NULL, 1, NULL);

#ifdef NVS_DATA    
    print_nvs_stats();
	dump_nvs_entries();
#endif
    
    shift_register_init();
    vTaskDelay(100/ portTICK_PERIOD_MS);
    
    detect_display_count();
	
	
	if (is_first_run())
	{
		ESP_LOGI(CONFIG_TAG, "Initialize `status` with default values...");
		
		//Default config
		status.groups[0].start_position = 0;
		status.groups[0].end_position = status.display_number-1;
		
		for(uint8_t i = 0; i < status.display_number; i++)
			status.groups[0].pattern[i] = 0;

		status.groups[0].separator = 0;
		status.groups[0].mode = 0;
		
		status.total_groups = 1;
		
		status.display_symbol_mode = SINGLE_SEGMENT;
		
		// Set dafault time
		uint8_t init_time[7] = {0,0,12,1,1,1,25}; // 12:00:00, 1 Jan 2025
		ds3231_init(init_time, CLOCK_RUN, FORCE_RESET);
	} 
	else 
	{
		ESP_LOGI(CONFIG_TAG, "Load `status` from NVS...");
		
	    load_config_from_nvs();
	}
	
    status.display_symbol_mode = SINGLE_MODUL;

    // Hardcode groups: Group 0 = displays 0..1, Group 1 = displays 2..3
    hardcode_two_groups_01_and_23();
    // // Optional: persist to NVS so web UI reflects the hardcoded groups
    // save_config_to_nvs();
	
	show_config();
	
    LED_set_color(YELLOW, 1);



    // Setup score A input and task
    xScoreAQueue = xQueueCreate(4, sizeof(uint32_t));
    if (xScoreAQueue != NULL) {
        ESP_LOGI(SCOREA_TAG, "Score A queue created");
        init_score_a_input();
        xScoreAHoldTimer = xTimerCreate("ScoreAHold", pdMS_TO_TICKS(REMOTE_DECREMENT_HOLD_TIME), pdFALSE, NULL, score_a_hold_timer_callback);
        if (xScoreAHoldTimer == NULL) {
            ESP_LOGE(SCOREA_TAG, "Failed to create hold timer");
        }
        xScoreAResetTimer = xTimerCreate("ScoreAReset", pdMS_TO_TICKS(REMOTE_FULL_RESET_HOLD_TIME), pdFALSE, NULL, score_a_reset_timer_callback);
        if (xScoreAResetTimer == NULL) {
            ESP_LOGE(SCOREA_TAG, "Failed to create reset timer");
        }
        xTaskCreate(vScoreATask, "vScoreATask", 2048, NULL, 8, &xScoreATaskHandle);
        ESP_LOGI(SCOREA_TAG, "Score A task created");
    } else {
        ESP_LOGE(FIRM, "Failed to create score A queue");
    }

    // Setup score B input and task
    xScoreBQueue = xQueueCreate(4, sizeof(uint32_t));
    if (xScoreBQueue != NULL) {
        ESP_LOGI(SCOREB_TAG, "Score B queue created");
        init_score_b_input();
        xScoreBHoldTimer = xTimerCreate("ScoreBHold", pdMS_TO_TICKS(REMOTE_DECREMENT_HOLD_TIME), pdFALSE, NULL, score_b_hold_timer_callback);
        if (xScoreBHoldTimer == NULL) {
            ESP_LOGE(SCOREB_TAG, "Failed to create hold timer");
        }
        xScoreBResetTimer = xTimerCreate("ScoreBReset", pdMS_TO_TICKS(REMOTE_FULL_RESET_HOLD_TIME), pdFALSE, NULL, score_b_reset_timer_callback);
        if (xScoreBResetTimer == NULL) {
            ESP_LOGE(SCOREB_TAG, "Failed to create reset timer");
        }
        xTaskCreate(vScoreBTask, "vScoreBTask", 2048, NULL, 8, &xScoreBTaskHandle);
        ESP_LOGI(SCOREB_TAG, "Score B task created");
    } else {
        ESP_LOGE(FIRM, "Failed to create score B queue");
    }

        // DEBUG GPIO INPUTS
        // Main loop - print GPIO state every second
    // while (1) {
    //     int level = gpio_get_level(SCORE_A_INPUT_PIN);
    //     ESP_LOGI(SCOREA_TAG, "GPIO %d state: %d", SCORE_A_INPUT_PIN, level);
    //     vTaskDelay(pdMS_TO_TICKS(100));
    //     int level2 = gpio_get_level(SCORE_B_INPUT_PIN);
    //     ESP_LOGI(SCOREB_TAG, "GPIO %d state: %d", SCORE_B_INPUT_PIN, level2);
    //     vTaskDelay(pdMS_TO_TICKS(100));
    // }
    
    vTaskDelete(NULL);
}

void vDataProcessingTask(void *arg)
{
	xNewDataSemaphore = xSemaphoreCreateBinary();
    if (xNewDataSemaphore == NULL) {
        return;
    }
	
	while(1)
    {
		if (xSemaphoreTake(xNewDataSemaphore, portMAX_DELAY) == pdTRUE)
        {
			ESP_LOGI(CONFIG_TAG, "New DATA from APP");
			ESP_LOGI(CONFIG_TAG, "total_groups: %d", status.total_groups);
			
			for(uint8_t i = 0; i < status.total_groups; i++)
		    {
				uint8_t displays_number = status.groups[i].end_position - status.groups[i].start_position + 1;
				
				ESP_LOGI(CONFIG_TAG, "group: %d", i);
				ESP_LOGI(CONFIG_TAG, "start position: %d", status.groups[i].start_position);
		    	ESP_LOGI(CONFIG_TAG, "end position: %d", status.groups[i].end_position);
			    ESP_LOGI(CONFIG_TAG, "separator: %d", status.groups[i].separator);
			    	  
			    if(status.groups[i].mode == MODE_MQTT){
					ESP_LOGI(CONFIG_TAG, "mode: MQTT");
					ESP_LOGI(CONFIG_TAG, "topic: %s", status.groups[i].mqtt.topic);	
				}
				else if(status.groups[i].mode == MODE_TIMER){
					ESP_LOGI(CONFIG_TAG, "mode: TIMER");
					
					status.groups[i].timer.value = status.groups[i].timer.count_from;
					status.groups[i].timer.direction = (status.groups[i].timer.count_from < status.groups[i].timer.count_to) ? COUNT_UP : COUNT_DOWN;

//					if(status.groups[i].timer.count_from < status.groups[i].timer.count_to)
//						status.groups[i].timer.direction = COUNT_UP;	
//					else
// 						status.groups[i].timer.direction = COUNT_DOWN;
					
					DisplayNumber(status.groups[i].timer.value, i);
	 				
	 				// notify timer task (will either start or cancel current wait)
	 				xTaskNotifyGive(xTimerTaskHandle);
				}
				else if(status.groups[i].mode == MODE_CLOCK){
					ESP_LOGI(CONFIG_TAG, "mode: CLOCK");
					ESP_LOGI(CONFIG_TAG, "type: %d", status.groups[i].clock.type);
					
					xTaskNotifyGive(xTimerTaskHandle);

					read_time();
					
					if(status.groups[i].clock.time_format == FORMAT_12H)  //12H
					{
						if(status.rtc.hour <= 12)
							ESP_LOGI(RTC, "Time %02d:%02d:%02d AM  Data %d.%d.%d", status.rtc.hour, status.rtc.minute, status.rtc.second, status.rtc.day, status.rtc.month, status.rtc.year);
						else
							ESP_LOGI(RTC, "Time %02d:%02d:%02d PM  Data %d.%d.%d", status.rtc.hour - 12, status.rtc.minute, status.rtc.second, status.rtc.day, status.rtc.month, status.rtc.year);	
					}		
					else //24H
					{
						ESP_LOGI(RTC, "Time %02d:%02d:%02d  Data %d.%d.%d", status.rtc.hour, status.rtc.minute, status.rtc.second, status.rtc.day, status.rtc.month, status.rtc.year);
					}	
					
					if(status.groups[i].clock.type == RTC_SECONDS)
						DisplayNumber(status.rtc.second, i);
					else if(status.groups[i].clock.type == RTC_MINUTES)
						DisplayNumber(status.rtc.minute, i);
					else if(status.groups[i].clock.type == RTC_HOURS)
					{
						if(status.groups[i].clock.time_format == FORMAT_12H)
						{
							if(status.rtc.hour <= 12)
								DisplayNumber(status.rtc.hour, i);
							else
								DisplayNumber(status.rtc.hour - 12, i);
						}
						else 
						{
							DisplayNumber(status.rtc.hour, i);
						}
					}
					else if(status.groups[i].clock.type == RTC_DAY)
						DisplayNumber(status.rtc.day, i);
					else if(status.groups[i].clock.type == RTC_MONTCH)
						DisplayNumber(status.rtc.month, i);
					else if(status.groups[i].clock.type == RTC_YEAR)
						DisplayNumber(status.rtc.year, i);										
										
				}
				else if(status.groups[i].mode == MODE_MANUAL){
					ESP_LOGI(CONFIG_TAG, "mode: MANUAL");
					
					for(uint8_t l = status.groups[i].start_position; l <= status.groups[i].end_position; l++)
						DisplaySymbol(status.groups[i].pattern[l - status.groups[i].start_position], l);
				}
				else if(status.groups[i].mode == MODE_CUSTOM_API){ // TO DO
					ESP_LOGI(CONFIG_TAG, "mode: CUSTOM_API");
					DemoMode(1);
				}
				else{
					ESP_LOGI(CONFIG_TAG, "mode: NONE");
					
					for(uint8_t k = status.groups[i].start_position; k <= status.groups[i].end_position; k++)
						DisplayDigit(10, k);
				}
			    
			    
			    for(uint8_t j = 0; j < displays_number; j++)
					ESP_LOGI(CONFIG_TAG, "disp_%d: %d", j, status.groups[i].pattern[j]);
					
			}
			
			LED_set_color(RED, 1);
			
			save_config_to_nvs();
			
        }
	}
}

void vClockModeHandlingTask(void *arg)
{
	xPeriodicSemaphore = xSemaphoreCreateBinary();
    if (xPeriodicSemaphore == NULL) {
        return;
    }
    
    ESP_LOGI(CONFIG_TAG, "CLOCK");
    
    ENABLE_ONE_SEC_ISR();
	
	while(1)
    {
		if (xSemaphoreTake(xPeriodicSemaphore, portMAX_DELAY) == pdTRUE)
        {
			CLEAR_ONE_SEC_FLAG();
			
			read_time();

			for(uint8_t i = 0; i < status.total_groups; i++)
			{
				/* CLOCK MODE */
				if(status.groups[i].mode == MODE_CLOCK)
				{		
					if(status.groups[i].clock.type == RTC_SECONDS)
					{
						DisplayNumber(status.rtc.second, i);
					}
					else if(status.groups[i].clock.type == RTC_MINUTES)
					{
						DisplayNumber(status.rtc.minute, i);
					}
					else if(status.groups[i].clock.type == RTC_HOURS)
					{
						if(status.groups[i].clock.time_format == FORMAT_12H)
						{
							if(status.rtc.hour <= 12)
								DisplayNumber(status.rtc.hour, i);
							else
								DisplayNumber(status.rtc.hour - 12, i);
						}
						else 
						{
							DisplayNumber(status.rtc.hour, i);
						}
					}
					else if(status.groups[i].clock.type == RTC_DAY)
					{
						DisplayNumber(status.rtc.day, i);
					}
					else if(status.groups[i].clock.type == RTC_MONTCH)
					{
						DisplayNumber(status.rtc.month, i);
					}
					else if(status.groups[i].clock.type == RTC_YEAR)
					{
						DisplayNumber(status.rtc.year, i);
					}
				}
			}
		}
	}
}

void vTimerModeHandlingTask(void *arg)
{
    // store own handle
    xTimerTaskHandle = xTaskGetCurrentTaskHandle();

    for( ;; )
    {
        // wait indefinitely for a “start/reset” notification
        ulTaskNotifyTake(pdFALSE, portMAX_DELAY);

    RESTART_TIMER:;

        // iterate over all groups in TIMER mode
        for(uint8_t i = 0; i < status.total_groups; i++)
        {
            if(status.groups[i].mode != MODE_TIMER ||
               status.groups[i].timer.direction == COUNT_OFF)
            {
                continue;
            }

            // compute tick interval from seconds (or whatever masz w interval_unit)
            TickType_t intervalTicks = pdMS_TO_TICKS(
                status.groups[i].timer.interval_unit * 1000UL
            );

            // make local copies of the state
            int32_t value     = status.groups[i].timer.value;
            int32_t target    = status.groups[i].timer.count_to;
            int32_t direction = status.groups[i].timer.direction;

            // loop until we hit target OR we get another notify
            while((direction == COUNT_UP && value < target) || (direction == COUNT_DOWN && value > target))
            {
                // wait for interval OR reset/cancel
                if( ulTaskNotifyTake(pdFALSE, intervalTicks) > 0 )
                {
                    // notification ⇒ user wants reset
                    goto RESTART_TIMER;
                }

                // timeout ⇒ update value i display
                if(direction == COUNT_UP)
                    value++;
                else
                    value--;

                status.groups[i].timer.value = value;
                DisplayNumber(value, i);
            }

            // Natural ending – disable timer
            status.groups[i].timer.direction = COUNT_OFF;
            
            if(status.groups[i].timer.alarm == true)
            	GenerateAlarm(i);
        }
    }
}
