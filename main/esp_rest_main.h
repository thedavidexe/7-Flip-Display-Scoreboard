/*
 * esp_rest_main.h
 *
 *  Created on: 21 mar 2025
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

#ifndef MAIN_ESP_REST_MAIN_H_
#define MAIN_ESP_REST_MAIN_H_

#define SERVER	"SERVER"
#define MDNS_INSTANCE "7-Flip Display Server"
#define CONFIG_EXAMPLE_MDNS_HOST_NAME "esp-home"
#define CONFIG_EXAMPLE_WEB_MOUNT_POINT "/www"

/* Device Wi-Fi operating modes for persistent storage */
#define MODE_AP  0   // Hotspot (Access Point) mode
#define MODE_STA 1   // Wi-Fi Client (Station) mode

void RestfulServerTask(void *arg);

#endif /* MAIN_ESP_REST_MAIN_H_ */
