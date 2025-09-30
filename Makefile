# Module
obj-m += gpio-joystick.o

KERNEL_SRC := /lib/modules/$(shell uname -r)/build
PWD        := $(shell pwd)
DTC        ?= dtc
DTC_FLAGS  := -I dts -O dtb -@          # keep symbols for overlays

# Optional SoC flags from cmdline: make CONFIG_ARCH_BCM2712=1
EXTRA_CFLAGS += $(if $(CONFIG_ARCH_BCM2835),-DCONFIG_ARCH_BCM2835=$(CONFIG_ARCH_BCM2835))
EXTRA_CFLAGS += $(if $(CONFIG_ARCH_BCM2836),-DCONFIG_ARCH_BCM2836=$(CONFIG_ARCH_BCM2836))
EXTRA_CFLAGS += $(if $(CONFIG_ARCH_BCM2837),-DCONFIG_ARCH_BCM2837=$(CONFIG_ARCH_BCM2837))
EXTRA_CFLAGS += $(if $(CONFIG_ARCH_BCM2711),-DCONFIG_ARCH_BCM2711=$(CONFIG_ARCH_BCM2711))
EXTRA_CFLAGS += $(if $(CONFIG_ARCH_BCM2712),-DCONFIG_ARCH_BCM2712=$(CONFIG_ARCH_BCM2712))

all: modules dtb

modules:
	$(MAKE) -C $(KERNEL_SRC) M=$(PWD) EXTRA_CFLAGS="$(EXTRA_CFLAGS)" \
	    INSTALL_MOD_DIR=extra modules

dtb: gpio-joystick.dts
	$(DTC) $(DTC_FLAGS) -o gpio-joystick.dtbo $<

install: modules dtb
	$(MAKE) -C $(KERNEL_SRC) M=$(PWD) INSTALL_MOD_DIR=extra modules_install
	install -D -m0644 gpio-joystick.dtbo /boot/firmware/overlays/gpio-joystick.dtbo
	depmod -a

uninstall:
	rm -f /lib/modules/$(shell uname -r)/extra/gpio-joystick.ko*
	rm -f /boot/firmware/overlays/gpio-joystick.dtbo
	depmod -a

clean:
	$(MAKE) -C $(KERNEL_SRC) M=$(PWD) clean
	rm -f gpio-joystick.dtbo

.PHONY: all modules dtb install uninstall clean
