/*
 * 74AHC595.c
 *
 *  Created on: 6 mar 2025
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
#include "driver/gpio.h"

// Initialization
extern status_t status;
static int last_data_bit = -1;


/* Helper function to initialize GPIO */
esp_err_t gpio_init(gpio_int_type_t type, gpio_mode_t mode, gpio_pulldown_t pull_down, gpio_pullup_t pull_up, int no, uint8_t initial_state)
{
    gpio_config_t io_conf = {};

    io_conf.intr_type = type;
    io_conf.mode = mode;
    io_conf.pin_bit_mask = (1ULL << no);
    io_conf.pull_down_en = pull_down;
    io_conf.pull_up_en = pull_up;
    
    if((mode == GPIO_MODE_OUTPUT) || (mode == GPIO_MODE_OUTPUT_OD) || (mode == GPIO_MODE_INPUT_OUTPUT) || (mode == GPIO_MODE_INPUT_OUTPUT_OD))
    	gpio_set_level(no, initial_state);

    return gpio_config(&io_conf);
}

/**
 * @brief Initializes GPIOs, creates the queue, and starts the shift register task.
 */
esp_err_t shift_register_init(void)
{
	esp_err_t err;
	
    // Configure GPIO pins as outputs and set initial levels
    err = gpio_init(GPIO_INTR_DISABLE, GPIO_MODE_OUTPUT, GPIO_PULLDOWN_DISABLE, GPIO_PULLUP_DISABLE, SHIFT_REG_DATA_PIN, 0);
	if(err != ESP_OK){
		return err;
	}
	
	err = gpio_init(GPIO_INTR_DISABLE, GPIO_MODE_OUTPUT, GPIO_PULLDOWN_DISABLE, GPIO_PULLUP_DISABLE, SHIFT_REG_LATCH_PIN, 0);
	if(err != ESP_OK){
		return err;
	}
	
	err = gpio_init(GPIO_INTR_DISABLE, GPIO_MODE_OUTPUT, GPIO_PULLDOWN_DISABLE, GPIO_PULLUP_DISABLE, SHIFT_REG_CLOCK_PIN, 0);
	if(err != ESP_OK){
		return err;
	}
	
	err = gpio_init(GPIO_INTR_DISABLE, GPIO_MODE_INPUT, GPIO_PULLDOWN_DISABLE, GPIO_PULLUP_DISABLE, DETECT_PIN, 0);
	if(err != ESP_OK){
		return err;
	}


	err = gpio_init(GPIO_INTR_DISABLE, GPIO_MODE_OUTPUT, GPIO_PULLDOWN_ENABLE, GPIO_PULLUP_DISABLE, POWER_PIN, 0);
	if(err != ESP_OK){
		return err;
	}
	
	err = gpio_init(GPIO_INTR_DISABLE, GPIO_MODE_INPUT, GPIO_PULLDOWN_ENABLE, GPIO_PULLUP_DISABLE, DETECT_PIN, 0);
	if(err != ESP_OK){
		return err;
	}
    
    return ESP_OK;
}


/**
 * @brief Shifts a single bit into the shift registers using bit-banging.
 *
 * This function sets the DATA pin to the specified bit value, pulses the CLOCK pin,
 * and introduces a small delay to meet the timing requirements of the shift registers.
 *
 * @param bit The bit value to shift in (0 or 1).
 */
void shift_one_bit(uint8_t bit)
{
    // Lower the CLOCK pin before updating DATA.
    gpio_set_level(SHIFT_REG_CLOCK_PIN, 0);
    // Set the DATA pin to the desired bit.
    gpio_set_level(SHIFT_REG_DATA_PIN, bit);
    // Pulse the CLOCK pin high to latch the bit.
    gpio_set_level(SHIFT_REG_CLOCK_PIN, 1);
}

/**
 * @brief Bit-bangs a 16-bit word to the cascaded shift registers, updating DATA only when necessary.
 *
 * This function sends a 16-bit word (MSB first) by manually toggling the clock and data lines.
 * It checks whether the current bit value differs from the previously sent bit (stored in last_data_bit)
 * to minimize unnecessary transitions on the DATA pin.
 *
 * For each bit:
 *   1. Lower the CLOCK pin.
 *   2. Extract the current bit.
 *   3. If the bit differs from the last value sent, update the DATA pin.
 *   4. Wait briefly to meet hardware timing.
 *   5. Raise the CLOCK pin to latch the bit.
 *   6. Wait again before proceeding to the next bit.
 *
 * Finally, both the CLOCK and DATA pins are set to low.
 *
 * @param data The 16-bit data word to be shifted out.
 */
