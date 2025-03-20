/*
 * rtc.c
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

#include "hal/gpio_types.h"
#include "main.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/i2c.h"
#include "driver/i2c_master.h"
#include <stdint.h>

#include "rtc.h"

extern status_t status;

uint8_t just_once_flag = 0;

static void BCD_to_HEX(uint8_t *data_array, uint8_t array_length);        /*turns the bcd time values from ds3231 into hex*/
static void HEX_to_BCD(uint8_t *data_array, uint8_t array_length);        /*turns the hex numbers into bcd, to be written back into ds3231*/
static void ds3231_data_clone(uint8_t option, uint8_t *input_array);        /*clones an array into one of 3 ..._registers_clone[], based on chosen option*/

static uint8_t register_current_value;        /*used to read current values of ds3231 registers*/
static uint8_t register_new_value;        /*used to write new values to ds3231 registers*/
static uint8_t time_registers_clone[7];       /*used for the purpose of not curropting the time settings by reconverting an already converted HEX to BCD array*/
static uint8_t alarm1_registers_clone[4];       /*used for the purpose of not curropting the time settings by reconverting an already converted HEX to BCD array*/
static uint8_t alarm2_registers_clone[3];       /*used for the purpose of not curropting the time settings by reconverting an already converted HEX to BCD array*/
static uint8_t register_default_value[] = {       /*used in reset function, contains default values*/
  DS3231_REGISTER_SECONDS_DEFAULT,
  DS3231_REGISTER_MINUTES_DEFAULT,
  DS3231_REGISTER_HOURS_DEFAULT,
  DS3231_REGISTER_DAY_OF_WEEK_DEFAULT,
  DS3231_REGISTER_DATE_DEFAULT,
  DS3231_REGISTER_MONTH_DEFAULT,
  DS3231_REGISTER_YEAR_DEFAULT,
  DS3231_REGISTER_ALARM1_SECONDS_DEFAULT,
  DS3231_REGISTER_ALARM1_MINUTES_DEFAULT,
  DS3231_REGISTER_ALARM1_HOURS_DEFAULT,
  DS3231_REGISTER_ALARM1_DAY_OF_WEEK_OR_DATE_DEFAULT,
  DS3231_REGISTER_ALARM2_MINUTES_DEFAULT,
  DS3231_REGISTER_ALARM2_HOURS_DEFAULT,
  DS3231_REGISTER_ALARM2_DAY_OF_WEEK_OR_DATE_DEFAULT,
  DS3231_REGISTER_CONTROL_DEFAULT,
  DS3231_REGISTER_CONTROL_STATUS_DEFAULT,
  DS3231_REGISTER_AGING_OFFSET_DEFAULT,
};

//LOW level API
/*function to transmit one byte of data to register_address on ds3231 (device_address: 0X68)*/
void time_i2c_write_single(uint8_t device_address, uint8_t register_address, uint8_t* data_byte)
{
    uint8_t write_buf[2] = {register_address, *data_byte};

    i2c_master_write_to_device(I2C_MASTER_NUM, device_address, write_buf, sizeof(write_buf), I2C_MASTER_TIMEOUT_MS / portTICK_PERIOD_MS);
	//i2c_master_transmit(I2C_MASTER_NUM, write_buf, 2, I2C_MASTER_TIMEOUT_MS / portTICK_PERIOD_MS);
}

/*function to transmit an array of data to device_address, starting from start_register_address*/
void time_i2c_write_multi(uint8_t device_address, uint8_t start_register_address, uint8_t *data_array, uint8_t data_length)
{
    int i;
    uint8_t write_buf[1 + data_length];

    write_buf[0] = start_register_address;

    for(i = 1; i < data_length + 1; i++)
    	write_buf[i] = data_array[i-1];

    i2c_master_write_to_device(I2C_MASTER_NUM, device_address, write_buf, data_length + 1, I2C_MASTER_TIMEOUT_MS / portTICK_PERIOD_MS);
	//i2c_master_transmit(I2C_MASTER_NUM, write_buf, 2, I2C_MASTER_TIMEOUT_MS / portTICK_PERIOD_MS);
}

