# GPIO Joystick Driver for Raspberry Pi 5

This repository contains the GPIO Joystick Driver for Raspberry Pi 5, which allows using GPIO pins to connect arcade-style joysticks with digital buttons.

## Features

* Supports up to 2 joysticks, each with 13 inputs:

  * 4 digital directions: Up, Down, Left, Right.
  * 9 buttons: Start, Select, A, B, TR, Y, X, TL, Home.
* High-performance 1 ms polling using hrtimers.
* Configurable via `ARCADE_MODE` and device tree overlays.
* Device Tree compilation integrated into the `Makefile` for ease of use.

## Requirements

* Raspberry Pi 5 (RP1-based GPIO pin controller).
* Linux kernel version >= 6.6.
* `evtest` or `jstest` for input testing.

## Installation

### Install dependencies

```bash
sudo apt install gpiod libgpiod-dev device-tree-compiler
```

### Automatic Installation (Recommended)

Compile and install the kernel module and device tree overlay automatically:

```bash
make
sudo make install
sudo depmod -a
```

Reboot to activate the changes:

```bash
sudo reboot
```

### Manual Installation

#### 1. Compile the Driver

```bash
make
```

#### 2. Insert the Module Temporarily

Manually insert the module (temporary until reboot):

```bash
sudo insmod gpio-joystick.ko map=1,2
```

#### 3. Check Logs

Verify the driver is loaded and joysticks are detected:

```bash
dmesg | grep "GPIO Joystick"
```

## Testing

### Using `evtest`

Install and run `evtest` to verify joystick events:

```bash
sudo apt install evtest
sudo evtest
```

Select the appropriate input device corresponding to your joystick.

### Using `jstest`

Install and test the joystick with `jstest`:

```bash
sudo apt install joystick
jstest /dev/input/js0
jstest /dev/input/js1
```

## Checking Pin Numbers and Status

### How to Check New Pin Numbers (kernel >= 6.6)

List GPIO pins with:

```bash
cat /sys/kernel/debug/gpio
```

### How to Check Pin Status on Pi 5

Inspect pin configuration:

```bash
sudo pinctrl
```

## Manually Setting Pull-Ups

To manually configure pull-ups for GPIO pins:

```bash
sudo pinctrl gpiochip4 4 ip pu   # Set GPIO4 as input with pull-up
sudo pinctrl gpiochip4 17 ip pu  # Set GPIO17 as input with pull-up
sudo pinctrl gpiochip4 27 ip pu  # Set GPIO27 as input with pull-up
sudo pinctrl gpiochip4 22 ip pu  # Set GPIO22 as input with pull-up
```

Repeat this for all GPIO pins used by your joystick.

## Device Tree Overlay (permanent configuration)

The overlay `gpio-joystick.dts` is compiled automatically via the `Makefile`.

### Manual Compilation and Installation

Run the following commands to build and install the overlay manually:

```bash
make dtb
sudo cp gpio-joystick.dtbo /boot/overlays/
```

### Enable the Overlay

Edit `/boot/config.txt` and add:

```bash
dtoverlay=gpio-joystick
```

Reboot to apply changes:

```bash
sudo reboot
```

## Contributions

Contributions are welcome! Please fork the repository, make changes, and submit a pull request.

## License

This project is licensed under the GPLv2 License. See the LICENSE file for details.
