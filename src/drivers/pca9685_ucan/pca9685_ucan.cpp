/****************************************************************************
 *
 *   Copyright (c) 2012-2022 PX4 Development Team. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name PX4 nor the names of its contributors may be
 *    used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
 * OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 ****************************************************************************/

/**
 * @file pca9685_ucan.cpp
 *
 * Driver for the PCA9685 I2C PWM module
 * The chip is used on the Adafruit I2C/PWM converter https://www.adafruit.com/product/815
 *
 * Parts of the code are adapted from the arduino library for the board
 * https://github.com/adafruit/Adafruit-PWM-Servo-Driver-Library
 * for the license of these parts see the
 * arduino_Adafruit_PWM_Servo_Driver_Library_license.txt file
 * see https://github.com/adafruit/Adafruit-PWM-Servo-Driver-Library for contributors
 *
 * @author Benjamin Perseghetti <bperseghetti@rudislabs.com>
 * @author Landon Haugh <landon.haugh@nxp.com>
 */

#include <px4_platform_common/px4_config.h>
#include <px4_platform_common/defines.h>
#include <px4_platform_common/module.h>

#include <drivers/device/i2c.h>

#include <sys/types.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <ctype.h>
#include <math.h>
#include <px4_platform_common/getopt.h>
#include <px4_platform_common/i2c_spi_buses.h>
#include <nuttx/clock.h>

#include <perf/perf_counter.h>
#include <systemlib/err.h>

#include <uORB/uORB.h>
#include <uORB/Subscription.hpp>
#include <uORB/topics/pca_pwm.h>

#include <board_config.h>

#define PCA9685_MODE1 0x0
#define PCA9685_PRESCALE 0xFE

#define LED0_ON_L 0x6

#define ADDR 0x40	// I2C adress

#define ORB_SUB_UPDATE_PERIOD 10 // uORB subscription update period in milliseconds

#define NUMBER_PWM_CHANNELS 16 // Number of PWM channels.

#define PWM_PERIOD_MIN_US 656

#define PWM_PERIOD_MAX_US 41666


enum IOX_MODE {
	IOX_MODE_ON,
	IOX_MODE_TEST_OUT
};

using namespace time_literals;

class PCA9685 : public device::I2C, public I2CSPIDriver<PCA9685>
{
public:
	PCA9685(const I2CSPIDriverConfig &config);
	~PCA9685() override = default;

	static void print_usage();

	void print_status();

	int		init() override;
	int		reset();

	void RunImpl();

protected:
	void custom_method(const BusCLIArguments &cli) override;
private:

	enum IOX_MODE		_mode;
	uint64_t		_i2cpwm_interval;
	perf_counter_t		_comms_errors;

	uint8_t			_msg[6];


	int			_pca_pwm_sub;
	struct pca_pwm_s	_pca_pwm;
	uint16_t		_pwm_period_us = 20000;
	float			_pwm_freq = 50.0f;
	uint16_t		_test_pwm = 0;
	uint16_t		_current_values[NUMBER_PWM_CHANNELS]; /**< stores the current pwm output values as sent to the setPin() */

	bool _mode_on_initialized;  /** Set to true after the first call of i2cpwm in mode IOX_MODE_ON */
	bool _updated = false;

	/**
	 * Helper function to set the pwm frequency
	 */
	int setPWMFreq(float freq);

	/**
	 * Helper function to set the demanded pwm value
	 * @param num pwm output number
	 */
	int setPWM(uint8_t num, uint16_t on, uint16_t off);

	/**
	 * Sets pin without having to deal with on/off tick placement and properly handles
	 * a zero value as completely off.
	 * @param num pwm output number
	 * @param val should be a value from 0 to 4095 inclusive.
	 */
	int setPin(uint8_t num, uint16_t val);


	/* Wrapper to read a byte from addr */
	int read8(uint8_t addr, uint8_t &value);

	/* Wrapper to wite a byte to addr */
	int write8(uint8_t addr, uint8_t value);

};

