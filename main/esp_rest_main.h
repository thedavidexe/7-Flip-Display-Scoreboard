/*
 * esp_rest_main.h
 *
 *  Created on: 21 mar 2025
 *      Author: Sebastian Sokołowski
 *      Company: Smart Solutions for Home
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef MAIN_ESP_REST_MAIN_H_
#define MAIN_ESP_REST_MAIN_H_

#define SERVER  "SERVER"
#define MDNS_INSTANCE "7-Flip Display Server"
#define CONFIG_EXAMPLE_MDNS_HOST_NAME "flip-display"
#define CONFIG_WWW_0_MOUNT_POINT "/www_0"
#define CONFIG_WWW_1_MOUNT_POINT "/www_1"

/* Device Wi-Fi operating modes for persistent storage */
#define MODE_AP  0   // Access Point mode
#define MODE_STA 1   // Station mode

void RestfulServerTask(void *arg);
void notify_pattern_change(void);

#endif /* MAIN_ESP_REST_MAIN_H_ */
