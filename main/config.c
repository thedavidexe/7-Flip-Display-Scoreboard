/*
 * config.c
 *
 *  Created on: 11 kwi 2025
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

#include "config.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_err.h"
#include <string.h>
#include <stdio.h>
#include "main.h" 

#define NVS_NAMESPACE "storage"
extern const char *CONFIG_TAG;

extern status_t status;  // global shared status

/**
 * @brief Save entire `status` structure to NVS.
 *        Any group indices >= new total_groups but < old total_groups are erased.
 */
esp_err_t save_config_to_nvs(void)
{
    esp_err_t err;
    nvs_handle_t h;
    int32_t old_total = 0;
    int32_t new_total = status.total_groups;

    // Open NVS namespace for read/write
    err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;

    // Fetch old total_groups (so we can delete stale entries)
    err = nvs_get_i32(h, "total_groups", &old_total);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        old_total = 0;
    } else if (err != ESP_OK) {
        nvs_close(h);
        return err;
    }

    // Erase any group X keys where X >= new_total && X < old_total
    for (int i = new_total; i < old_total; i++) {
        char key[32];

        // basic fields
        snprintf(key, sizeof(key), "group%d_start", i);
        nvs_erase_key(h, key);
        snprintf(key, sizeof(key), "group%d_end", i);
        nvs_erase_key(h, key);
        snprintf(key, sizeof(key), "group%d_pattern", i);
        nvs_erase_key(h, key);
        snprintf(key, sizeof(key), "group%d_sep", i); // originally "group%d_separator"
        nvs_erase_key(h, key);
        snprintf(key, sizeof(key), "group%d_mode", i);
        nvs_erase_key(h, key);

        // MQTT
        snprintf(key, sizeof(key), "group%d_mq_topic", i); // originally "group%d_mqtt_topic"
        nvs_erase_key(h, key);

        // TIMER
        const char *timer_keys[] = {
            "t_type",        /* originally "timer_type" */
            "t_int",         /* originally "timer_interval" */
            "t_int_u",       /* originally "timer_interval_unit" */
            "t_alarm",       /* originally "timer_alarm" */
            "t_show",        /* originally "timer_show" */
            "t_from",        /* originally "timer_count_from" */
            "t_to",          /* originally "timer_count_to" */
            "t_work",        /* originally "timer_work" */
            "t_rest",        /* originally "timer_rest" */
            "t_cycles"       /* originally "timer_cycles" */
        };
        for (int k = 0; k < sizeof(timer_keys)/sizeof(timer_keys[0]); k++) {
            snprintf(key, sizeof(key), "group%d_%s", i, timer_keys[k]);
            nvs_erase_key(h, key);
        }

        // CLOCK
        const char *clock_keys[] = {
            "c_type",        /* originally "clock_type" */
            "c_fmt",         /* originally "clock_time_format" */
            "c_tfmt",        /* originally "clock_time_tormat" */
            "c_ofs"          /* originally "clock_time_offset" */
        };
        for (int k = 0; k < sizeof(clock_keys)/sizeof(clock_keys[0]); k++) {
            snprintf(key, sizeof(key), "group%d_%s", i, clock_keys[k]);
            nvs_erase_key(h, key);
        }

        // CUSTOM API
        const char *api_keys[] = {
            "api_url",
            "api_kp",        /* originally "api_key_patch" */
            "api_hdrs",      /* originally "api_headers" */
            "api_m",         /* originally "api_method" */
            "api_f",         /* originally "api_format" */
            "api_int"        /* originally "api_pulling_interval" */
        };
        for (int k = 0; k < sizeof(api_keys)/sizeof(api_keys[0]); k++) {
            snprintf(key, sizeof(key), "group%d_%s", i, api_keys[k]);
            nvs_erase_key(h, key);
        }
    }

    // Write general settings
    nvs_set_i32(h, "total_groups", new_total);
    nvs_set_u8 (h, "led",        status.led ? 1 : 0);
    nvs_set_str(h, "timezone",   status.timezone);

    // Write per-group settings
    for (int i = 0; i < new_total; i++) {
        char key[32];

        // positions
        snprintf(key, sizeof(key), "group%d_start", i);
        nvs_set_i32(h, key, status.groups[i].start_position);
        snprintf(key, sizeof(key), "group%d_end", i);
        nvs_set_i32(h, key, status.groups[i].end_position);

        // pattern[] as a blob of MAX_DISPLAYS ints
        snprintf(key, sizeof(key), "group%d_pattern", i);
        nvs_set_blob(h, key,
                     status.groups[i].pattern,
                     sizeof(status.groups[i].pattern));

        // separator and mode
        snprintf(key, sizeof(key), "group%d_sep", i); // originally "group%d_separator"
        nvs_set_i32(h, key, status.groups[i].separator);
        snprintf(key, sizeof(key), "group%d_mode", i);
        nvs_set_i32(h, key, status.groups[i].mode);

        // mode-specific
        switch (status.groups[i].mode) {
        case MODE_MQTT:
            snprintf(key, sizeof(key), "group%d_mq_topic", i); // originally "group%d_mqtt_topic"
            nvs_set_str(h, key, status.groups[i].mqtt.topic);
            break;

        case MODE_TIMER: {
            char sk[32];

            // timer type
            snprintf(sk, sizeof(sk), "group%d_t_type", i); // originally "group%d_timer_type"
            nvs_set_i32(h, sk, status.groups[i].timer.type);

            // interval value
            snprintf(sk, sizeof(sk), "group%d_t_int", i); // originally "group%d_timer_interval"
            nvs_set_i32(h, sk, status.groups[i].timer.interval);

            // interval unit
            snprintf(sk, sizeof(sk), "group%d_t_int_u", i); // originally "group%d_timer_interval_unit"
            nvs_set_i32(h, sk, status.groups[i].timer.interval_unit);

            // alarm flag
            snprintf(sk, sizeof(sk), "group%d_t_alarm", i); // originally "group%d_timer_alarm"
            nvs_set_u8(h, sk, status.groups[i].timer.alarm ? 1 : 0);

            // show current cycle
            snprintf(sk, sizeof(sk), "group%d_t_show", i); // originally "group%d_timer_show"
            nvs_set_u8(h, sk, status.groups[i].timer.show_curr_cycle ? 1 : 0);

            // count from
            snprintf(sk, sizeof(sk), "group%d_t_from", i); // originally "group%d_timer_count_from"
            nvs_set_i32(h, sk, status.groups[i].timer.count_from);

            // count to
            snprintf(sk, sizeof(sk), "group%d_t_to", i); // originally "group%d_timer_count_to"
            nvs_set_i32(h, sk, status.groups[i].timer.count_to);

            // work time
            snprintf(sk, sizeof(sk), "group%d_t_work", i); // originally "group%d_timer_work"
            nvs_set_i32(h, sk, status.groups[i].timer.work_time);

            // rest time
            snprintf(sk, sizeof(sk), "group%d_t_rest", i); // originally "group%d_timer_rest"
            nvs_set_i32(h, sk, status.groups[i].timer.rest_time);

            // cycles
            snprintf(sk, sizeof(sk), "group%d_t_cycles", i); // originally "group%d_timer_cycles"
            nvs_set_i32(h, sk, status.groups[i].timer.cycles);
        }
        break;

        case MODE_CLOCK: {
            char sk[32];

            // clock type
            snprintf(sk, sizeof(sk), "group%d_c_type", i); // originally "group%d_clock_type"
            nvs_set_i32(h, sk, status.groups[i].clock.type);

            // time format (12h/24h)
            snprintf(sk, sizeof(sk), "group%d_c_fmt", i); // originally "group%d_clock_time_format"
            nvs_set_i32(h, sk, status.groups[i].clock.time_format);

//            // time_tormat flag (true for 24h, false for 12h)
//            snprintf(sk, sizeof(sk), "group%d_c_tfmt", i); // originally "group%d_clock_time_tormat"
//            nvs_set_u8(h, sk, status.groups[i].clock.time_tormat ? 1 : 0);

            // time offset
            snprintf(sk, sizeof(sk), "group%d_c_ofs", i); // originally "group%d_clock_time_offset"
            nvs_set_i32(h, sk, status.groups[i].clock.time_offset);
        }
        break;

        case MODE_CUSTOM_API: {
            char sk[32];

            // URL endpoint
            snprintf(sk, sizeof(sk), "group%d_api_url", i);
            nvs_set_str(h, sk, status.groups[i].api.url);

            // JSON key path in response
            snprintf(sk, sizeof(sk), "group%d_api_kp", i); // originally "group%d_api_key_patch"
            nvs_set_str(h, sk, status.groups[i].api.key_patch);

            // HTTP headers blob (serialized JSON)
            snprintf(sk, sizeof(sk), "group%d_api_hdrs", i); // originally "group%d_api_headers"
            nvs_set_str(h, sk, status.groups[i].api.headers);

            // HTTP method (POST/GET)
            snprintf(sk, sizeof(sk), "group%d_api_m", i); // originally "group%d_api_method"
            nvs_set_i32(h, sk, status.groups[i].api.method);

            // Response format (JSON/XML/TEXT)
            snprintf(sk, sizeof(sk), "group%d_api_f", i); // originally "group%d_api_format"
            nvs_set_i32(h, sk, status.groups[i].api.format);

            // Polling interval (seconds)
            snprintf(sk, sizeof(sk), "group%d_api_int", i); // originally "group%d_api_pulling_interval"
            nvs_set_u8(h, sk, status.groups[i].api.pulling_interval);
        }
        break;

        default:
            // nothing extra for MODE_NONE / MODE_MANUAL
            break;
        }
    }

    // Commit and close
    err = nvs_commit(h);
    nvs_close(h);
    return err;
}