/*function to read one byte of data from register_address on ds3231*/
void time_i2c_read_single(uint8_t device_address, uint8_t register_address, uint8_t* data_byte)
{
	i2c_master_write_read_device(I2C_MASTER_NUM, device_address, &register_address, 1, data_byte, 1, I2C_MASTER_TIMEOUT_MS / portTICK_PERIOD_MS);
}

/*function to read an array of data from device_address*/
void time_i2c_read_multi(uint8_t device_address, uint8_t start_register_address, uint8_t* data_array, uint8_t data_length)
{
	i2c_master_write_read_device(I2C_MASTER_NUM, device_address, &start_register_address, 1, data_array, data_length, I2C_MASTER_TIMEOUT_MS / portTICK_PERIOD_MS);
}

/*function to initialize I2C peripheral in 100khz or 400khz*/
void ds3231_I2C_init()
{
	int i2c_master_port = I2C_MASTER_NUM;
	esp_err_t err;


	i2c_config_t conf = {
		.mode = I2C_MODE_MASTER,
		.sda_io_num = I2C_MASTER_SDA_IO,
		.scl_io_num = I2C_MASTER_SCL_IO,
		.sda_pullup_en = GPIO_PULLUP_ENABLE,
		.scl_pullup_en = GPIO_PULLUP_ENABLE,
		.master.clk_speed = I2C_MASTER_FREQ_HZ,
	};

	err = i2c_param_config(i2c_master_port, &conf);
	if(err != ESP_OK)
	{
		SetAlarm(HARDWARE_PROBLEM);
	}

	i2c_driver_install(i2c_master_port, conf.mode, I2C_MASTER_RX_BUF_DISABLE, I2C_MASTER_TX_BUF_DISABLE, 0);

}

/*ds3231_init function accepts 3 inputs, data_array[7] is the new time settings,
  run_state commands ds3231 to run or halt (CLOCK_RUN and CLOCK_HALT), and reset_state
  could force-reset ds3231 (FORCE_RESET) or checks if ds3231 is reset beforehand
  (NO_FORCE_RESET)*/
void ds3231_init(uint8_t *data_array, uint8_t run_command, uint8_t reset_state)
{
	ds3231_I2C_init();

	if (((ds3231_init_status_report() == DS3231_NOT_INITIALIZED) && (reset_state == NO_FORCE_RESET)) || (reset_state == FORCE_RESET))
	{
	ds3231_reset(ALL);
	ds3231_set(TIME, data_array);
	}
	ds3231_init_status_update();        /*now the device is initialized (DS3231_INITIALIZED)*/
	ds3231_run_command(run_command);
}

/*function to command ds3231 to stop or start updating its time registers, WORKS ONLY WITH BATTERY BACKED DS3231*/
uint8_t ds3231_run_command(uint8_t command)
{
  switch (command)
  {
    case CLOCK_RUN:
      time_i2c_read_single(DS3231_I2C_ADDRESS, DS3231_REGISTER_CONTROL, &register_current_value);
      register_new_value = register_current_value & (~(1 << DS3231_BIT_EOSC));
      time_i2c_write_single(DS3231_I2C_ADDRESS, DS3231_REGISTER_CONTROL, &register_new_value);
      return OPERATION_DONE;
    case CLOCK_HALT:
      time_i2c_read_single(DS3231_I2C_ADDRESS, DS3231_REGISTER_CONTROL, &register_current_value);
      register_new_value = register_current_value | (1 << DS3231_BIT_EOSC);
      time_i2c_write_single(DS3231_I2C_ADDRESS, DS3231_REGISTER_CONTROL, &register_new_value);
      return OPERATION_DONE;
    default:
      return OPERATION_FAILED;
  }
}

/*function to check the status of ds3231, whether its running or not. WORKS ONLY WITH BATTERY BACKED DS3231*/
uint8_t ds3231_run_status()
{
  time_i2c_read_single(DS3231_I2C_ADDRESS, DS3231_REGISTER_CONTROL, &register_current_value);
  if ((register_current_value & (1 << DS3231_BIT_EOSC)) == 0)
    return CLOCK_RUN;
  else
    return CLOCK_HALT;
}