PCA9685::PCA9685(const I2CSPIDriverConfig &config) :
	I2C(config),
	I2CSPIDriver(config),
	_mode(IOX_MODE_ON),
	_i2cpwm_interval((float)_pwm_period_us),
	_comms_errors(perf_alloc(PC_COUNT, MODULE_NAME": com_err")),
	_pca_pwm_sub(-1),
	_pca_pwm(),
	_mode_on_initialized(false)
{
	memset(_msg, 0, sizeof(_msg));
	memset(_current_values, 0, sizeof(_current_values));
}

int
PCA9685::init()
{
	int ret;
	ret = I2C::init();

	if (ret != OK) {
		return ret;
	}

	ret = reset();

	if (ret != OK) {
		return ret;
	}

	ret = setPWMFreq(_pwm_freq);

	if (ret == 0) {
		ScheduleNow();
	}

	return ret;
}

void
PCA9685::print_status()
{
	I2CSPIDriverBase::print_status();
	PX4_INFO("Mode: %u", _mode);
}

void
PCA9685::RunImpl()
{
	if (_mode == IOX_MODE_TEST_OUT) {
		if (_test_pwm > 4096) {
			_test_pwm = 0;
		}

		for (int i = 0; i < NUMBER_PWM_CHANNELS; i++) {
				setPin(i, _test_pwm);
		}

		_test_pwm += 4096/10;

	} else {
		if (!_mode_on_initialized) {
			_pca_pwm_sub = orb_subscribe(ORB_ID(pca_pwm));
			orb_set_interval(_pca_pwm_sub, ORB_SUB_UPDATE_PERIOD);

			_mode_on_initialized = true;
		}

		orb_check(_pca_pwm_sub, &_updated);

		if (_updated) {
			orb_copy(ORB_ID(pca_pwm), _pca_pwm_sub, &_pca_pwm);
			if (_pwm_period_us != _pca_pwm.pwm_period &&
			    _pca_pwm.pwm_period >= PWM_PERIOD_MIN_US &&
			    _pca_pwm.pwm_period <= PWM_PERIOD_MAX_US) {
				_pwm_period_us = _pca_pwm.pwm_period;
				_pwm_freq = 1000000.0f / (float)_pwm_period_us;
				DEVICE_DEBUG("freq: %.2f, period: %u", (double)_pwm_freq, _pwm_period_us);
			}
			for (int i = 0; i < NUMBER_PWM_CHANNELS; i++) {
				uint16_t new_value = (uint16_t)(((float)_pwm_period_us/(float)_pca_pwm.pulse_width[i])*4096.0f);
				DEVICE_DEBUG("%d: current: %u, new: %u, pulse width: %u", i, _current_values[i], new_value, _pca_pwm.pulse_width[i]);

				if (new_value != _current_values[i] && new_value < 4096) {
					setPin(i, new_value);
					_current_values[i] = new_value;
				} else {
					if (new_value < 4096) {
						DEVICE_DEBUG("pwm new value: %u is out of range [0, 4096]", new_value);
					}
				}
			}
		}
	}

	ScheduleDelayed(_i2cpwm_interval);
}

int
PCA9685::setPWM(uint8_t num, uint16_t on, uint16_t off)
{
	int ret;
	/* convert to correct message */
	_msg[0] = LED0_ON_L + 4 * num;
	_msg[1] = on;
	_msg[2] = on >> 8;
	_msg[3] = off;
	_msg[4] = off >> 8;

	/* try i2c transfer */
	ret = transfer(_msg, 5, nullptr, 0);

	if (OK != ret) {
		perf_count(_comms_errors);
		DEVICE_LOG("i2c::transfer returned %d", ret);
	}

	return ret;
}

int
PCA9685::setPin(uint8_t num, uint16_t val)
{
	// Clamp value between 0 and 4095 inclusive.
	if (val > 4095) {
		val = 4095;
	}

	if (val == 4095) {
		// Special value for signal fully on.
		return setPWM(num, 4096, 0);

	} else if (val == 0) {
		// Special value for signal fully off.
		return setPWM(num, 0, 4096);

	} else {
		return setPWM(num, 0, val);
	}

	return PX4_ERROR;
}

