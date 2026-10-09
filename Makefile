PROJECT_NAME := AshIntroProjectFSAE
PROJECT_VERSION := f33

#    ░█▀█░█▀█░▀█▀░▀█▀░█▀█░█▀█░█▀▀
#    ░█░█░█▀▀░░█░░░█░░█░█░█░█░▀▀█
#    ░▀▀▀░▀░░░░▀░░▀▀▀░▀▀▀░▀░▀░▀▀▀
# Make configuration
V ?= 0
ifeq ($(V),3)
Q :=
else
Q := @
endif

# Build info
BUILD_DIR := build
STM32_BUILD_DIR := $(BUILD_DIR)/stm32

# Cross-compilation tooling
STM32_PREFIX := arm-none-eabi
STM32_CC := $(STM32_PREFIX)-gcc
STM32_ASM := $(STM32_PREFIX)-gcc
STM32_LD := $(STM32_PREFIX)-gcc
STM32_GDB := $(STM32_PREFIX)-gdb
STM32_OBJCOPY := $(STM32_PREFIX)-objcopy
STM32_OBJDUMP := $(STM32_PREFIX)-objdump

# Cross-compilation options
STM32_COMMON_FLAGS := -mcpu=cortex-m4 -mfpu=fpv4-sp-d16 -mfloat-abi=hard -D USE_HAL_DRIVER -D STM32G441xx
STM32_CC_FLAGS := $(STM32_COMMON_FLAGS) -ffreestanding -ffunction-sections -fdata-sections -Wall -Wextra -Werror=implicit-function-declaration -g
STM32_ASM_FLAGS := $(STM32_CC_FLAGS)
STM32_LD_SCRIPT := STM32G441xBxx_FLASH.ld
STM32_LD_FLAGS := $(STM32_COMMON_FLAGS) -static -Wl,--gc-sections -T $(STM32_LD_SCRIPT) -specs=nano.specs -specs=nosys.specs

#    ░█▀▀░█▀█░█░█░█▀▄░█▀▀░█▀▀░█▀▀
#    ░▀▀█░█░█░█░█░█▀▄░█░░░█▀▀░▀▀█
#    ░▀▀▀░▀▀▀░▀▀▀░▀░▀░▀▀▀░▀▀▀░▀▀▀
# ========== Variable Setup ===========
ALL_MODS :=

RAW_OBJS :=
RAW_INCS := 
RAW_SRCS := 