void shift_out_word(uint16_t data)
{
    for (int i = 15; i >= 0; i--)
    {
        // Lower CLOCK before updating DATA.
        gpio_set_level(SHIFT_REG_CLOCK_PIN, 0);
        
        // Extract the current bit (MSB first).
        int bit = (data >> i) & 0x01;
        
        // Update DATA only if the bit has changed.
        if (bit != last_data_bit)
        {
            gpio_set_level(SHIFT_REG_DATA_PIN, bit);
            last_data_bit = bit;
        }
        
        // Short delay for timing purposes.
        //vTaskDelay(10 / portTICK_PERIOD_MS);
        
        // Raise CLOCK to latch the bit into the shift register.
        gpio_set_level(SHIFT_REG_CLOCK_PIN, 1);
        
        // Short delay to ensure the bit is properly latched.
       //vTaskDelay(10 / portTICK_PERIOD_MS);
    }
    
    // After all bits are sent, ensure both CLOCK and DATA are low.
    gpio_set_level(SHIFT_REG_CLOCK_PIN, 0);
    gpio_set_level(SHIFT_REG_DATA_PIN, 0);
}

/**
 * @brief Sends a single 16-bit word to the shift registers with a latch toggle afterward.
 *
 * This function resets the last DATA pin state, sets the LATCH pin low to start the transmission,
 * calls shift_out_word() to transmit the 16-bit word, and then toggles the LATCH pin high (and low)
 * to update the outputs of the shift registers.
 *
 * @param data The 16-bit data word to be sent.
 */
void shift_register_send_word(uint16_t data)
{
    // Reset the DATA pin state.
    last_data_bit = -1;
    
    // Begin transmission by setting LATCH low.
    gpio_set_level(SHIFT_REG_LATCH_PIN, 0);
    
    // Transmit the 16-bit word.
    shift_out_word(data);
    
    // Toggle LATCH to update the outputs.
    gpio_set_level(SHIFT_REG_LATCH_PIN, 1);
    // Optionally, set LATCH back to low.
    gpio_set_level(SHIFT_REG_LATCH_PIN, 0);
}

/**
 * @brief Sends a chain of 16-bit words to cascaded shift registers with a single latch toggle.
 *
 * This function is used when updating a portion of the display chain.
 * It sends an array of 16-bit words consecutively (without toggling LATCH between words)
 * and then toggles LATCH only once at the end to update the outputs.
 *
 * @param data Pointer to an array of 16-bit words to be sent.
 * @param count The number of words in the chain.
 */
void shift_register_send_chain(uint16_t *data, uint8_t count)
{
    // Reset the DATA pin state.
    last_data_bit = -1;
    
    // Start chain transmission by setting LATCH low.
    gpio_set_level(SHIFT_REG_LATCH_PIN, 0);
    
    // Transmit each 16-bit word in the chain.
    for (int i = 0; i < count; i++)
    {
        shift_out_word(data[i]);
    }
    
    // Toggle LATCH once after the chain is transmitted.
    gpio_set_level(SHIFT_REG_LATCH_PIN, 1);
    gpio_set_level(SHIFT_REG_LATCH_PIN, 0);
}

// The function accepts an 8-bit variable 'segs', where:
// bit0 -> segment A, bit1 -> segment B, bit2 -> segment C,
// bit3 -> segment D, bit4 -> segment E, bit5 -> segment F,
// bit6 -> segment G; bit7 is unused.
// The returned value is a 16-bit number, where for each segment:
// 0b01 means the segment is ON, and 0b10 means the segment is OFF,
// packed in the following order: E, D, C, F, G, A, B.
static uint16_t get_symbol_pattern(uint8_t segs) 
{
    uint16_t pattern = 0;
    // Order of segments for packing:
    // "E", "D", "C", "F", "G", "A", "B"
    // corresponding to the input bits: 4, 3, 2, 5, 6, 0, 1.
    int bitMapping[7] = { 4, 3, 2, 5, 6, 0, 1 };

    for (int i = 0; i < 7; i++) 
    {
        // If the input bit corresponding to the segment (according to the mapping) is set,
        // assign the code 0b01 (segment on), otherwise 0b10 (segment off)
        uint16_t bitValue = (segs & (1 << bitMapping[i])) ? 0x01 : 0x02;
        // Calculate the shift - each segment uses 2 bits,
        // and the most significant pair starts at bits 13-12 (decreasing by 2 bits for each subsequent pair)
        int shift = (6 - i) * 2;
        pattern |= (bitValue << shift);
    }

    return pattern;
}

