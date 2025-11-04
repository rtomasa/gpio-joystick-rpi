# gpio-joystick-rpi Makefile con soporte dual: Kbuild (DKMS) y manual

ifneq ($(KERNELRELEASE),)
# ====== Rama Kbuild: invocada por "make -C $(KDIR) M=$(PWD) modules" ======

# Módulo
obj-m += gpio-joystick.o

# Flags opcionales por SoC (se propagan dentro del build del kernel)
SOC_FLAGS := \
  $(if $(CONFIG_ARCH_BCM2835),-DCONFIG_ARCH_BCM2835=$(CONFIG_ARCH_BCM2835)) \
  $(if $(CONFIG_ARCH_BCM2836),-DCONFIG_ARCH_BCM2836=$(CONFIG_ARCH_BCM2836)) \
  $(if $(CONFIG_ARCH_BCM2837),-DCONFIG_ARCH_BCM2837=$(CONFIG_ARCH_BCM2837)) \
  $(if $(CONFIG_ARCH_BCM2711),-DCONFIG_ARCH_BCM2711=$(CONFIG_ARCH_BCM2711)) \
  $(if $(CONFIG_ARCH_BCM2712),-DCONFIG_ARCH_BCM2712=$(CONFIG_ARCH_BCM2712))

ccflags-y += $(SOC_FLAGS)

else
# ====== Rama standalone: uso manual (make all, make install, etc.) ======

# Kernel build dir y utilidades
KERNEL_SRC := /lib/modules/$(shell uname -r)/build
PWD        := $(shell pwd)

DTC        ?= dtc
DTS_CPP    ?= cpp
DTC_FLAGS  := -I dts -O dtb -@                 # overlay symbols
CPPFLAGS_DTS := -x assembler-with-cpp -nostdinc -undef -D__DTS__ -Wno-trigraphs -P

# Flags opcionales por SoC para build externo
SOC_FLAGS := \
  $(if $(CONFIG_ARCH_BCM2835),-DCONFIG_ARCH_BCM2835=$(CONFIG_ARCH_BCM2835)) \
  $(if $(CONFIG_ARCH_BCM2836),-DCONFIG_ARCH_BCM2836=$(CONFIG_ARCH_BCM2836)) \
  $(if $(CONFIG_ARCH_BCM2837),-DCONFIG_ARCH_BCM2837=$(CONFIG_ARCH_BCM2837)) \
  $(if $(CONFIG_ARCH_BCM2711),-DCONFIG_ARCH_BCM2711=$(CONFIG_ARCH_BCM2711)) \
  $(if $(CONFIG_ARCH_BCM2712),-DCONFIG_ARCH_BCM2712=$(CONFIG_ARCH_BCM2712))

EXTRA_CFLAGS += $(SOC_FLAGS)

DTBO_PULLUP       := gpio-joystick-pullup.dtbo
DTBO_BIAS_DISABLE := gpio-joystick-bias-disable.dtbo
DTBOS := $(DTBO_PULLUP) $(DTBO_BIAS_DISABLE)

all: modules dtbo

modules:
	$(MAKE) -C $(KERNEL_SRC) M=$(PWD) EXTRA_CFLAGS="$(EXTRA_CFLAGS)" \
	    INSTALL_MOD_DIR=extra modules

dtbo: $(DTBOS)

# pull-up
$(DTBO_PULLUP): gpio-joystick.dts
	$(DTS_CPP) $(CPPFLAGS_DTS) -DBIAS_PULL_UP=1 $< > $*.tmp.dts
	$(DTC) $(DTC_FLAGS) -o $@ $*.tmp.dts
	@rm -f $*.tmp.dts

# bias-disable
$(DTBO_BIAS_DISABLE): gpio-joystick.dts
	$(DTS_CPP) $(CPPFLAGS_DTS) $< > $*.tmp.dts
	$(DTC) $(DTC_FLAGS) -o $@ $*.tmp.dts
	@rm -f $*.tmp.dts

install: modules dtbo
	$(MAKE) -C $(KERNEL_SRC) M=$(PWD) INSTALL_MOD_DIR=extra modules_install
	install -D -m0644 $(DTBO_PULLUP)        /boot/firmware/overlays/$(DTBO_PULLUP)
	install -D -m0644 $(DTBO_BIAS_DISABLE)  /boot/firmware/overlays/$(DTBO_BIAS_DISABLE)
	depmod -a

uninstall:
	rm -f /lib/modules/$(shell uname -r)/extra/gpio-joystick.ko*
	rm -f /boot/firmware/overlays/$(DTBO_PULLUP)
	rm -f /boot/firmware/overlays/$(DTBO_BIAS_DISABLE)
	depmod -a

clean:
	$(MAKE) -C $(KERNEL_SRC) M=$(PWD) clean
	rm -f $(DTBOS)

.PHONY: all modules dtbo install uninstall clean

endif
