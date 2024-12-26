/*
 *  GPIO Joystick Driver for Raspberry Pi 5
 *
 *  Copyright (c) 2024: Ruben Tomas Alonso
 * 
 *  Based on the mk_arcade_joystick_rpi driver by:
 *  	- Matthieu Proucelle
 * 		- Mark Spaeth
 * 		- Daniel Moreno
 *  Based on the gamecon driver by:
 * 		- Vojtech Pavlik
 * 		- Markus Hiienkari
 * 
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 *  MA 02110-1301, USA.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/input.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/gpio/consumer.h>   /* For gpiod_* APIs */
#include <linux/gpio.h>            /* For gpio_to_desc() */
#include <linux/of.h>              /* For Device Tree structures (optional) */
#include <linux/of_gpio.h>         /* For of_get_named_gpio() if needed */
#include <linux/hrtimer.h>         /* For high-resolution timers */
#include <linux/ktime.h>

/*
 * Module info
 */
MODULE_AUTHOR("Ruben Tomas Alonso");
MODULE_DESCRIPTION("GPIO Joystick Driver for RPi5 (digital D-Pad)");
MODULE_LICENSE("GPL");

#define MAX_DEVICES   2

/* ------------------------------------------------------------------
 * Configuration via module parameters
 * ------------------------------------------------------------------ */
struct joy_config {
	int args[MAX_DEVICES];
	unsigned int nargs;
};

static struct joy_config joy_cfg __initdata;

/*
 * module_param_array_named for 'map' to pick the joystick type:
 *   TYPE_JOYSTICK_GPIO, TYPE_JOYSTICK_GPIO_BPLUS
 */
module_param_array_named(map, joy_cfg.args, int, &(joy_cfg.nargs), 0);
MODULE_PARM_DESC(map, "Enable GPIO Joystick(s)");

/* ------------------------------------------------------------------
 * Defines & Data
 * ------------------------------------------------------------------ */

enum joy_type {
	TYPE_NONE = 0,
	TYPE_JOYSTICK_GPIO,
	TYPE_JOYSTICK_GPIO_BPLUS,
	TYPE_MAX
};

/* We poll at 1 ms via hrtimer */
#define POLL_INTERVAL_NS   1000000  /* 1 ms in nanoseconds */

/*
 * We have 12 signals total:
 *   4 directions (Up, Down, Left, Right) + 8 buttons
 */
#define TOTAL_INPUTS 12

/*
 * Default pin mappings (12 signals each).
 *  - [0] = Up
 *  - [1] = Down
 *  - [2] = Left
 *  - [3] = Right
 *  - [4] = Start
 *  - [5] = Select
 *  - [6] = A
 *  - [7] = B
 *  - [8] = TR
 *  - [9] = Y
 *  - [10] = X
 *  - [11] = TL
 */
static const int joy_gpio_maps[] = {
    573, 586, 596, 591, 579, 578, 594, 593, 592, 587, 584, 583
};

/* 2nd joystick on the B+ GPIOS (12 signals) */
static const int joy_gpio_maps_bplus[] = {
	580, 574, 575, 582, 588, 595, 590, 589, 585, 581, 576, 577
};

/*
 * We treat each direction and button as a digital input.
 * We map them to Linux KEY codes (4 for directions, 8 for buttons).
 */
static const short joy_gpio_btn[] = {
	BTN_DPAD_UP,    // Up
	BTN_DPAD_DOWN,  // Down
	BTN_DPAD_LEFT,  // Left
	BTN_DPAD_RIGHT, // Right
	BTN_START,      // Start
	BTN_SELECT,     // Select
	BTN_A,          // A
	BTN_B,          // B
	BTN_TR,         // TR
	BTN_Y,          // Y
	BTN_X,          // X
	BTN_TL          // TL
};

/*
 * Joystick display names
 */
static const char *joy_names[] = {
	NULL,
	"GPIO Joystick 1",
	"GPIO Joystick 2"
};

/* ------------------------------------------------------------------
 * Per-pad data
 * ------------------------------------------------------------------ */
struct joy_pad {
	struct input_dev *dev;
	enum joy_type type;
	char phys[32];

	/* gpiod descriptors for each line */
	struct gpio_desc *gpiods[TOTAL_INPUTS];

	/* Store the user-provided pin numbers for debugging */
	int gpio_maps[TOTAL_INPUTS];
};

/* ------------------------------------------------------------------
 * Main struct for the driver
 * ------------------------------------------------------------------ */
struct joy {
	struct joy_pad pads[MAX_DEVICES];

	/* hrtimer for 1 ms polling */
	struct hrtimer hrtimer;

