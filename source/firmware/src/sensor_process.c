/*
 * Open-BLDC - Open BrushLess DC Motor Controller
 * Copyright (C) 2010 by Piotr Esden-Tempski <piotr@esden.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/**
 * @file   sensor_process.c
 * @author Piotr Esden-Tempski <esden@esden.net>
 * @date   Tue Aug 17 00:29:15 2010
 *
 * @brief  Sensory input postprocessing user land process.
 *
 * Implementation of the sensor user land process which is being called each
 * time new sensor data arrive through the interrupt system.
 */

#include "types.h"

#include <lg/gpdef.h>
#include <lg/gprotc.h>

#include "gprot.h"
#include "driver/led.h"
#include "driver/adc.h"
#include "gprot.h"

#include "sensor_process.h"

/**
 * Parameters for sensor data post processing
 */
struct sensor_params {
	/**
	 * Phase voltage post processing parameters
	 */
	struct pv {
		s32 offset; /**< how much to offset the value */
		u32 iir;    /**< IIR filter value */
	} pv;
	/**
	 * Half supply rail voltage post processing parameters
	 */
	struct hbv {
		s32 offset; /**< how much to offset the value */
		u32 iir;    /**< IIR filter value */
	} hbv;
	/**
	 * Global controller current
	 */
	struct gc {
		s32 zero_current_offset; /**< Zero current offset value */
		s32 zero_current;	 /**< The value of zero */
		u32 iir;		 /**< IIR filter value */
	} gc;
};

/**
 * Sensor value struct instance
 */
struct sensors sensors;

/**
 * Sensor post processing parameters instance
 */
static struct sensor_params sensor_params;

/**
 * Generate debug output flag
 */
static int sensor_trigger_debug_output;

/**
 * Infinite Impulse Response filter calculation
 */
#define SENSOR_OFFSET_IIR(VALUE, NEW_VALUE, OFFSET, IIR) \
	(((VALUE * IIR) + (NEW_VALUE + OFFSET)) / (IIR + 1))

/**
 * Delay counter for low priority sensor updates
 */
static int sensor_process_low_prio_update_cnt = 0;

/**
 * Sensor process initializer.
 *
 * Resets all global presets.
 */
void sensor_process_init(void)
{

	(void)gpc_setup_reg(GPROT_ADC_ZERO_VALUE_REG_ADDR,
			    (u16 *) & (sensors.half_battery_voltage));
	(void)gpc_setup_reg(GPROT_ADC_GLOBAL_CURRENT_REG_ADDR,
			    (u16 *) & (sensors.global_current));
	(void)gpc_setup_reg(GPROT_ADC_PHASE_VOLTAGE_REG_ADDR,
			    (u16 *) & (sensors.phase_voltage));

	sensors.phase_voltage = 0;
	sensors.half_battery_voltage = 0;
	sensors.global_current = 0;

	sensor_params.pv.offset = 0;
	sensor_params.pv.iir = 0;
	sensor_params.hbv.offset = 000;
	sensor_params.hbv.iir = 20;
	sensor_params.gc.zero_current_offset = 0;
	sensor_params.gc.zero_current = 2045;
	sensor_params.gc.iir = 20;

	sensor_trigger_debug_output = 0;
	sensor_process_low_prio_update_cnt = 0;
}

/**
 * Resets all current sensor data to default values.
 */
void sensor_process_reset(void)
{
	sensors.phase_voltage = 0;
}

/**
 * The main periodic process implementation.
 *
 * This function is being called every time new sensor data arrives through the
 * interrupt system.
 */
void run_sensor_process(void)
{
	u16 phase_voltage = adc_data.phase_voltage;
	u16 half_battery_voltage = adc_data.half_battery_voltage;
	u16 global_current = adc_data.global_current;

	//ON(LED_RED);
	/* High priority sensors */
	if (sensors.phase_voltage == 0) {
		sensors.phase_voltage = 1;
	} else if (sensors.phase_voltage == 1) {
		sensors.phase_voltage = phase_voltage;
	} else if (sensors.phase_voltage == 65535) {
		sensors.phase_voltage = 65534;
	} else if (sensors.phase_voltage == 65534) {
		sensors.phase_voltage = phase_voltage;
	} else {
		sensors.phase_voltage = SENSOR_OFFSET_IIR(sensors.phase_voltage,
							  phase_voltage,
							  sensor_params.pv.
							  offset,
							  sensor_params.pv.iir);

	}

	/* Low priority sensors */
	if (sensor_process_low_prio_update_cnt == 211) {
		sensor_process_low_prio_update_cnt = 0;

		/* Jump to the current battery voltage when initializing */
		if (sensors.half_battery_voltage == 0) {
			sensors.half_battery_voltage = half_battery_voltage;
		}
		/* Adjust slowly the half battery voltage according to measurement */
		if ((u16)((s32)half_battery_voltage + (s32)sensor_params.hbv.offset) >
		    sensors.half_battery_voltage) {
			sensors.half_battery_voltage++;
		} else if ((u16)((s32)half_battery_voltage + (s32)sensor_params.hbv.offset) <
			   sensors.half_battery_voltage) {
			sensors.half_battery_voltage--;
		}

		/* Calculate global current */
		sensors.global_current =
		    SENSOR_OFFSET_IIR(sensors.global_current, global_current,
				      (sensor_params.gc.zero_current +
				       sensor_params.gc.zero_current_offset),
				      sensor_params.gc.iir);
	} else {
		sensor_process_low_prio_update_cnt++;
	}

	if (sensor_trigger_debug_output == 100) {
		sensor_trigger_debug_output = 0;
		(void)gpc_register_touched(GPROT_ADC_ZERO_VALUE_REG_ADDR);
		(void)gpc_register_touched(GPROT_ADC_GLOBAL_CURRENT_REG_ADDR);
		(void)gpc_register_touched(GPROT_ADC_PHASE_VOLTAGE_REG_ADDR);
	} else {
		sensor_trigger_debug_output++;
	}
	//OFF(LED_RED);
}