/*function to read the oscillator flag OSF and to decide whether it has been reset beforehand or not*/
uint8_t ds3231_init_status_report()
{
  time_i2c_read_single(DS3231_I2C_ADDRESS, DS3231_REGISTER_CONTROL_STATUS, &register_current_value);
  if (register_current_value & (1 << DS3231_BIT_OSF))
    return DS3231_NOT_INITIALIZED;
  else
    return DS3231_INITIALIZED;
}

/*function to reset the OSF bit (OSF = 0)*/
void ds3231_init_status_update()
{
  time_i2c_read_single(DS3231_I2C_ADDRESS, DS3231_REGISTER_CONTROL_STATUS, &register_current_value);
  register_new_value = register_current_value & (~(1 << DS3231_BIT_OSF));
  time_i2c_write_single(DS3231_I2C_ADDRESS, DS3231_REGISTER_CONTROL_STATUS, &register_new_value);
}

/*resets the desired register(s), without affecting run_state (RUN_STATE ONLY MAKES SENSE WITH BATTERY-BACKED DS3231*/
void ds3231_reset(uint8_t option)
{
  /*data_clone function together with registers_clone static variables, prevent data curroption caused by reconvertig HEX to BCD*/
  ds3231_data_clone(TIME, &register_default_value[0]);
  ds3231_data_clone(ALARM1, &register_default_value[7]);
  ds3231_data_clone(ALARM2, &register_default_value[0X0B]);
  HEX_to_BCD(&time_registers_clone[0], 7);
  HEX_to_BCD(&alarm1_registers_clone[0], 4);
  HEX_to_BCD(&alarm2_registers_clone[0], 3);

  switch (option)
  {
    case SECOND:
      time_i2c_write_single(DS3231_I2C_ADDRESS, DS3231_REGISTER_SECONDS, &time_registers_clone[0]);
      break;
    case MINUTE:
      time_i2c_write_single(DS3231_I2C_ADDRESS, DS3231_REGISTER_MINUTES, &time_registers_clone[1]);
      break;
    case HOUR:
      time_registers_clone[2] &= (~(1 << DS3231_BIT_12_24));        /*to turn on 24 hours format by default*/
      time_i2c_write_single(DS3231_I2C_ADDRESS, DS3231_REGISTER_HOURS, &time_registers_clone[2]);
      break;
    case DAY_OF_WEEK:
      time_i2c_write_single(DS3231_I2C_ADDRESS, DS3231_REGISTER_DAY_OF_WEEK, &time_registers_clone[3]);
      break;
    case DATE:
      time_i2c_write_single(DS3231_I2C_ADDRESS, DS3231_REGISTER_DATE, &time_registers_clone[4]);
      break;
    case MONTH:
      time_registers_clone[5] &= (~(1 << DS3231_BIT_CENTURY));
      time_i2c_write_single(DS3231_I2C_ADDRESS, DS3231_REGISTER_MONTH, &time_registers_clone[5]);
      break;
    case YEAR:
      time_i2c_write_single(DS3231_I2C_ADDRESS, DS3231_REGISTER_YEAR, &time_registers_clone[6]);
      break;
    case CONTROL:
      time_i2c_read_single(DS3231_I2C_ADDRESS, DS3231_REGISTER_CONTROL, &register_current_value);       /*in order to preserve running state (RUN or HALT)*/
      register_new_value = (register_current_value & (1 << DS3231_BIT_EOSC)) | (register_default_value[0X0E] & (~(1 << DS3231_BIT_EOSC)));
      time_i2c_write_single(DS3231_I2C_ADDRESS, DS3231_REGISTER_CONTROL, &register_new_value);
      break;
    case CONTROL_STATUS:
      time_i2c_read_single(DS3231_I2C_ADDRESS, DS3231_REGISTER_CONTROL_STATUS, &register_current_value);       /*in order to preserve OSF flag*/
      register_new_value = (register_current_value & (1 << DS3231_BIT_OSF)) | (register_default_value[0X0F] & (~(1 << DS3231_BIT_EOSC)));
      time_i2c_write_single(DS3231_I2C_ADDRESS, DS3231_REGISTER_CONTROL_STATUS, &register_new_value);
      break;
    case ALARM1:
      time_i2c_write_multi(DS3231_I2C_ADDRESS, DS3231_REGISTER_ALARM1_SECONDS, &alarm1_registers_clone[0], 4);
      break;
    case ALARM2:
      time_i2c_write_multi(DS3231_I2C_ADDRESS, DS3231_REGISTER_ALARM2_MINUTES, &alarm2_registers_clone[0], 3);
      break;
    case ALARMS:
      time_i2c_write_multi(DS3231_I2C_ADDRESS, DS3231_REGISTER_ALARM1_SECONDS, &alarm1_registers_clone[0], 4);
      time_i2c_write_multi(DS3231_I2C_ADDRESS, DS3231_REGISTER_ALARM2_MINUTES, &alarm2_registers_clone[0], 3);
      break;
    case AGING_OFFSET:
      register_new_value = DS3231_REGISTER_AGING_OFFSET_DEFAULT;
      time_i2c_write_single(DS3231_I2C_ADDRESS, DS3231_REGISTER_AGING_OFFSET, &register_new_value);
      break;
    case TIME:
      time_registers_clone[2] &= (~(1 << DS3231_BIT_12_24));
      time_registers_clone[5] &= (~(1 << DS3231_BIT_CENTURY));
      time_i2c_write_multi(DS3231_I2C_ADDRESS, DS3231_REGISTER_SECONDS, &time_registers_clone[0], 7);
      break;
    case ALL:
      /*TIME registers reset*/
      time_registers_clone[2] &= (~(1 << DS3231_BIT_12_24));        /*to preserve 24 hours mode*/
      time_registers_clone[5] &= (~(1 << DS3231_BIT_CENTURY));        /*resetting century bit*/
      time_i2c_write_multi(DS3231_I2C_ADDRESS, DS3231_REGISTER_SECONDS, &time_registers_clone[0], 7);       /*to reset all the TIME registers*/
      /*CONTROL and CONTROL_STATUS registers reset*/
      time_i2c_read_single(DS3231_I2C_ADDRESS, DS3231_REGISTER_CONTROL_STATUS, &register_current_value);       /*in order to preserve OSF flag*/
      register_new_value = (register_current_value & (1 << DS3231_BIT_OSF)) | (register_default_value[0X0F] & (~(1 << DS3231_BIT_EOSC)));
      time_i2c_write_single(DS3231_I2C_ADDRESS, DS3231_REGISTER_CONTROL_STATUS, &register_new_value);
      time_i2c_read_single(DS3231_I2C_ADDRESS, DS3231_REGISTER_CONTROL, &register_current_value);       /*to preserve run_status, either RUN or HALT*/
      register_new_value = (register_current_value & (1 << DS3231_BIT_EOSC)) | (register_default_value[0X0E] & (~(1 << DS3231_BIT_EOSC)));
      time_i2c_write_single(DS3231_I2C_ADDRESS, DS3231_REGISTER_CONTROL, &register_new_value);
      /*AGING_OFFSET registers reset*/
      register_new_value = DS3231_REGISTER_AGING_OFFSET_DEFAULT;
      time_i2c_write_single(DS3231_I2C_ADDRESS, DS3231_REGISTER_AGING_OFFSET, &register_new_value);
      break;
    default:
      break;
  }
}

