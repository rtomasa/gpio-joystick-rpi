# Module name
obj-m += gpio-joystick-rpi.o

# Get kernel version and directory
KERNEL_SRC := /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)

# Allow passing custom CONFIG_ARCH_BCMxxxx definitions
EXTRA_CFLAGS :=
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
dtb: gpio-joystick-rpi.dts
	dtc -I dts -O dtb -o gpio-joystick-rpi.dtbo gpio-joystick-rpi.dts

# Clean target
clean:
	$(MAKE) -C $(KERNEL_SRC) M=$(PWD) clean
	$(RM) gpio-joystick-rpi.dtbo