/**
 * @brief Converts a single digit (0–9 or 10 for clear) into its corresponding 16-bit pattern.
 *
 * This helper function uses a predefined mapping for a mechanical 7-segment display.
 * Each digit is converted to a 16-bit word that controls the segments. For an invalid digit
 * or the digit 10 (used for clearing the display), a clear pattern is returned.
 *
 * @param digit The digit to convert (0–9, or 10 to clear the display).
 * @return The 16-bit pattern corresponding to the given digit.
 */
uint16_t get_digit_pattern(uint8_t digit)
{
    uint16_t pattern = 0;
    switch (digit)
    {
	    case 0:  pattern = 0x3F;  break;  // 0b00111111: A B C D E F
	    case 1:  pattern = 0x06;  break;  // 0b00000110:   B C
	    case 2:  pattern = 0x5B;  break;  // 0b01011011: A B   D E   G
	    case 3:  pattern = 0x4F;  break;  // 0b01001111: A B C D     G
	    case 4:  pattern = 0x66;  break;  // 0b01100110:   B C     F G
	    case 5:  pattern = 0x6D;  break;  // 0b01101101: A   C D   F G
	    case 6:  pattern = 0x7D;  break;  // 0b01111101: A   C D E F G
	    case 7:  pattern = 0x07;  break;  // 0b00000111: A B C
	    case 8:  pattern = 0x7F;  break;  // 0b01111111: A B C D E F G
	    case 9:  pattern = 0x6F;  break;  // 0b01101111: A B C D   F G
        // For an invalid digit (or digit 10), return the clear pattern.
        case 10:
        default: pattern = 0x00; break;
    }
    return pattern;
}



/**
 * @brief Displays a single digit on a 7-segment mechanical display.
 *
 * This function converts a digit (0–9 or 10 for clear) into its corresponding 16-bit pattern
 * and sends it to the shift registers. The process is as follows:
 *   1. Send the calculated pattern to enable the motor drivers and set the segments.
 *   2. Wait 100 ms for the mechanical action to complete.
 *   3. Send a 16-bit zero word (all outputs 0) to disable the motor drivers.
 *
 * @param digit The digit to display (0–9) or 10 to clear the display.
 */
void DisplayDigit(uint8_t digit, uint8_t target)
{
    uint16_t pattern = get_digit_pattern(digit);
    
    DisplaySymbol(pattern, target);
}

/**
 * @brief Displays a multi-digit decimal number on cascaded 7-segment mechanical displays sequentially.
 *
 * To avoid a high instantaneous current draw, the displays are updated one-by-one from the most
 * significant digit to the least significant. For each digit:
 *   1. A chain is prepared whose length equals the number of remaining displays.
 *      The first word in the chain contains the new digit's pattern and the remaining words are 0x0000.
 *   2. The chain is sent to update the targeted display.
 *   3. The system waits 100 ms for the mechanical action to complete.
 *   4. A chain of the same length containing all zeros is sent to disable the motor drivers for that display.
 *
 * @param number The decimal number to be displayed.
 */
void DisplayNumber(uint32_t number, uint8_t group)
{
    uint8_t digits[10];  // Buffer to hold up to 10 digits (sufficient for a uint32_t).
    uint8_t count = 0;
    
    // Special case: if number is 0, add a single digit '0'.
    if (number == 0)
    {
         digits[count++] = 0;
    }
    else
    {
         // Extract individual digits (least significant first).
         while (number > 0)
         {
              digits[count] = number % 10;    
              number /= 10;        
              count++;
         }
         // Reverse the digit array so the most significant digit is first.
         for (int i = 0; i < count / 2; i++)
         {
              uint8_t temp = digits[i];
              
              digits[i] = digits[count - 1 - i];
              digits[count - 1 - i] = temp;
         }
    }
      
    // Sequentially update each display starting from the most significant digit.
    // For a number with 'count' digits, the update for the digit at index 'i'
    // is performed by shifting a chain with length = (count - i).
    for (uint8_t i = status.groups[group].start_position; i <= status.groups[group].end_position; i++)
    {    
        DisplayDigit(digits[i - status.groups[group].start_position], i);
    }
}