/*function to read internal registers of ds3231, one register at a time or an array of registers*/
uint8_t ds3231_read(uint8_t option, uint8_t *data_array)
{
  switch (option)
  {
    case SECOND:
      time_i2c_read_single(DS3231_I2C_ADDRESS, DS3231_REGISTER_SECONDS, &register_current_value);
      *data_array = register_current_value;
      BCD_to_HEX(data_array, 1);
      break;
    case MINUTE:
      time_i2c_read_single(DS3231_I2C_ADDRESS, DS3231_REGISTER_MINUTES, &register_current_value);
      *data_array = register_current_value;
      BCD_to_HEX(data_array, 1);
      break;
    case HOUR:
      time_i2c_read_single(DS3231_I2C_ADDRESS, DS3231_REGISTER_HOURS, &register_current_value);
      *data_array = register_current_value;
      BCD_to_HEX(data_array, 1);
      break;
    case DAY_OF_WEEK:
      time_i2c_read_single(DS3231_I2C_ADDRESS, DS3231_REGISTER_DAY_OF_WEEK, &register_current_value);
      *data_array = register_current_value;
      BCD_to_HEX(data_array, 1);
      break;
    case DATE:
      time_i2c_read_single(DS3231_I2C_ADDRESS, DS3231_REGISTER_DATE, &register_current_value);
      *data_array = register_current_value;
      BCD_to_HEX(data_array, 1);
      break;
    case MONTH:
      time_i2c_read_single(DS3231_I2C_ADDRESS, DS3231_REGISTER_MONTH, &register_current_value);
      *data_array = register_current_value;
      BCD_to_HEX(data_array, 1);
      break;
    case YEAR:
      time_i2c_read_single(DS3231_I2C_ADDRESS, DS3231_REGISTER_YEAR, &register_current_value);
      *data_array = register_current_value;
      BCD_to_HEX(data_array, 1);
      break;
    case CONTROL:
      time_i2c_read_single(DS3231_I2C_ADDRESS, DS3231_REGISTER_CONTROL, &register_current_value);
      *data_array = register_current_value;
      break;
    case CONTROL_STATUS:
      time_i2c_read_single(DS3231_I2C_ADDRESS, DS3231_REGISTER_CONTROL_STATUS, &register_current_value);
      *data_array = register_current_value;
      break;
    case AGING_OFFSET:
      time_i2c_read_single(DS3231_I2C_ADDRESS, DS3231_REGISTER_AGING_OFFSET, &register_current_value);
      *data_array = register_current_value;
      break;
    case TIME:
      time_i2c_read_multi(DS3231_I2C_ADDRESS, DS3231_REGISTER_SECONDS, data_array, 7);
      BCD_to_HEX(data_array, 7);
      break;
    default:
      return OPERATION_FAILED;
  }
  return OPERATION_DONE;
}

