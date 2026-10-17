# GGSMDriver firmware (STM32G0B1CBT6). Command-line build with arm-none-eabi-gcc;
# the same sources build in STM32CubeIDE (see README.md).
#
#   make            build/ggsmdriver.elf, .bin, .hex and a size report
#   make OPT=-Og    different optimization level (default -Os)
#   make clean

TARGET := ggsmdriver
BUILD  := build

PREFIX ?= arm-none-eabi-
CC     := $(PREFIX)gcc
OBJCOPY := $(PREFIX)objcopy
SIZE   := $(PREFIX)size

OPT ?= -Os
EXTRA_CFLAGS ?=

HAL := Drivers/STM32G0xx_HAL_Driver
USB := Middlewares/ST/STM32_USB_Device_Library

# Our code: built with -Wall -Wextra, expected to stay warning free.
OWN_C := \
  Core/Src/main.c \
  Core/Src/board_time.c \
  Core/Src/serial.c \
  Core/Src/stm32g0xx_it.c \
  Core/Src/stm32g0xx_hal_msp.c \
  USB_Device/App/usb_device.c \
  USB_Device/App/usbd_cdc_if.c \
  USB_Device/App/usbd_desc.c \
  USB_Device/Target/usbd_conf.c \
  ggstepper/ggstepper.c \
  ggstepper/ggstepper_hw.c \
  automato_bridge/automato_bridge.c

# ST code, unmodified or nearly so: compiled with warnings off.
VENDOR_C := \
  Core/Src/system_stm32g0xx.c \
  Core/Src/syscalls.c \
  Core/Src/sysmem.c \
  $(HAL)/Src/stm32g0xx_hal.c \
  $(HAL)/Src/stm32g0xx_hal_cortex.c \
  $(HAL)/Src/stm32g0xx_hal_dma.c \
  $(HAL)/Src/stm32g0xx_hal_dma_ex.c \
  $(HAL)/Src/stm32g0xx_hal_exti.c \
  $(HAL)/Src/stm32g0xx_hal_flash.c \
  $(HAL)/Src/stm32g0xx_hal_flash_ex.c \
  $(HAL)/Src/stm32g0xx_hal_gpio.c \
  $(HAL)/Src/stm32g0xx_hal_pcd.c \
  $(HAL)/Src/stm32g0xx_hal_pcd_ex.c \
  $(HAL)/Src/stm32g0xx_hal_pwr.c \
  $(HAL)/Src/stm32g0xx_hal_pwr_ex.c \
  $(HAL)/Src/stm32g0xx_hal_rcc.c \
  $(HAL)/Src/stm32g0xx_hal_rcc_ex.c \
  $(HAL)/Src/stm32g0xx_hal_tim.c \
  $(HAL)/Src/stm32g0xx_hal_tim_ex.c \
  $(HAL)/Src/stm32g0xx_hal_uart.c \
  $(HAL)/Src/stm32g0xx_hal_uart_ex.c \
  $(HAL)/Src/stm32g0xx_ll_usb.c \
  $(USB)/Core/Src/usbd_core.c \
  $(USB)/Core/Src/usbd_ctlreq.c \
  $(USB)/Core/Src/usbd_ioreq.c \
  $(USB)/Class/CDC/Src/usbd_cdc.c

ASM := Core/Startup/startup_stm32g0b1cbtx.s

LDSCRIPT := STM32G0B1CBTX_FLASH.ld

DEFS := -DSTM32G0B1xx -DUSE_HAL_DRIVER

INCS := \
  -ICore/Inc \
  -IUSB_Device/App \
  -IUSB_Device/Target \
  -Iggstepper \
  -Iautomato_bridge \
  -I$(HAL)/Inc \
  -I$(HAL)/Inc/Legacy \
  -IDrivers/CMSIS/Device/ST/STM32G0xx/Include \
  -IDrivers/CMSIS/Include \
  -I$(USB)/Core/Inc \
  -I$(USB)/Class/CDC/Inc

CPU := -mcpu=cortex-m0plus -mthumb -mfloat-abi=soft

COMMON := $(CPU) $(DEFS) $(INCS) $(OPT) -g3 -std=gnu11 \
  -ffunction-sections -fdata-sections -fstack-usage -MMD -MP $(EXTRA_CFLAGS)
OWN_FLAGS := -Wall -Wextra
VENDOR_FLAGS := -w

LDFLAGS := $(CPU) -T$(LDSCRIPT) -specs=nano.specs \
  -Wl,-Map=$(BUILD)/$(TARGET).map,--cref -Wl,--gc-sections \
  -Wl,--start-group -lc -lm -Wl,--end-group

OWN_OBJ := $(patsubst %.c,$(BUILD)/%.o,$(OWN_C))
VENDOR_OBJ := $(patsubst %.c,$(BUILD)/%.o,$(VENDOR_C))
ASM_OBJ := $(patsubst %.s,$(BUILD)/%.o,$(ASM))
OBJ := $(OWN_OBJ) $(VENDOR_OBJ) $(ASM_OBJ)

.PHONY: all clean
all: $(BUILD)/$(TARGET).elf $(BUILD)/$(TARGET).bin $(BUILD)/$(TARGET).hex
	$(SIZE) $(BUILD)/$(TARGET).elf

$(BUILD)/$(TARGET).elf: $(OBJ) $(LDSCRIPT)
	@echo LD $@
	@$(CC) $(OBJ) $(LDFLAGS) -o $@

$(BUILD)/%.bin: $(BUILD)/%.elf
	@echo BIN $@
	@$(OBJCOPY) -O binary $< $@

$(BUILD)/%.hex: $(BUILD)/%.elf
	@echo HEX $@
	@$(OBJCOPY) -O ihex $< $@

$(OWN_OBJ): $(BUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	@echo CC $<
	@$(CC) $(COMMON) $(OWN_FLAGS) -c $< -o $@

$(VENDOR_OBJ): $(BUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	@echo CC $<
	@$(CC) $(COMMON) $(VENDOR_FLAGS) -c $< -o $@

$(ASM_OBJ): $(BUILD)/%.o: %.s
	@mkdir -p $(dir $@)
	@echo AS $<
	@$(CC) $(CPU) -x assembler-with-cpp -c $< -o $@

clean:
	rm -rf $(BUILD)

-include $(OBJ:.o=.d)
