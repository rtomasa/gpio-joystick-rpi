/*
 *  GPIO Joystick Driver for Raspberry Pi 5
 *
 *  Copyright (c) 2025: Ruben Tomas Alonso
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
#include <linux/gpio/consumer.h> /* For gpiod_* APIs */
#include <linux/of.h>			 /* For Device Tree structures (optional) */
#include <linux/hrtimer.h>		 /* For high-resolution timers */
#include <linux/ktime.h>
#include <linux/platform_device.h>
#include <linux/workqueue.h>

static const struct of_device_id gpio_joy_of_match[] = {
	{.compatible = "rta,gpio-joystick"},
	{/* sentinel */}};

/*
 * Module info
 */
MODULE_AUTHOR("Ruben Tomas Alonso");
MODULE_DESCRIPTION("GPIO Joystick Driver for RPi5 (digital D-Pad)");
MODULE_LICENSE("GPL");
MODULE_DEVICE_TABLE(of, gpio_joy_of_match);

/* ------------------------------------------------------------------
 * Defines & Data
 * ------------------------------------------------------------------ */

enum joy_type
{
	TYPE_NONE = 0,
	TYPE_JOYSTICK_GPIO,
	TYPE_JOYSTICK_GPIO_BPLUS,
	TYPE_MAX
};

/* We poll at 1 ms via hrtimer */
static unsigned int poll_ms = 1;
module_param(poll_ms, uint, 0644);
#define POLL_INTERVAL_NS ((u64)poll_ms * 1000000ULL)

/*
 * We have 14 signals total:
 *   4 directions (Up, Down, Left, Right) + 10 buttons
 */
#define TOTAL_INPUTS 14

/*
 * We treat each direction and button as a digital input.
 * We map them to Linux KEY codes (4 for directions, 8 for buttons).
 */
static const unsigned short joy_gpio_btn[] = {
	BTN_DPAD_UP,	// Up
	BTN_DPAD_DOWN,	// Down
	BTN_DPAD_LEFT,	// Left
	BTN_DPAD_RIGHT, // Right
	BTN_START,		// Start
	BTN_SELECT,		// Select
	BTN_A,			// A (1)
	BTN_B,			// B (2)
	BTN_TR,			// TR (6)
	BTN_Y,			// Y (3)
	BTN_X,			// X (4)
	BTN_TL,			// TL (5)
	BTN_MODE,		// Home / Service
	BTN_THUMBR		// Test
};

/* ------------------------------------------------------------------
 * Per-pad data
 * ------------------------------------------------------------------ */
struct joy_pad
{
	struct input_dev *dev;
	enum joy_type type;
	char phys[32];

	/* gpiod descriptors for each line */
	struct gpio_desc *gpiods[TOTAL_INPUTS];
};

/* ------------------------------------------------------------------
 * Main struct for the driver
 * ------------------------------------------------------------------ */
struct joy
{
	struct joy_pad pad;
	struct hrtimer hrtimer;
	int used;
	struct mutex mutex;
	struct device *dev;
	struct work_struct poll_work;
	struct workqueue_struct *wq;
};

/* ------------------------------------------------------------------
 * Reading GPIO lines with gpiod
 * ------------------------------------------------------------------ */
static void joy_gpio_read_packet(struct joy_pad *pad, unsigned char *data)
{
	for (int i = 0; i < TOTAL_INPUTS; i++)
	{
		struct gpio_desc *d = pad->gpiods[i];
		int v = d ? gpiod_get_value_cansleep(d) : 1; /* pull-up => not pressed */
		data[i] = (v == 0) ? 1 : 0;				     /* active low (GPIO_ACTIVE_HIGH) */
	}
}

/* ------------------------------------------------------------------
 * Report to Input Subsystem
 * ------------------------------------------------------------------ */
static void joy_input_report(struct joy_pad *pad, unsigned char *data)
{
	struct input_dev *dev = pad->dev;

	/*
	 * Directions + Buttons are all reported as KEY events.
	 * We'll loop over all pin signals, each one mapped to a "button/dpad" code.
	 */
	for (int i = 0; i < TOTAL_INPUTS; i++)
	{
		input_report_key(dev, joy_gpio_btn[i], data[i]);
	}

	input_sync(dev);
}

