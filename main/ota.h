/*
 * ota.h
 *
 *  Created on: 23 mar 2025
 *      Author: Sebastian Sokołowski
 *      Company: Smart Solutions for Home
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef OTA_H_
#define OTA_H_

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Define OTA update type: firmware, web application (www), or both
typedef enum {
    OTA_TYPE_FIRMWARE,
    OTA_TYPE_WEB_APP,
    OTA_TYPE_BOTH
} ota_type_t;

/**
 * @brief Structure for combined OTA parameters.
 */
typedef struct {
    char *firmware_url;
    char *web_app_url;
} ota_both_param_t;

/**
 * @brief Starts an OTA update using the provided URL.
 *
 * @param url The URL of the binary file.
 * @param type The type of OTA update.
 * @return esp_err_t ESP_OK on success, or an error code on failure.
 */
esp_err_t ota_start(const char *url, ota_type_t type);

/**
 * @brief Starts a combined OTA update for both firmware and web app.
 *
 * @param firmware_url The URL of the firmware binary.
 * @param web_app_url The URL of the web app binary.
 * @return esp_err_t ESP_OK on success, or an error code on failure.
 */
esp_err_t ota_start_both(const char *firmware_url, const char *web_app_url);

#ifdef __cplusplus
}
#endif

#endif /* OTA_H_ */
