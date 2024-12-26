# GPIO Joystick Driver for Raspberry Pi 5

This repository contains the GPIO Joystick Driver for Raspberry Pi 5, which allows using GPIO pins to connect arcade-style joysticks with digital buttons.

## Features

- Supports up to 2 joysticks, each with 12 inputs:
  - 4 digital directions: Up, Down, Left, Right.
  - 8 buttons: Start, Select, A, B, TR, Y, X, TL.
- High-performance 1 ms polling using hrtimers.
- Configurable via device tree overlays and module parameters.

## Requirements

- Raspberry Pi 5 (RP1-based GPIO pin controller).
- Linux kernel version >= 6.6.
- `evtest` or `jstest` for input testing.

## Installation

### 1. Compile the Driver

```bash
make
```

### 2. Insert the Module

Manually insert the module (this installation is temporary and will last until the system is rebooted):

```bash
sudo insmod gpio-joystick-rpi.ko map=1,2
```

### 3. Check Logs

Verify the driver is loaded and joysticks are detected:

```bash
dmesg | grep "GPIO Joystick"
```

## Testing

### Using `evtest`

Install `evtest` and use it to check joystick events:

```bash
sudo apt install evtest
sudo evtest
```

Select the appropriate input device corresponding to your joystick.

### Using `jstest`

Install `jstest` and test the joystick:

```bash
sudo apt install joystick
jstest /dev/input/js0
jstest /dev/input/js1
```

## Checking Pin Numbers and Status

### How to Check New Pin Numbers (kernel >= 6.6)

Use the following command to list GPIO pins:

```bash
cat /sys/kernel/debug/gpio
```

### How to Check Pin Status on Pi 5

Use `pinctrl` to inspect pin configuration:

```bash
sudo pinctrl
```

## Manually Setting Pull-Ups

To manually configure pull-ups for GPIO pins using `pinctrl`:

```bash
sudo pinctrl gpiochip4 4 ip pu   # Set GPIO4 as input with pull-up
sudo pinctrl gpiochip4 17 ip pu  # Set GPIO17 as input with pull-up
sudo pinctrl gpiochip4 27 ip pu  # Set GPIO27 as input with pull-up
sudo pinctrl gpiochip4 22 ip pu  # Set GPIO22 as input with pull-up
```

Repeat the process for all GPIO pins used by your joystick.

## Device Tree Overlay

For permanent configuration, use the provided `gpio-joystick-rpi.dts` overlay. Compile and load it as follows:

### 1. Compile the Overlay

```bash
dtc -I dts -O dtb -o gpio-joystick-rpi.dtbo gpio-joystick-rpi.dts
```

### 2. Copy the Overlay to `/boot/overlays`

```bash
sudo cp gpio-joystick-rpi.dtbo /boot/overlays/
```

### 3. Enable the Overlay

Add the following line to `/boot/config.txt`:

```bash
dtoverlay=gpio-joystick-rpi
```

### 4. Reboot

```bash
sudo reboot
```

## Contributions

Contributions are welcome! Please fork the repository, make changes, and submit a pull request.

## License

This project is licensed under the GPLv2 License. See the LICENSE file for details.

