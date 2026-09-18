# Source directory (relative to this Makefile)
SRC_DIR := .

# Source files in dependency order
SRCS := device.c \
        devicemanager.c \
        floppy/device_floppy_pio.c \
        floppy/device_floppy_dma.c \
        papertape/device_paper_tape.c \
        smd/device_smd.c \
        smd/disk_smd.c \
        scsi/device_scsi.c \
        rtc/device_rtc.c \
        panel/panel.c \
        terminal/device_terminal.c

# Include paths for all device subdirectories
INCLUDES := -I$(SRC_DIR) \
           -I$(SRC_DIR)/common \
           -I$(SRC_DIR)/terminal \
           -I$(SRC_DIR)/floppy \
           -I$(SRC_DIR)/papertape \
           -I$(SRC_DIR)/smd \
           -I$(SRC_DIR)/scsi \
           -I$(SRC_DIR)/rtc \
           -I$(SRC_DIR)/panel \
           -I../cpu \
           -I../ndlib

# Devices module specific flags
CFLAGS += -D_GNU_SOURCE -DDEBUG

# Module name
MODULE := devices

# Build paths - use parent's paths if available, otherwise set defaults
BUILD_DIR ?= ../../build
OBJ_DIR ?= $(BUILD_DIR)/obj
LIB_DIR ?= $(BUILD_DIR)/lib


# Build paths for this module
MODULE_OBJ_DIR := $(OBJ_DIR)/$(MODULE)
OBJS := $(SRCS:%.c=$(MODULE_OBJ_DIR)/%.o)
MODULE_LIB := $(LIB_DIR)/lib$(MODULE).a

# Required libraries
LIBS := -L$(LIB_DIR) -lndlib

# point this at wherever you built mkptypes
MKPTYPES    := ./../../tools/mkptypes/mkptypes

# Combine prototypes into cpu_protos.h
devices_protos.h: $(SRCS)
	@echo "Creating combined prototype header $@"
	@echo "/* AUTO-GENERATED FILE. DO NOT EDIT! */" > $@
	@for src in $(SRCS); do \
		echo "/* Prototypes from $$src */" >> $@; \
		$(MKPTYPES) $$src >> $@; \
		echo "" >> $@; \
	done


# Create necessary directories
create_dirs:
	@mkdir -p $(MODULE_OBJ_DIR)/common
	@mkdir -p $(MODULE_OBJ_DIR)/floppy
	@mkdir -p $(MODULE_OBJ_DIR)/papertape
	@mkdir -p $(MODULE_OBJ_DIR)/smd
	@mkdir -p $(MODULE_OBJ_DIR)/rtc
	@mkdir -p $(MODULE_OBJ_DIR)/panel
	@mkdir -p $(MODULE_OBJ_DIR)/terminal

# Build the library
all: | create_dirs $(PIS) $(MODULE_LIB)

# Create the static library
$(MODULE_LIB): $(OBJS)
	$(AR) rcs $@ $^

# Pattern rule for object files
$(MODULE_OBJ_DIR)/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(LDFLAGS) $(CFLAGS) $(INCLUDES) $(LIBS) -c $< -o $@

# Header dependencies
$(MODULE_OBJ_DIR)/device.o: devices_protos.h devices_types.h    
$(MODULE_OBJ_DIR)/devicemanager.o: devices_protos.h devices_types.h    
$(MODULE_OBJ_DIR)/floppy/device_floppy_pio.o: floppy/device_floppy_pio.h devices_protos.h
$(MODULE_OBJ_DIR)/floppy/device_floppy_dma.o: floppy/device_floppy_dma.h devices_protos.h
$(MODULE_OBJ_DIR)/papertape/device_paper_tape.o: papertape/device_paper_tape.h devices_protos.h
$(MODULE_OBJ_DIR)/smd/device_smd.o: smd/device_smd.h smd/disk_smd.h devices_protos.h
$(MODULE_OBJ_DIR)/smd/disk_smd.o: smd/disk_smd.h
$(MODULE_OBJ_DIR)/scsi/device_scsi.o: scsi/device_scsi.h devices_protos.h
$(MODULE_OBJ_DIR)/rtc/device_rtc.o: rtc/device_rtc.h devices_protos.h
$(MODULE_OBJ_DIR)/panel/panel.o: panel/panel.h devices_protos.h
$(MODULE_OBJ_DIR)/terminal/device_terminal.o: terminal/device_terminal.h devices_protos.h

# Clean module's build artifacts
clean:
	rm -f $(OBJS)
	rm -f $(MODULE_LIB)

.PHONY: all clean create_dirs 

# Default target
.DEFAULT_GOAL := all
