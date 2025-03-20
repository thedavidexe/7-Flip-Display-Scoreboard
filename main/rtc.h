/*
 * rtc.h
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

#ifndef MAIN_RTC_H_
#define MAIN_RTC_H_

#include <stdint.h>
#define RTC "RTC"

enum options {SECOND, MINUTE, HOUR, DAY_OF_WEEK, DATE, MONTH, YEAR, CONTROL, CONTROL_STATUS, AGING_OFFSET, ALARM1, ALARM2, ALARMS, TEMPERATURE, TIME, ALL};
enum square_wave {WAVE_OFF, WAVE_1, WAVE_2, WAVE_3, WAVE_4};
enum run_state {CLOCK_HALT, CLOCK_RUN};
enum ds3231_registers {
  DS3231_REGISTER_SECONDS,
  DS3231_REGISTER_MINUTES,
  DS3231_REGISTER_HOURS,
  DS3231_REGISTER_DAY_OF_WEEK,
  DS3231_REGISTER_DATE,
  DS3231_REGISTER_MONTH,
  DS3231_REGISTER_YEAR,
  DS3231_REGISTER_ALARM1_SECONDS,
  DS3231_REGISTER_ALARM1_MINUTES,
  DS3231_REGISTER_ALARM1_HOURS,
  DS3231_REGISTER_ALARM1_DAY_OF_WEEK_OR_DATE,
  DS3231_REGISTER_ALARM2_MINUTES,
  DS3231_REGISTER_ALARM2_HOURS,
  DS3231_REGISTER_ALARM2_DAY_OF_WEEK_OR_DATE,
  DS3231_REGISTER_CONTROL,
  DS3231_REGISTER_CONTROL_STATUS,
  DS3231_REGISTER_AGING_OFFSET,
  DS3231_REGISTER_ALARM2_TEMP_MSB,
  DS3231_REGISTER_ALARM2_TEMP_LSB
  };

#define I2C_MASTER_SCL_IO           22      			/*!< GPIO number used for I2C master clock */
#define I2C_MASTER_SDA_IO           21      			/*!< GPIO number used for I2C master data  */
#define I2C_MASTER_NUM              0                   /*!< I2C master i2c port number, the number of i2c peripheral interfaces available will depend on the chip */
#define I2C_MASTER_FREQ_HZ          400000              /*!< I2C master clock frequency */
#define I2C_MASTER_TX_BUF_DISABLE   0                   /*!< I2C master doesn't need buffer */
#define I2C_MASTER_RX_BUF_DISABLE   0                   /*!< I2C master doesn't need buffer */
#define I2C_MASTER_TIMEOUT_MS       1000

#define DS3231_I2C_ADDRESS                    0X68

#define FORCE_RESET                           0X00
#define NO_FORCE_RESET                        0X01
#define NO_RESET                              0X02
#define DS3231_IS_RUNNING                     0X01
#define DS3231_IS_STOPPED                     0X00
#define OPERATION_DONE                        0X01
#define OPERATION_FAILED                      0X00
#define DS3231_NOT_INITIALIZED                0X01        /*bit OSF == 1 indicates that the oscillator was stopped*/
#define DS3231_INITIALIZED                    0X00        /*bit OSF == 0 indicates that the oscillator was running before mcu was powered on*/

#define DS3231_BIT_12_24                      0X06
#define DS3231_BIT_CENTURY                    0X07
#define DS3231_BIT_A1M1                       0X07
#define DS3231_BIT_A1M2                       0X07
#define DS3231_BIT_A1M3                       0X07
#define DS3231_BIT_A1M4                       0X07
#define DS3231_BIT_A2M2                       0X07
#define DS3231_BIT_A3M3                       0X07
#define DS3231_BIT_A4M4                       0X07
#define DS3231_BIT_12_24_ALARM1               0X06
#define DS3231_BIT_12_24_ALARM2               0X06
#define DS3231_BIT_DY_DT_ALARM1               0X06
#define DS3231_BIT_DY_DT_ALARM2               0X06
#define DS3231_BIT_A1IE                       0X00
#define DS3231_BIT_A2IE                       0X01
#define DS3231_BIT_INTCN                      0X02
#define DS3231_BIT_RS1                        0X03
#define DS3231_BIT_RS2                        0X04
#define DS3231_BIT_CONV                       0X05
#define DS3231_BIT_BBSQW                      0X06
#define DS3231_BIT_EOSC                       0X07
#define DS3231_BIT_A1F                        0X00
#define DS3231_BIT_A2F                        0X01
#define DS3231_BIT_BSY                        0X02
#define DS3231_BIT_EN32KHZ                    0X03
#define DS3231_BIT_OSF                        0X07

#define DS3231_REGISTER_SECONDS_DEFAULT                       0X00
#define DS3231_REGISTER_MINUTES_DEFAULT                       0X00
#define DS3231_REGISTER_HOURS_DEFAULT                         0X00
#define DS3231_REGISTER_DAY_OF_WEEK_DEFAULT                   0X01
#define DS3231_REGISTER_DATE_DEFAULT                          0X01
#define DS3231_REGISTER_MONTH_DEFAULT                         0X01
#define DS3231_REGISTER_YEAR_DEFAULT                          0X00
#define DS3231_REGISTER_ALARM1_SECONDS_DEFAULT                0X00
#define DS3231_REGISTER_ALARM1_MINUTES_DEFAULT                0X00
#define DS3231_REGISTER_ALARM1_HOURS_DEFAULT                  0X00
#define DS3231_REGISTER_ALARM1_DAY_OF_WEEK_OR_DATE_DEFAULT    0X00
#define DS3231_REGISTER_ALARM2_MINUTES_DEFAULT                0X00
#define DS3231_REGISTER_ALARM2_HOURS_DEFAULT                  0X00
#define DS3231_REGISTER_ALARM2_DAY_OF_WEEK_OR_DATE_DEFAULT    0X00
#define DS3231_REGISTER_CONTROL_DEFAULT                       0X1C
#define DS3231_REGISTER_CONTROL_STATUS_DEFAULT                0X00
#define DS3231_REGISTER_AGING_OFFSET_DEFAULT                  0X00

void RTCHandlingTask(void *arg);

void ds3231_reset(uint8_t input);
void ds3231_init(uint8_t *data_array, uint8_t run_command, uint8_t reset_state);
void ds3231_init_status_update();
uint8_t ds3231_read(uint8_t registers, uint8_t *data_array);
uint8_t ds3231_set(uint8_t registers, uint8_t *data_array);
uint8_t ds3231_init_status_report();
uint8_t ds3231_run_command(uint8_t command);
uint8_t ds3231_run_status();

void ds3231_I2C_init();
void time_i2c_write_single(uint8_t device_address, uint8_t register_address, uint8_t* data_byte);
void time_i2c_write_multi(uint8_t device_address, uint8_t start_register_address, uint8_t* data_array, uint8_t data_length);
void time_i2c_read_single(uint8_t device_address, uint8_t register_address, uint8_t* data_byte);
void time_i2c_read_multi(uint8_t device_address, uint8_t start_register_address, uint8_t* data_array, uint8_t data_length);
void set_time(uint32_t data);
void read_time(void);
void TurnOffCurrentMeal(uint8_t meal);

#endif /* MAIN_RTC_H_ */
