/*
 * status_led.c
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
#include "status_led.h"

extern status_t status;

/* Function to set an alarm */
void SetAlarm(char alarm)
{
    status.alert |= alarm;  // Set the corresponding bit
}

/* Function to clear an alarm */
void ClearAlarm(uint16_t alarm)
{
    status.alert &= ~alarm;  // Clear the corresponding bit
}

/* Function to check if an alarm is set */
bool GetAlarm(uint16_t alarm)
{
    return (status.alert & alarm) != 0;  // Check if the corresponding bit is set
}
