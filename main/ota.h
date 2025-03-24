/*
 * ota.h
 *
 *  Created on: 23 mar 2025
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

#ifndef OTA_H_
#define OTA_H_

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Define OTA update type: firmware or web application (www)
typedef enum {
    OTA_TYPE_FIRMWARE,
    OTA_TYPE_WEB_APP
} ota_type_t;

/**
 * @brief Starts an OTA update using the provided URL.
 *
 * This function creates a FreeRTOS task that downloads and installs the new firmware.
 * The OTA process will only be initiated when this function is called.
 *
 * @param url The URL of the firmware binary file.
 * @return esp_err_t ESP_OK on success, or an appropriate error code on failure.
 */
esp_err_t ota_start(const char *url, ota_type_t type);

#ifdef __cplusplus
}
#endif


#endif /* MAIN_OTA_H_ */