int
PCA9685::setPWMFreq(float freq)
{
	int ret  = OK;
	float prescaleval = 25000000;
	prescaleval /= 4096;
	prescaleval /= freq;
	prescaleval -= 1;
	uint8_t prescale = uint8_t(prescaleval + 0.5f); //implicit floor()
	uint8_t oldmode;
	ret = read8(PCA9685_MODE1, oldmode);

	if (ret != OK) {
		return ret;
	}

	uint8_t newmode = (oldmode & 0x7F) | 0x10; // sleep

	ret = write8(PCA9685_MODE1, newmode); // go to sleep

	if (ret != OK) {
		return ret;
	}

	ret = write8(PCA9685_PRESCALE, prescale); // set the prescaler

	if (ret != OK) {
		return ret;
	}

	ret = write8(PCA9685_MODE1, oldmode);

	if (ret != OK) {
		return ret;
	}

	usleep(5000); //5ms delay (from arduino driver)

	ret = write8(PCA9685_MODE1, oldmode | 0xa1);  //  This sets the MODE1 register to turn on auto increment.

	if (ret != OK) {
		return ret;
	}

	return ret;
}

/* Wrapper to read a byte from addr */
int
PCA9685::read8(uint8_t addr, uint8_t &value)
{
	int ret = OK;

	/* send addr */
	ret = transfer(&addr, sizeof(addr), nullptr, 0);

	if (ret != OK) {
		goto fail_read;
	}

	/* get value */
	ret = transfer(nullptr, 0, &value, 1);

	if (ret != OK) {
		goto fail_read;
	}

	return ret;

fail_read:
	perf_count(_comms_errors);
	DEVICE_LOG("i2c::transfer returned %d", ret);

	return ret;
}

int PCA9685::reset(void)
{
	warnx("resetting");
	return write8(PCA9685_MODE1, 0x0);
}

/* Wrapper to wite a byte to addr */
int
PCA9685::write8(uint8_t addr, uint8_t value)
{
	int ret = OK;
	_msg[0] = addr;
	_msg[1] = value;
	/* send addr and value */
	ret = transfer(_msg, 2, nullptr, 0);

	if (ret != OK) {
		perf_count(_comms_errors);
		DEVICE_LOG("i2c::transfer returned %d", ret);
	}

	return ret;
}

void
PCA9685::print_usage()
{
	PRINT_MODULE_USAGE_NAME("pca9685_ucan", "driver");
	PRINT_MODULE_USAGE_COMMAND("start");
	PRINT_MODULE_USAGE_PARAMS_I2C_SPI_DRIVER(true, false);
	PRINT_MODULE_USAGE_PARAMS_I2C_ADDRESS(0x40);
	PRINT_MODULE_USAGE_COMMAND("reset");
	PRINT_MODULE_USAGE_COMMAND_DESCR("test", "enter test mode");
	PRINT_MODULE_USAGE_DEFAULT_COMMANDS();
}

void PCA9685::custom_method(const BusCLIArguments &cli)
{
	switch (cli.custom1) {
	case 0: reset(); break;

	case 1: _mode = IOX_MODE_TEST_OUT; break;
	}
}

extern "C" int pca9685_ucan_main(int argc, char *argv[])
{
	using ThisDriver = PCA9685;
	BusCLIArguments cli{true, false};
	cli.default_i2c_frequency = 100000;
	cli.i2c_address = ADDR;

	const char *verb = cli.parseDefaultArguments(argc, argv);

	if (!verb) {
		ThisDriver::print_usage();
		return -1;
	}

	BusInstanceIterator iterator(MODULE_NAME, cli, DRV_PWM_DEVTYPE_PCA9685);

	if (!strcmp(verb, "start")) {
		return ThisDriver::module_start(cli, iterator);
	}

	if (!strcmp(verb, "stop")) {
		return ThisDriver::module_stop(iterator);
	}

	if (!strcmp(verb, "status")) {
		return ThisDriver::module_status(iterator);
	}

	if (!strcmp(verb, "reset")) {
		cli.custom1 = 0;
		return ThisDriver::module_custom_method(cli, iterator);
	}

	if (!strcmp(verb, "test")) {
		cli.custom1 = 1;
		return ThisDriver::module_custom_method(cli, iterator);
	}

	ThisDriver::print_usage();
	return -1;
}