	int pad_count[TYPE_MAX];
	int used;
	struct mutex mutex;
	int count;
};

static struct joy *joy_base;

/* ------------------------------------------------------------------
 * Reading GPIO lines with gpiod
 * ------------------------------------------------------------------ */
static void joy_gpio_read_packet(struct joy_pad *pad, unsigned char *data)
{
	int i;
	int val;

	for (i = 0; i < TOTAL_INPUTS; i++) {
		if (pad->gpiods[i]) {
			/*
			 * If your hardware is active-low, keep the inversion:
			 *   data[i] = (gpiod_get_value() == 0) ? 1 : 0;
			 * Otherwise, remove the '== 0' check or invert logic accordingly.
			 */
			val = gpiod_get_value(pad->gpiods[i]);
			data[i] = (val == 0) ? 1 : 0;
		} else {
			data[i] = 0;
		}
	}
}

/* ------------------------------------------------------------------
 * Report to Input Subsystem
 * ------------------------------------------------------------------ */
static void joy_input_report(struct joy_pad *pad, unsigned char *data)
{
	struct input_dev *dev = pad->dev;
	int i;

	/*
	 * Directions + Buttons are all reported as KEY events.
	 * We'll loop over all 12 signals, each one mapped to a "button/dpad" code.
	 */
	for (i = 0; i < TOTAL_INPUTS; i++) {
		input_report_key(dev, joy_gpio_btn[i], data[i]);
	}

	input_sync(dev);
}

/* ------------------------------------------------------------------
 * Polling Logic
 * ------------------------------------------------------------------ */
static void joy_process_packet(struct joy *joy)
{
	unsigned char data[TOTAL_INPUTS];
	struct joy_pad *pad;
	int i;

	for (i = 0; i < MAX_DEVICES; i++) {
		pad = &joy->pads[i];
		if (pad->type == TYPE_JOYSTICK_GPIO ||
		    pad->type == TYPE_JOYSTICK_GPIO_BPLUS) {

			joy_gpio_read_packet(pad, data);
			joy_input_report(pad, data);
		}
	}
}

/* ------------------------------------------------------------------
 * hrtimer Callback
 * ------------------------------------------------------------------ */
static enum hrtimer_restart joy_hrtimer_callback(struct hrtimer *timer)
{
	struct joy *m = container_of(timer, struct joy, hrtimer);

	joy_process_packet(m);

	/* Schedule next callback in 1 ms */
	hrtimer_forward_now(timer, ns_to_ktime(POLL_INTERVAL_NS));
	return HRTIMER_RESTART;
}

/* ------------------------------------------------------------------
 * Input Device Open/Close
 * ------------------------------------------------------------------ */
static int joy_open(struct input_dev *dev)
{
	struct joy *joy = input_get_drvdata(dev);
	int err;

	err = mutex_lock_interruptible(&joy->mutex);
	if (err)
		return err;

	if (!joy->used++) {
		hrtimer_start(&joy->hrtimer,
			      ns_to_ktime(POLL_INTERVAL_NS),
			      HRTIMER_MODE_REL);
	}

	mutex_unlock(&joy->mutex);
	return 0;
}

static void joy_close(struct input_dev *dev)
{
	struct joy *joy = input_get_drvdata(dev);

	mutex_lock(&joy->mutex);
	if (!--joy->used) {
		hrtimer_cancel(&joy->hrtimer);
	}
	mutex_unlock(&joy->mutex);
}

/* ------------------------------------------------------------------
 * Pad Setup using gpiod
 * ------------------------------------------------------------------ */