/*function to set internal registers of ds3231, one register at a time or an array of registers*/
uint8_t ds3231_set(uint8_t option, uint8_t *data_array)
{
  switch (option)
  {
    case SECOND:
      time_registers_clone[0] = *data_array;
      HEX_to_BCD(&time_registers_clone[0], 1);
      time_i2c_write_single(DS3231_I2C_ADDRESS, DS3231_REGISTER_SECONDS, &time_registers_clone[0]);
      break;
    case MINUTE:
      time_registers_clone[1] = *data_array;
      HEX_to_BCD(&time_registers_clone[1], 1);
      time_i2c_write_single(DS3231_I2C_ADDRESS, DS3231_REGISTER_MINUTES, &time_registers_clone[1]);
      break;
    case HOUR:
      time_registers_clone[2] = *data_array;
      HEX_to_BCD(&time_registers_clone[2], 1);
      time_i2c_write_single(DS3231_I2C_ADDRESS, DS3231_REGISTER_HOURS, &time_registers_clone[2]);
      break;
    case DAY_OF_WEEK:
      time_registers_clone[3] = *data_array;
      HEX_to_BCD(&time_registers_clone[3], 1);
      time_i2c_write_single(DS3231_I2C_ADDRESS, DS3231_REGISTER_DAY_OF_WEEK, &time_registers_clone[3]);
      break;
    case DATE:
      time_registers_clone[4] = *data_array;
      HEX_to_BCD(&time_registers_clone[4], 1);
      time_i2c_write_single(DS3231_I2C_ADDRESS, DS3231_REGISTER_DATE, &time_registers_clone[4]);
      break;
    case MONTH:
      time_registers_clone[5] = *data_array;
      HEX_to_BCD(&time_registers_clone[5], 1);
      time_i2c_write_single(DS3231_I2C_ADDRESS, DS3231_REGISTER_MONTH, &time_registers_clone[5]);
      break;
    case YEAR:
      time_registers_clone[6] = *data_array;
      HEX_to_BCD(&time_registers_clone[6], 1);
      time_i2c_write_single(DS3231_I2C_ADDRESS, DS3231_REGISTER_YEAR, &time_registers_clone[6]);
      break;
    case CONTROL:
      time_i2c_read_single(DS3231_I2C_ADDRESS, DS3231_REGISTER_CONTROL, &register_current_value);
      register_new_value = (register_current_value & (1 << DS3231_BIT_EOSC)) | (*data_array & (~(1 << DS3231_BIT_EOSC)));
      time_i2c_write_single(DS3231_I2C_ADDRESS, DS3231_REGISTER_CONTROL, &register_new_value);
      break;
    case CONTROL_STATUS:
      time_i2c_read_single(DS3231_I2C_ADDRESS, DS3231_REGISTER_CONTROL_STATUS, &register_current_value);
      register_new_value = (register_current_value & (1 << DS3231_BIT_OSF)) | (*data_array & (~(1 << DS3231_BIT_OSF)));
      time_i2c_write_single(DS3231_I2C_ADDRESS, DS3231_REGISTER_CONTROL_STATUS, &register_new_value);
      break;
    case TIME:
      ds3231_data_clone(TIME, data_array);
      HEX_to_BCD(&time_registers_clone[0], 7);
      time_i2c_write_multi(DS3231_I2C_ADDRESS, DS3231_REGISTER_SECONDS, &time_registers_clone[0], 7);
      break;
    case AGING_OFFSET:
      register_new_value = *data_array;
      time_i2c_write_single(DS3231_I2C_ADDRESS, DS3231_REGISTER_AGING_OFFSET, &register_new_value);
      break;
    default:
      return OPERATION_FAILED;
  }
  return OPERATION_DONE;
}

