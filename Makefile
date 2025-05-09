# Module name
obj-m += gpio-joystick.o

# Get kernel version and directory
KERNEL_SRC := /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)

# Device Tree Compiler
DTC := dtc

# Allow passing custom CONFIG_ARCH_BCMxxxx definitions. Example: make CONFIG_ARCH_BCM2712=1
EXTRA_CFLAGS := -DARCADE_MODE=1  # Enable arcade-style inputs
EXTRA_CFLAGS += $(if $(CONFIG_ARCH_BCM2835),-DCONFIG_ARCH_BCM2835=$(CONFIG_ARCH_BCM2835))
EXTRA_CFLAGS += $(if $(CONFIG_ARCH_BCM2836),-DCONFIG_ARCH_BCM2836=$(CONFIG_ARCH_BCM2836))
EXTRA_CFLAGS += $(if $(CONFIG_ARCH_BCM2837),-DCONFIG_ARCH_BCM2837=$(CONFIG_ARCH_BCM2837))
EXTRA_CFLAGS += $(if $(CONFIG_ARCH_BCM2711),-DCONFIG_ARCH_BCM2711=$(CONFIG_ARCH_BCM2711))
EXTRA_CFLAGS += $(if $(CONFIG_ARCH_BCM2712),-DCONFIG_ARCH_BCM2712=$(CONFIG_ARCH_BCM2712))

# Default target
all: modules dtb

# Build the kernel module
modules:
	$(MAKE) -C $(KERNEL_SRC) M=$(PWD) EXTRA_CFLAGS="$(EXTRA_CFLAGS)" modules

# Compile the DTS file into a DTB
dtb: gpio-joystick.dts
	$(DTC) -I dts -O dtb -o gpio-joystick.dtbo gpio-joystick.dts

# Install module and device tree overlay (requires root)
install: modules dtb
	$(MAKE) -C $(KERNEL_SRC) M=$(PWD) EXTRA_CFLAGS="$(EXTRA_CFLAGS)" modules_install
	install -D -m 0644 gpio-joystick.dtbo /boot/firmware/overlays/gpio-joystick.dtbo
	depmod -a

# Clean target
clean:
	$(MAKE) -C $(KERNEL_SRC) M=$(PWD) clean
	$(RM) gpio-joystick.dtbo

# Phony targets
.PHONY: all modules dtb clean install