static int __init joy_setup_pad_gpio(struct joy *joy, int idx, int pad_type)
{
	int i, err;
	struct joy_pad *pad = &joy->pads[idx];

	if (idx >= MAX_DEVICES) {
		pr_err("Device count exceeds max\n");
		return -EINVAL;
	}

	if (pad_type < 1 || pad_type >= TYPE_MAX) {
		pr_err("Pad type %d unknown\n", pad_type);
		return -EINVAL;
	}

	pad->dev = input_allocate_device();
	if (!pad->dev) {
		pr_err("Not enough memory for input device\n");
		return -ENOMEM;
	}

	pad->type = pad_type;
	snprintf(pad->phys, sizeof(pad->phys), "input%d", idx);

	pad->dev->name = joy_names[pad_type];
	pad->dev->phys = pad->phys;
	pad->dev->id.bustype = BUS_PARPORT;	// For a GPIO-based device
	pad->dev->id.vendor  = 0x0107;		// Example vendor ID
	pad->dev->id.product = pad_type;	// Using pad_type as product ID
	pad->dev->id.version = 0x0100;		// Version 1.0.0

	input_set_drvdata(pad->dev, joy);

	/* Only EV_KEY needed for 12 digital inputs */
	__set_bit(EV_KEY, pad->dev->evbit);

	pad->dev->open  = joy_open;
	pad->dev->close = joy_close;

	/*
	 * Register the 12 KEY codes. Directions are d-pad keys, next 8 are standard buttons.
	 */
	for (i = 0; i < TOTAL_INPUTS; i++) {
		__set_bit(joy_gpio_btn[i], pad->dev->keybit);
	}

	joy->pad_count[pad_type]++;

	/*
	 * Assign default mappings
	 * We assume we want all 12 signals for each type. 
	 * If some pins are not used, set them to -1 or physically remove them.
	 */
	switch (pad_type) {
	case TYPE_JOYSTICK_GPIO:
		memcpy(pad->gpio_maps, joy_gpio_maps,
		       TOTAL_INPUTS * sizeof(int));
		break;
	case TYPE_JOYSTICK_GPIO_BPLUS:
		memcpy(pad->gpio_maps, joy_gpio_maps_bplus,
		       TOTAL_INPUTS * sizeof(int));
		break;
	default:
		break;
	}

	/*
	 * For each pin, obtain a gpiod descriptor and set input direction
	 * If any pin is -1, skip it.
	 */
	for (i = 0; i < TOTAL_INPUTS; i++) {
		if (pad->gpio_maps[i] != -1) {
			struct gpio_desc *desc;

			desc = gpio_to_desc(pad->gpio_maps[i]);
			if (!desc) {
				pr_err("gpio_to_desc failed for GPIO %d\n",
				       pad->gpio_maps[i]);
				err = -EINVAL;
				goto fail;
			}
			err = gpiod_direction_input(desc);
			if (err) {
				pr_err("Cannot set GPIO %d as input\n",
				       pad->gpio_maps[i]);
				goto fail;
			}
			pad->gpiods[i] = desc;
		} else {
			pad->gpiods[i] = NULL;
		}
	}

	pr_info("Joystick %d configured: type=%d, vendor=0x%04x, product=0x%04x\n",
		idx, pad_type, pad->dev->id.vendor, pad->dev->id.product);

	err = input_register_device(pad->dev);
	if (err) {
		pr_err("Failed to register input device for pad %d\n", idx);
		input_free_device(pad->dev);
		pad->dev = NULL;
		return err;
	}

	return 0;

fail:
	/* Cleanup on failure */
	for (; i >= 0; i--) {
		if (pad->gpiods[i]) {
			pad->gpiods[i] = NULL;
		}
	}
	input_free_device(pad->dev);
	pad->dev = NULL;
	return err;
}

/* ------------------------------------------------------------------
 * Probe (Set Up)
 * ------------------------------------------------------------------ */
static struct joy __init *joy_probe(struct joy *joy, int *pads, int n_pads)
{
	int i, err;

	for (i = 0; i < n_pads; i++) {
		err = joy_setup_pad_gpio(joy, joy->count, pads[i]);
		if (!err)
			joy->count++;
	}
	return joy;
}

/* ------------------------------------------------------------------
 * Module Init / Exit
 * ------------------------------------------------------------------ */
static int __init joy_init(void)
{
	pr_info("Initializing GPIO Joystick Driver\n");

	joy_base = kzalloc(sizeof(*joy_base), GFP_KERNEL);
	if (!joy_base)
		return -ENOMEM;

	mutex_init(&joy_base->mutex);
	joy_base->count = 0;

	/* Initialize our hrtimer for 1 ms polling */
	hrtimer_init(&joy_base->hrtimer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	joy_base->hrtimer.function = joy_hrtimer_callback;

	joy_probe(joy_base, joy_cfg.args, joy_cfg.nargs);

	if (joy_base->count < 1) {
		pr_err("At least one valid GPIO device must be specified\n");
		kfree(joy_base);
		return -EINVAL;
	}

	return 0;
}

static void __exit joy_exit(void)
{
	int i;

	if (joy_base) {
		/* Cancel the hrtimer if it's still running */
		hrtimer_cancel(&joy_base->hrtimer);

		/* Unregister input devices */
		for (i = 0; i < joy_base->count; i++) {
			if (joy_base->pads[i].dev)
				input_unregister_device(joy_base->pads[i].dev);
		}
		kfree(joy_base);
	}
	pr_info("GPIO Joystick Driver removed\n");
}

module_init(joy_init);
module_exit(joy_exit);