/*to clone the desired data and prevent reconversion of BCD to HEX*/
static void ds3231_data_clone(uint8_t option, uint8_t *input_array)
{
  switch (option)
  {
    case TIME:
      for (uint8_t counter = 0; counter < 7; counter++)
      {
        time_registers_clone[counter] = input_array[counter];
      }
      break;
    case ALARM1:
      for (uint8_t counter = 0; counter < 4; counter++)
      {
        alarm1_registers_clone[counter] = input_array[counter];
      }
      break;
    case ALARM2:
      for (uint8_t counter = 0; counter < 3; counter++)
      {
        alarm2_registers_clone[counter] = input_array[counter];
      }
      break;
    default:
      break;
  }
}

/*internal function related to this file and not accessible from outside*/
static void BCD_to_HEX(uint8_t *data_array, uint8_t array_length)
{
  for (int8_t index = (array_length - 1); index >= 0; index--)
  {
    data_array[index] = ((data_array[index] >> 4) << 1) + ((data_array[index] >> 4) << 3) + (data_array[index] & 0X0F);
  }
}

/*internal function related to this file and not accessible from outside*/
static void HEX_to_BCD(uint8_t *data_array, uint8_t array_length)
{
  uint8_t temporary_value;
  for (int8_t index = (array_length - 1); index >= 0; index--)
  {
    temporary_value = 0;
    while (((int8_t)data_array[index] - 0X0A) >= 0)
    {
      temporary_value += 0X10;
      data_array[index] -= 0X0A;
    }
    temporary_value += data_array[index];
    data_array[index] = temporary_value;
  }
}

void time_24_to_12(uint8_t time24, uint8_t *time12, uint8_t *am_pm)
{
	if(time24 == 0)
	{
		*time12 = 12;
		*am_pm = 0;
	}
	else if((time24 > 0) && (time24 < 12))
	{
		*time12 = time24;
		*am_pm = 0;
	}
	else if(time24 == 12)
	{
		*time12 = time24;
		*am_pm = 1;
	}
	else if((time24 > 12) && (time24 <= 23))
	{
		*time12 = time24 - 12;
		*am_pm = 1;
	}

}


