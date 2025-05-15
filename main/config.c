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

// Helper function to parse a separator string to an enum value
enum pp_separator_t parse_separator(const char *sep_str)
{
    if (!sep_str) return SEP_NULL;
    if (strcasecmp(sep_str, "colon") == 0) return SEP_COLON;
    else if (strcasecmp(sep_str, "space") == 0) return SEP_SPACE;
    else if (strcasecmp(sep_str, "blank") == 0) return SEP_BLANK;
    else if (strcasecmp(sep_str, "dot") == 0) return SEP_DOT;
    else if (strcasecmp(sep_str, "dash") == 0) return SEP_DASH;
    return SEP_NULL;
}

// Helper function to convert a separator enum value to a string
const char *separator_to_string(enum pp_separator_t sep)
{
    switch(sep){
        case SEP_COLON: return "colon";
        case SEP_SPACE: return "space";
        case SEP_BLANK: return "blank";
        case SEP_DOT: return "dot";
        case SEP_DASH: return "dash";
        default: return NULL; // For SEP_NULL, return NULL so that JSON gets a null value
    }
}

// Helper function to parse a mode string to an enum value
enum pp_mode_t parse_mode(const char *mode_str)
{
    if (!mode_str) return MODE_NONE;
    if (strcasecmp(mode_str, "none") == 0) return MODE_NONE;
    else if (strcasecmp(mode_str, "mqtt") == 0) return MODE_MQTT;
    else if (strcasecmp(mode_str, "timer") == 0) return MODE_TIMER;
    else if (strcasecmp(mode_str, "clock") == 0) return MODE_CLOCK;
    else if (strcasecmp(mode_str, "mannual") == 0 || strcasecmp(mode_str, "manual") == 0) return MODE_MANUAL;
    else if (strcasecmp(mode_str, "custom-api") == 0) return MODE_CUSTOM_API;
    return MODE_NONE;
}

// Helper function to convert a mode enum value to a string
const char *mode_to_string(enum pp_mode_t mode)
{
    switch(mode){
        case MODE_NONE: return "none";
        case MODE_MQTT: return "mqtt";
        case MODE_TIMER: return "timer";
        case MODE_CLOCK: return "clock";
        case MODE_MANUAL: return "mannual";
        case MODE_CUSTOM_API: return "custom-api";
        default: return "none";
    }
}