esp_err_t load_config_from_nvs(void)
{
    esp_err_t err;
    nvs_handle_t h;
    int32_t total;

    err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &h);
    if (err != ESP_OK) return err;

    // total_groups
    err = nvs_get_i32(h, "total_groups", &total);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        // no config stored yet
        nvs_close(h);
        return ESP_OK;
    } else if (err != ESP_OK) {
        nvs_close(h);
        return err;
    }
    status.total_groups = total;

    // led
    {
        uint8_t v = 0;
        if (nvs_get_u8(h, "led", &v) == ESP_OK) {
            status.led = v ? true : false;
        }
    }
    // timezone
    {
        size_t len = sizeof(status.timezone);
        if (nvs_get_str(h, "timezone", status.timezone, &len) != ESP_OK) {
            status.timezone[0] = '\0';
        }
    }

    // per-group
    for (int i = 0; i < status.total_groups; i++) {
        char key[32];
        int32_t v;

        snprintf(key, sizeof(key), "group%d_start", i);
        if (nvs_get_i32(h, key, &v) == ESP_OK) status.groups[i].start_position = v;

        snprintf(key, sizeof(key), "group%d_end", i);
        if (nvs_get_i32(h, key, &v) == ESP_OK) status.groups[i].end_position = v;

        // pattern[]
        snprintf(key, sizeof(key), "group%d_pattern", i);
        size_t sz = sizeof(status.groups[i].pattern);
        nvs_get_blob(h, key, status.groups[i].pattern, &sz);

        snprintf(key, sizeof(key), "group%d_sep", i); // originally "group%d_separator"
        if (nvs_get_i32(h, key, &v) == ESP_OK) status.groups[i].separator = v;

        snprintf(key, sizeof(key), "group%d_mode", i);
        if (nvs_get_i32(h, key, &v) == ESP_OK) status.groups[i].mode = v;

        // mode-specific
        switch (status.groups[i].mode) {
        case MODE_MQTT:
            {
                size_t len = sizeof(status.groups[i].mqtt.topic);
                snprintf(key, sizeof(key), "group%d_mq_topic", i); // originally "group%d_mqtt_topic"
                nvs_get_str(h, key, status.groups[i].mqtt.topic, &len);
            }
            break;

        case MODE_TIMER: {
            char sk[32];
            int32_t v32;
            uint8_t v8;

            snprintf(sk, sizeof(sk), "group%d_t_type", i); // originally "group%d_timer_type"
            if (nvs_get_i32(h, sk, &v32) == ESP_OK) {
                status.groups[i].timer.type = v32;
            }

            snprintf(sk, sizeof(sk), "group%d_t_int", i); // originally "group%d_timer_interval"
            if (nvs_get_i32(h, sk, &v32) == ESP_OK) {
                status.groups[i].timer.interval = v32;
            }

            snprintf(sk, sizeof(sk), "group%d_t_int_u", i); // originally "group%d_timer_interval_unit"
            if (nvs_get_i32(h, sk, &v32) == ESP_OK) {
                status.groups[i].timer.interval_unit = v32;
            }

            snprintf(sk, sizeof(sk), "group%d_t_alarm", i); // originally "group%d_timer_alarm"
            if (nvs_get_u8(h, sk, &v8) == ESP_OK) {
                status.groups[i].timer.alarm = v8;
            }

            snprintf(sk, sizeof(sk), "group%d_t_show", i); // originally "group%d_timer_show"
            if (nvs_get_u8(h, sk, &v8) == ESP_OK) {
                status.groups[i].timer.show_curr_cycle = v8;
            }

            snprintf(sk, sizeof(sk), "group%d_t_from", i); // originally "group%d_timer_count_from"
            if (nvs_get_i32(h, sk, &v32) == ESP_OK) {
                status.groups[i].timer.count_from = v32;
            }

            snprintf(sk, sizeof(sk), "group%d_t_to", i); // originally "group%d_timer_count_to"
            if (nvs_get_i32(h, sk, &v32) == ESP_OK) {
                status.groups[i].timer.count_to = v32;
            }

            snprintf(sk, sizeof(sk), "group%d_t_work", i); // originally "group%d_timer_work"
            if (nvs_get_i32(h, sk, &v32) == ESP_OK) {
                status.groups[i].timer.work_time = v32;
            }

            snprintf(sk, sizeof(sk), "group%d_t_rest", i); // originally "group%d_timer_rest"
            if (nvs_get_i32(h, sk, &v32) == ESP_OK) {
                status.groups[i].timer.rest_time = v32;
            }

            snprintf(sk, sizeof(sk), "group%d_t_cycles", i); // originally "group%d_timer_cycles"
            if (nvs_get_i32(h, sk, &v32) == ESP_OK) {
                status.groups[i].timer.cycles = v32;
            }
        }
        break;

        case MODE_CLOCK: {
            char sk[32];
            int32_t v32;
//            uint8_t v8;

            snprintf(sk, sizeof(sk), "group%d_c_type", i); // originally "group%d_clock_type"
            if (nvs_get_i32(h, sk, &v32) == ESP_OK) {
                status.groups[i].clock.type = v32;
            }
            snprintf(sk, sizeof(sk), "group%d_c_fmt", i); // originally "group%d_clock_time_format"
            if (nvs_get_i32(h, sk, &v32) == ESP_OK) {
                status.groups[i].clock.time_format = v32;
            }

//            snprintf(sk, sizeof(sk), "group%d_c_tfmt", i); // originally "group%d_clock_time_tormat"
//            if (nvs_get_u8(h, sk, &v8) == ESP_OK) {
//                status.groups[i].clock.time_tormat = v8;
//            }

            snprintf(sk, sizeof(sk), "group%d_c_ofs", i); // originally "group%d_clock_time_offset"
            if (nvs_get_i32(h, sk, &v32) == ESP_OK) {
                status.groups[i].clock.time_offset = v32;
            }
        }
        break;

        case MODE_CUSTOM_API: {
            char sk[32];
            int32_t v32;
            uint8_t v8;
            size_t len;

            snprintf(sk, sizeof(sk), "group%d_api_url", i);
            len = sizeof(status.groups[i].api.url);
            nvs_get_str(h, sk, status.groups[i].api.url, &len);

            snprintf(sk, sizeof(sk), "group%d_api_kp", i); // originally "group%d_api_key_patch"
            len = sizeof(status.groups[i].api.key_patch);
            nvs_get_str(h, sk, status.groups[i].api.key_patch, &len);

            snprintf(sk, sizeof(sk), "group%d_api_hdrs", i); // originally "group%d_api_headers"
            len = sizeof(status.groups[i].api.headers);
            nvs_get_str(h, sk, status.groups[i].api.headers, &len);

            snprintf(sk, sizeof(sk), "group%d_api_m", i); // originally "group%d_api_method"
            if (nvs_get_i32(h, sk, &v32) == ESP_OK) {
                status.groups[i].api.method = v32;
            }

            snprintf(sk, sizeof(sk), "group%d_api_f", i); // originally "group%d_api_format"
            if (nvs_get_i32(h, sk, &v32) == ESP_OK) {
                status.groups[i].api.format = v32;
            }

            snprintf(sk, sizeof(sk), "group%d_api_int", i); // originally "group%d_api_pulling_interval"
            if (nvs_get_u8(h, sk, &v8) == ESP_OK) {
                status.groups[i].api.pulling_interval = v8;
            }
        }
        break;

        default:
            break;
        }
    }

    nvs_close(h);
    return ESP_OK;
}