void read_time(void)
{
	static uint32_t time;
	static uint8_t time_read[7];
	static uint8_t default_time_array[7] = {0, 18, 8, 5, 8, 6, 23};

	ds3231_read(TIME, time_read);

	status.rtc.second = time_read[0];
	status.rtc.minute = time_read[1];

	if(time_read[2] == 0)
	{
		status.rtc.hour = 12;
		status.rtc.am_pm = 0;
	}
	else if((time_read[2] > 0) && (time_read[2] < 12))
	{
		status.rtc.hour = time_read[2];
		status.rtc.am_pm = 0;
	}
	else if(time_read[2] == 12)
	{
		status.rtc.hour = time_read[2];
		status.rtc.am_pm = 1;
	}
	else if((time_read[2] > 12) && (time_read[2] <= 23))
	{
		status.rtc.hour = time_read[2] - 12;
		status.rtc.am_pm = 1;
	}

	status.rtc.day = time_read[4];
	status.rtc.month = time_read[5];
	status.rtc.year = time_read[6];

	time = 0;

	time |= status.rtc.second;
	time |= status.rtc.minute << 6;
	time |= status.rtc.hour << 12;
	time |= status.rtc.am_pm << 16;
	time |= status.rtc.day << 17;
	time |= status.rtc.month  << 22;
	time |= status.rtc.year << 26;


	/* If the read time is the same era then it means the RTC has never been started so it can be configured to the default value */
	if(time == 0)
		ds3231_init(default_time_array, CLOCK_RUN, NO_FORCE_RESET);

	if(just_once_flag == 0)
	{
		just_once_flag = 0; //disabled

		if(status.rtc.am_pm == 1) //PM
			ESP_LOGI(RTC, "Time %02d:%02d PM  Data %d.%d.%d", status.rtc.hour, status.rtc.minute, status.rtc.day, status.rtc.month, status.rtc.year);
		else //AM
			ESP_LOGI(RTC, "Time %02d:%02d AM  Data %d.%d.%d", status.rtc.hour, status.rtc.minute, status.rtc.day, status.rtc.month, status.rtc.year);
	}

	//ESP_LOGI(RTC, "%d %d %d %d %d %d %d\r\n", time_read[0], time_read[1], time_read[2], time_read[3], time_read[4], time_read[5], time_read[6]);
}

void set_time(uint32_t data)
{
	static uint8_t time_read[7], time_write[7];

	status.rtc.second = data & 0x3F;
	status.rtc.minute = (data & 0xFC0) >> 6;
	status.rtc.hour = (data & 0xF000) >> 12;
	status.rtc.am_pm = (data & 0x10000) >> 16;
	status.rtc.day = (data & 0x3E0000) >> 17;
	status.rtc.month = (data & 0x3C00000) >> 22;
	status.rtc.year = (data & 0xFC000000) >> 26;


	time_write[0] = status.rtc.second;
	time_write[1] = status.rtc.minute;

	if(status.rtc.am_pm == 1)
		time_write[2] = status.rtc.hour + 12;
	else
		time_write[2] = status.rtc.hour;

	time_write[3] = 1;
	time_write[4] = status.rtc.day;
	time_write[5] = status.rtc.month;
	time_write[6] = status.rtc.year;


//	ESP_LOGI(RTC, "%d %d %d %d %d %d %d\r\n", status.rtc.second, status.rtc.minute, status.rtc.hour, status.rtc.am_pm, status.rtc.day, status.rtc.month, status.rtc.year);
//	ESP_LOGI(RTC, "%d %d %d %d %d %d %d\r\n", time_write[0], time_write[1], time_write[2], time_write[3], time_write[4], time_write[5], time_write[6]);


	ds3231_set(TIME, time_write);
	vTaskDelay(1000/ portTICK_PERIOD_MS);

	ds3231_read(TIME, time_read);

	if(status.rtc.am_pm == 1) //PM
		ESP_LOGI(RTC, "Time %02d:%02d PM   Data %d.%d.%d", ((time_read[2] & 0x1F) - 12), time_read[1], time_read[4], time_read[5], time_read[6]);
	else //AM
		ESP_LOGI(RTC, "Time %02d:%02d AM   Data %d.%d.%d", (time_read[2] & 0x1F), time_read[1], time_read[4], time_read[5], time_read[6]);
}

/**
 * @brief Keyboard Handling Task
 *
 */
void RTCHandlingTask(void *arg)
{
	ds3231_I2C_init();
	read_time();

    while(1)
    {
    	read_time();
    	vTaskDelay(60000/ portTICK_PERIOD_MS); //60s
    }
}