/**
 * @brief Displays an arbitrary symbol on the selected 7-segment display.
 *
 * This function sends a 16-bit pattern to the specified display module (target),
 * where display numbering is as follows:
 *   - 1 corresponds to the least significant display (rightmost).
 *   - All other displays will be turned off (all segments set to 0).
 *
 * It uses a similar mechanism as the DisplayNumber function to update the shift register chain.
 *
 * @param pattern 16-bit pattern to be displayed.
 * @param target Display number (1 = least significant, etc.).
 */
void DisplaySymbol(uint8_t pattern_raw, uint8_t target)
{
    // Retrieve the number of displays detected (set by detect_display_count).
    uint8_t total = status.display_number;
    uint16_t pattern = get_symbol_pattern(pattern_raw);
    uint16_t chain[status.display_number]; 

    // Check if the given display number is valid.
    if (target > (total - 1))
        return;

    // If the current iteration corresponds to the target display,
    // set the pattern; otherwise, set 0 (turn off).
    for (uint8_t i = 0; i < total; i++) {
        chain[i] = (i == target) ? pattern : 0x0000;
    }
    
    // Enable power to the displays.
    gpio_set_level(POWER_PIN, 1);

    // Send the prepared chain to the shift registers.
    shift_register_send_chain(chain, total);

    // Wait 100 ms to allow for the mechanical movement of the segments.
    vTaskDelay(100 / portTICK_PERIOD_MS);

    
    for (uint8_t i = 0; i < total; i++) {
        chain[i] = 0x0000;
    }
    // Turn off coil  drivers for the updated module by sending zeros.
    shift_register_send_chain(chain, total);

    // Short delay before updating the next display.
    vTaskDelay(1 / portTICK_PERIOD_MS);

    // Disable power to the displays.
    gpio_set_level(POWER_PIN, 0);
}


/**
 * @brief Detects the number of displays (each consisting of two cascaded shift registers) connected in series.
 *
 * This function sends a '1' as the first bit followed by 15 '0's (total 16 bits) into the shift register chain.
 * After each group of 16 clock pulses (which corresponds to one display), it reads the GPIO_NUM_23 pin.
 * If GPIO_NUM_23 is HIGH, it indicates that a display (a pair of shift registers) is connected.
 * The process continues until GPIO_NUM_23 reads LOW after a group of 16 clock pulses,
 * indicating no further display is connected.
 *
 * @return The number of displays detected.
 */
uint8_t detect_display_count(void)
{
    uint8_t display_count = 0, flag = 0;
    
    // Clear the shift registers.
    for (int i = 0; i < (16 * MAX_DISPLAYS); i++)
		shift_one_bit(0);
    
    while (1)
    {
        // For each display, shift out 16 bits:
        // The first bit is set to 1, and the following 15 bits are 0.
        for (int i = 0; i < 16; i++)
        {
            if (flag == 0)
            {
				flag = 1;
                shift_one_bit(1);      
            }
            else
            {
                shift_one_bit(0);
            }           
        }
        
        // Set Clock and Data to LOW
        gpio_set_level(SHIFT_REG_DATA_PIN, 0);
        gpio_set_level(SHIFT_REG_CLOCK_PIN, 0);
        
        // Read the GPIO pin connected to the serial output tap.
        int pin_state = gpio_get_level(DETECT_PIN);
          
        // If the pin is HIGH, a display is present; increment the counter.
        if (pin_state == 1)
            display_count++;
        else
            break;
        
        // Add a small delay to yield control to other tasks and allow watchdog reset.
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }
    
    for(int i = 0; i < 16; i++)
    	shift_one_bit(0);
    
    // Set global variable
    status.display_number = display_count;
    
    ESP_LOGI(DISP, "Display count: %d", display_count);
    
    return display_count;
}