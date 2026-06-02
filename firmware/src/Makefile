all: flash

TARGET     := noknok_leds
TARGET_MCU := CH32V203

# ch32fun path on RPi4
CH32FUN_DIR := /home/noknok/dev/ch32fun

ADDITIONAL_C_FILES :=

include $(CH32FUN_DIR)/ch32fun/ch32fun.mk

# Point to the fsusb USB device library in ch32fun extralibs
CFLAGS += -I$(CH32FUN_DIR)/extralibs
EXTRA_C_FILES += $(CH32FUN_DIR)/extralibs/fsusb.c

flash:  cv_flash
clean:  cv_clean