/**
 * @brief Erase all NVS keys in this namespace, reset `status` struct to factory defaults,
 *        then immediately persist those defaults back into NVS.
 */
esp_err_t factory_reset_config(void)
{
    esp_err_t err;
    nvs_handle_t h;

    // Erase entire NVS namespace
    err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    err = nvs_erase_all(h);
    if (err != ESP_OK) {
        nvs_close(h);
        return err;
    }
    err = nvs_commit(h);
    nvs_close(h);
    if (err != ESP_OK) return err;

    // Apply factory defaults to RAM
    status.total_groups = 1;
    // Optionally reset LED and timezone too:
    status.led = false;
    status.timezone[0] = '\0';

    status.groups[0].start_position = 0;
    status.groups[0].end_position   = status.display_number - 1;
    for (int d = 0; d < MAX_DISPLAYS; d++) {
        status.groups[0].pattern[d] = 0;
    }
    status.groups[0].separator = SEP_NULL;
    status.groups[0].mode      = MODE_NONE;

    // Persist defaults immediately
    return save_config_to_nvs();
}

/**
 * @brief   Check whether this is the first run (no config saved yet).
 *
 * @return  true   if the NVS key "total_groups" does not exist (fresh device)
 *          false  if the key exists (configuration already stored)
 */
bool is_first_run(void)
{
    nvs_handle_t h;
    // Open our NVS namespace in read-only mode
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &h);
    if (err != ESP_OK) {
        // If we can’t open NVS, assume first run
        return true;
    }

    int32_t total;
    // Attempt to read the "total_groups" key
    err = nvs_get_i32(h, "total_groups", &total);
    nvs_close(h);

    // If the key wasn’t found, it’s a first run
    return (err == ESP_ERR_NVS_NOT_FOUND);
}

void show_config(void)
{
	ESP_LOGI(CONFIG_TAG, "Printing config file..");
	ESP_LOGI(CONFIG_TAG, "total_groups: %d", status.total_groups);
	ESP_LOGI(CONFIG_TAG, "display_number: %d", status.display_number);
	ESP_LOGI(CONFIG_TAG, "led: %d", status.led);
	ESP_LOGI(CONFIG_TAG, "timezone: %s", status.timezone);
	
	for(uint8_t i=0; i<status.display_number; i++)
	{
		ESP_LOGI(CONFIG_TAG, "groups[%d].mode: %d", i, status.groups[i].mode);
	}
	
}