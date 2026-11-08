MCU_SERIES = c5
CMSIS_MCU = STM32C552xx
MICROPY_FLOAT_IMPL = single
AF_FILE = boards/stm32c552_af.csv
STARTUP_FILE = $(STM32LIB_CMSIS_BASE)/Source/startup_$(CMSIS_MCU_LOWER).o

# Native littlefs filesystem on internal flash.
MICROPY_VFS_FAT = 0
MICROPY_VFS_LFS2 = 1

LD_FILES = $(BOARD_DIR)/stm32c552ve.ld boards/common_basic.ld
TEXT0_ADDR = 0x08000000
FROZEN_MANIFEST ?= $(BOARD_DIR)/manifest.py