## @macro rwildcard
## @brief Recursive wildcard search
rwildcard = $(wildcard $(1)/$(2)) $(foreach d,\
$(wildcard $(1)/*),$(call rwildcard,$d,$(2)))

## @macro ADDMODULE
## @brief Stages a module's source and include directories for compilation
## @param $1 module namespace
## @param $2 source directories
## @param $3 include directories
define ADDMODULE
ALL_MODS += $(1)
$(1)_OBJDIR := $$(STM32_BUILD_DIR)/obj/$(1)

$(1)_SRCS := $$(foreach d,$(2),$$(wildcard $$(d)/*.c))
$(1)_INCS := $(3)
endef

# ========= Internal Sources ==========
# App
APP_DIRS := src/app
$(eval $(call ADDMODULE,APP,,$(APP_DIRS)))
APP_SRCS += $(call rwildcard,$(APP_DIRS),*.c)

# Driver
DRIVER_DIRS := src/driver
$(eval $(call ADDMODULE,DRIVER,,$(DRIVER_DIRS)))
DRIVER_SRCS := $(call rwildcard,$(DRIVER_DIRS),*.c)

# ========= External Sources ==========
# STM32CUBE
STM32CUBE_DIR := $(strip $(STM32_CUBE_G4))
STM32CUBE_HAL_DIR := $(STM32CUBE_DIR)/Drivers/STM32G4xx_HAL_Driver
STM32CUBE_CMSIS_DIR := $(STM32CUBE_DIR)/Drivers/CMSIS/Device/ST/STM32G4xx
SRCS_STM32CUBE := $(STM32CUBE_DIR) \
                  $(STM32CUBE_HAL_DIR)/Src \
		  $(STM32CUBE_CMSIS_DIR)/Source/Templates
INCS_STM32CUBE := $(STM32CUBE_DIR)/Drivers/CMSIS/Include \
                  $(STM32CUBE_HAL_DIR)/Inc \
                  $(STM32CUBE_CMSIS_DIR)/Include

$(eval $(call ADDMODULE,STM32CUBE,$(SRCS_STM32CUBE),$(INCS_STM32CUBE)))
STM32CUBE_SRCS += $(STM32CUBE_CMSIS_DIR)/Source/Templates/system_stm32g4xx.c

# FREERTOS
FREERTOS_DIR := $(strip $(FREERTOS_KERNEL))
SRCS_FREERTOS := $(FREERTOS_DIR) \
                 $(FREERTOS_DIR)/portable/GCC/ARM_CM4F
INCS_FREERTOS := $(FREERTOS_DIR)/include \
                 $(FREERTOS_DIR)/portable/GCC/ARM_CM4F

$(eval $(call ADDMODULE,FREERTOS,$(SRCS_FREERTOS),$(INCS_FREERTOS)))
FREERTOS_SRCS += $(FREERTOS_DIR)/portable/MemMang/heap_4.c

# SEGGER RTT
RTT_DIR := $(strip $(SEGGER_RTT))
ifneq ($(wildcard $(RTT_DIR)),)
INCS_RTT := $(RTT_DIR)/src $(RTT_DIR)/RTT
$(eval $(call ADDMODULE,RTT,,$(INCS_RTT)))
RTT_SRCS += $(RTT_DIR)/RTT/SEGGER_RTT.c \
            $(RTT_DIR)/Syscalls/SEGGER_RTT_Syscalls_GCC.c \
            $(RTT_DIR)/RTT/SEGGER_RTT_printf.c
endif

# RITFSAE DBC
DBC_DIR := $(strip $(FSAE_DBC))
ifneq ($(wildcard $(DBC_DIR)),)
INCS_DBC := $(DBC_DIR)/c_files
$(eval $(call ADDMODULE,DBC,,$(INCS_DBC)))
DBC_SRCS += $(call rwildcard,$(DBC_DIR)/c_files,*.c)
endif

# RITFSAE Core
CORE_DIR := $(FSAE_CORE_G441)/src/driver
INCS_CORE := $(CORE_DIR)/Inc $(CORE_DIR)/Src $(INCS_STM32CUBE) $(INCS_FREERTOS) $(INCS_RTT)
$(eval $(call ADDMODULE,CORE,,$(INCS_CORE)))
CORE_SRCS += $(call rwildcard,$(CORE_DIR)/Src,*.c)

#    ░█▀▀░█▀█░█▄█░█▀█░▀█▀░█░░░█▀▀
#    ░█░░░█░█░█░█░█▀▀░░█░░█░░░█▀▀
#    ░▀▀▀░▀▀▀░▀░▀░▀░░░▀▀▀░▀▀▀░▀▀▀
# ========= Rules Generator ===========
## @macro GENRULES
## @brief Validates a module and generates its build targets
## @param $1 module namespace
define GENRULES 
ifeq ($$(strip $$($(1)_SRCS)),)
$$(warning COMPILE [WARN]: Source files for module '$(1)' missing!)
else
$(1)_OBJS := $$(addprefix $$($(1)_OBJDIR)/,$$(notdir $$($(1)_SRCS:.c=.o)))
RAW_OBJS += $$($(1)_OBJS)
RAW_INCS += $$($(1)_INCS)
VPATH += $$(sort $$(dir $$($(1)_SRCS)))
$$($(1)_OBJDIR)/%.o: %.c
	@mkdir -p $$(@D)
	$$(call LOG,CC,$(1),$$<,$$(INC_FLAGS_SHORT) -o $$@)
	$$(Q)$$(STM32_CC) $$(STM32_CC_FLAGS) $$(ALL_INC_FLAGS) -c $$< -o $$@
endif
endef

# =========== Log Formatter ===========
## @macro SHORTEN
## @param $1 List to iterate over
define SHORTEN
$(eval acc := $(1))$(strip \
$(foreach m,$(ALL_MODS),\
$(if $($(m)_ROOT),\
$(eval acc := \
$(patsubst $($(m)_ROOT)/%,<$(m)>/%,$(acc))\
))))$(acc)
endef

## @macro LOG
## @param $1 tag (CC, LD, ...)
## @param $2 module (optional)
## @param $3 path
## @param $4 extra
_LOG_0 = $(if $(2),[$(2)] )$(notdir $(3))
_LOG_1 = $(call short,$(3))
_LOG_2 = $(_LOG_1) $(4)

ifneq ($(filter $(V),0 1 2),)
LOG = @printf '  %-6s %s\n' '$(1)' '$(_LOG_$(V))'
endif

# =========== Compile code ============
$(foreach mod,$(ALL_MODS),$(eval $(call GENRULES,$(mod))))

ALL_INC_DIRS = := $(sort $(RAW_INCS))
ALL_INC_FLAGS = -I src $(addprefix -I,$(ALL_INC_DIRS))

OUTNAME := $(STM32_BUILD_DIR)/$(PROJECT_NAME)-$(PROJECT_VERSION)

# Compilation targets
.PHONY: all
all: main

.PHONY: main
main: $(OUTNAME).elf $(OUTNAME).bin $(OUTNAME).ihex

# Main executable
$(OUTNAME).bin: $(OUTNAME).elf
	@mkdir -p $(@D)
	$(call LOG,BIN,,$@)
	$(Q)$(STM32_OBJCOPY) -O binary $< $@

# Assembly startup file target
STARTUP_OBJ := $(STM32_BUILD_DIR)/obj/STM32CUBE/startup_stm32g441xx.s.o

# Compile dynamically generated objects
$(OUTNAME).elf: $(RAW_OBJS) $(STARTUP_OBJ)
	@mkdir -p $(@D)
	$(call LOG,LD,,$@,$^)
	$(Q)$(STM32_LD) $(STM32_LD_FLAGS) $^ -o $@ -lc -lm

$(OUTNAME).ihex: $(OUTNAME).elf
	@mkdir -p $(@D)
	$(call LOG,IHEX,,$@,)
	$(Q)$(STM32_OBJCOPY) -O ihex $< $@

$(STARTUP_OBJ): src/startup_stm32g441xx.s
	@mkdir -p $(@D)
	$(call LOG,AS,STM32CUBE,$<)
	$(Q)$(STM32_CC) $(STM32_ASM_FLAGS) -c $< -o $@

# Misc targets
.PHONY: clean
clean:
	rm -r $(BUILD_DIR)

.PHONY: clean-user
clean-user:
	rm -r $(BUILD_DIR)/stm32/obj/APP
	rm -r $(BUILD_DIR)/stm32/obj/CORE