/* ------------------------------------------------------------------
 * Polling Logic
 * ------------------------------------------------------------------ */
static void joy_process_packet(struct joy *j)
{
	unsigned char data[TOTAL_INPUTS];
	struct joy_pad *pad = &j->pad;

	if (pad->type == TYPE_JOYSTICK_GPIO || pad->type == TYPE_JOYSTICK_GPIO_BPLUS)
	{
		joy_gpio_read_packet(pad, data);
		joy_input_report(pad, data);
	}
}

/* ------------------------------------------------------------------
 * hrtimer Callback
 * ------------------------------------------------------------------ */
static void joy_poll_work(struct work_struct *work)
{
	struct joy *j = container_of(work, struct joy, poll_work);
	joy_process_packet(j);
}

static enum hrtimer_restart joy_hrtimer_callback(struct hrtimer *t)
{
	struct joy *j = container_of(t, struct joy, hrtimer);
	if (likely(j->wq))
		queue_work(j->wq, &j->poll_work);
	hrtimer_forward_now(t, ns_to_ktime(POLL_INTERVAL_NS));
	return HRTIMER_RESTART;
}

/* ------------------------------------------------------------------
 * Input Device Open/Close
 * ------------------------------------------------------------------ */
static int joy_open(struct input_dev *dev)
{
	struct joy *joy = input_get_drvdata(dev);
	int err;
	unsigned int ms;

	err = mutex_lock_interruptible(&joy->mutex);
	if (err)
		return err;

	if (!joy->used++)
	{
		ms = poll_ms ? poll_ms : 1;
		hrtimer_start(&joy->hrtimer,
					  ns_to_ktime((u64)ms * 1000000ULL),
					  HRTIMER_MODE_REL);
	}

	mutex_unlock(&joy->mutex);
	return 0;
}

static void joy_close(struct input_dev *dev)
{
	struct joy *j = input_get_drvdata(dev);

	mutex_lock(&j->mutex);
	if (!--j->used)
	{
		hrtimer_cancel(&j->hrtimer);
		cancel_work_sync(&j->poll_work);
	}
	mutex_unlock(&j->mutex);
}

/* ------------------------------------------------------------------
 * Pad Setup using gpiod
 * ------------------------------------------------------------------ */

static void joy_input_unregister(void *data)
{
	struct input_dev *idev = data;
	input_unregister_device(idev);
}

static int joy_setup_pad_gpio(struct joy *joy, int pad_type, u32 regid)
{
	static const char *const names[TOTAL_INPUTS] = {
		"up", "down", "left", "right", "start", "select",
		"a", "b", "tr", "y", "x", "tl", "home", "test"};
	int i, err;
	struct joy_pad *pad = &joy->pad;

	if (pad_type < 1 || pad_type >= TYPE_MAX)
	{
		dev_err(joy->dev, "[gpio-joy] Pad type %d unknown\n", pad_type);
		return -EINVAL;
	}

	pad->dev = devm_input_allocate_device(joy->dev);
	if (!pad->dev)
	{
		dev_err(joy->dev, "[gpio-joy] Not enough memory for input device\n");
		return -ENOMEM;
	}

	pad->type = pad_type;
	snprintf(pad->phys, sizeof(pad->phys), "gpio-joystick.%u", regid);

	pad->dev->name = regid ? "GPIO Joystick P2" : "GPIO Joystick P1";
	pad->dev->phys = pad->phys;
	pad->dev->id.bustype = BUS_HOST;
	pad->dev->id.vendor = 0x0107;
	pad->dev->id.product = pad_type;
	pad->dev->id.version = 0x0100;
	pad->dev->dev.parent = joy->dev;

	input_set_drvdata(pad->dev, joy);
	__set_bit(EV_KEY, pad->dev->evbit);

	for (i = 0; i < TOTAL_INPUTS; i++)
	{
		struct gpio_desc *d = devm_gpiod_get_optional(joy->dev, names[i], GPIOD_IN);
		if (IS_ERR(d))
			return PTR_ERR(d);
		pad->gpiods[i] = d; /* can be NULL if it is not in DT */
		if (d)
			__set_bit(joy_gpio_btn[i], pad->dev->keybit);
	}

	bool any = false;
	for (i = 0; i < TOTAL_INPUTS; i++)
	{
		if (pad->gpiods[i])
		{
			any = true;
			break;
		}
	}

	if (!any)
	{
		dev_err(joy->dev, "[gpio-joy] No GPIOs defined in DT; refusing to register\n");
		return -ENODEV;
	}

	pad->dev->open = joy_open;
	pad->dev->close = joy_close;

	dev_info(joy->dev, "[gpio-joy] Joystick %u configured: type=%d, vendor=0x%04x, product=0x%04x\n",
			 regid, pad_type, pad->dev->id.vendor, pad->dev->id.product);

	err = input_register_device(pad->dev);
	if (err)
	{
		dev_err(joy->dev, "[gpio-joy] Failed to register input device\n");
		return err;
	}

	/* Auto-unregister */
	err = devm_add_action_or_reset(joy->dev, joy_input_unregister, pad->dev);
	if (err)
		return err;

	return 0;
}

