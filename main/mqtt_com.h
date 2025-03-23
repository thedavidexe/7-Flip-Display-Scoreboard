/*
 * mqtt_com.h
 *
 *  Created on: 22 mar 2025
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

#ifndef MQTT_COM_H
#define MQTT_COM_H

#include <stdint.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Handle of the MQTT task (for notifying configuration changes) */
extern TaskHandle_t mqtt_task_handle;

/** 
 * FreeRTOS task that manages the MQTT connection and subscriptions.
 * This task connects to the MQTT broker (MQTT v5.0), subscribes to configured topics,
 * and logs incoming data on those topics.
 */
void mqtt_task(void *pvParameters);

/**
 * Publish a message to the MQTT broker on the given topic.
 * This will only publish if MQTT is enabled and currently connected.
 *
 * @param topic   Topic string to publish to.
 * @param payload Message payload (UTF-8 string) to publish.
 */
void mqtt_publish(const char *topic, const char *payload);

#ifdef __cplusplus
}
#endif

#endif /* MQTT_COM_H */