/* ------------------------------------------------------------------
 * Module Init / Exit
 * ------------------------------------------------------------------ */
static int gpio_joy_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct joy *j;
	u32 id = 0; /* 0=P1, 1=P2 */
	int err;

	j = devm_kzalloc(dev, sizeof(*j), GFP_KERNEL);
	if (!j)
		return -ENOMEM;

	j->dev = dev;
	mutex_init(&j->mutex);

	INIT_WORK(&j->poll_work, joy_poll_work);
	j->wq = alloc_workqueue("gpio-joy", WQ_HIGHPRI | WQ_UNBOUND, 1);
	if (!j->wq)
		return -ENOMEM;
	hrtimer_init(&j->hrtimer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	j->hrtimer.function = joy_hrtimer_callback;

	of_property_read_u32(dev->of_node, "reg", &id);

	err = joy_setup_pad_gpio(j, id == 0 ? TYPE_JOYSTICK_GPIO : TYPE_JOYSTICK_GPIO_BPLUS, id);
	if (err)
	{
		if (j->wq)
		{
			drain_workqueue(j->wq);
			destroy_workqueue(j->wq);
			j->wq = NULL;
		}
		return err;
	}

	platform_set_drvdata(pdev, j);
	return 0;
}

static void gpio_joy_remove(struct platform_device *pdev)
{
	struct joy *j = platform_get_drvdata(pdev);

	hrtimer_cancel(&j->hrtimer);
	if (j->wq)
	{
		drain_workqueue(j->wq);
		destroy_workqueue(j->wq);
		j->wq = NULL;
	}
}

static int gpio_joy_suspend(struct device *dev)
{
	struct joy *j = dev_get_drvdata(dev);
	hrtimer_cancel(&j->hrtimer);
	if (j->wq)
		drain_workqueue(j->wq);
	return 0;
}

static int gpio_joy_resume(struct device *dev)
{
	struct joy *j = dev_get_drvdata(dev);
	if (j->used)
		hrtimer_start(&j->hrtimer, ns_to_ktime(POLL_INTERVAL_NS), HRTIMER_MODE_REL);
	return 0;
}

static const struct dev_pm_ops gpio_joy_pm_ops = {
	.suspend = gpio_joy_suspend,
	.resume = gpio_joy_resume,
};

static struct platform_driver gpio_joy_driver = {
	.driver = {
		.name = "gpio-joystick",
		.of_match_table = gpio_joy_of_match,
		.pm = &gpio_joy_pm_ops,
	},
	.probe = gpio_joy_probe,
	.remove = gpio_joy_remove,
};

module_platform_driver(gpio_joy_driver);
