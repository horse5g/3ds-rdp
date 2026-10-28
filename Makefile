.SUFFIXES:
ifeq ($(strip $(DEVKITARM)),)
$(error Set DEVKITARM to your devkitARM installation)
endif
TOPDIR ?= $(CURDIR)
include $(DEVKITARM)/3ds_rules
TARGET := 3ds-rdp
BUILD := build
SOURCES := source
INCLUDES := include
ARCH := -march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft
CFLAGS := -g -Wall -Wextra -O2 -mword-relocations -ffunction-sections $(ARCH)
CFLAGS += $(INCLUDE) -D__3DS__ -std=gnu11
ASFLAGS := -g $(ARCH)
LDFLAGS := -specs=3dsx.specs -g $(ARCH) -Wl,-Map,$(notdir $*.map)
LIBS := -lctru -lm
LIBDIRS := $(CTRULIB)
# Set RDP_FULL_CLIENT=1 only after cross-compiling the OpenSSL and
# FreeRDP archives. The normal build remains the known-working probe.
ifeq ($(RDP_FULL_CLIENT),1)
FRROOT := $(TOPDIR)/external/FreeRDP
FRBUILD := $(TOPDIR)/build/freerdp-3ds
SSLROOT := $(TOPDIR)/external/openssl
SSLBUILD := $(TOPDIR)/build/openssl-3ds
CFLAGS += -DRDP_FULL_CLIENT
CFLAGS += -I$(FRROOT)/include -I$(FRROOT)/winpr/include
CFLAGS += -I$(FRBUILD) -I$(FRBUILD)/include -I$(FRBUILD)/winpr -I$(FRBUILD)/winpr/include
CFLAGS += -I$(SSLBUILD)/include -I$(SSLROOT)/include
LIBDIRS += $(DEVKITPRO)/portlibs/3ds
LIBS := -Wl,--start-group -lfreerdp3 -lwinpr3 -lssl -lcrypto -lz -lctru -lm -Wl,--end-group
endif
export APP_TITLE := 3DS RDP Probe
export APP_DESCRIPTION := Standalone RDP negotiation test
export APP_AUTHOR := 3ds-rdp contributors
ifneq ($(BUILD),$(notdir $(CURDIR)))
export OUTPUT := $(CURDIR)/$(TARGET)
export TOPDIR := $(CURDIR)
export VPATH := $(foreach dir,$(SOURCES),$(CURDIR)/$(dir))
export DEPSDIR := $(CURDIR)/$(BUILD)
CFILES := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
export OFILES := $(CFILES:.c=.o)
export INCLUDE := $(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) $(foreach dir,$(LIBDIRS),-I$(dir)/include) -I$(CURDIR)/$(BUILD)
export LIBPATHS := $(foreach dir,$(LIBDIRS),-L$(dir)/lib)
ifeq ($(RDP_FULL_CLIENT),1)
# FreeRDP/OpenSSL CMake and Configure emit archives directly in these build dirs,
# not their /lib subdirectories, unlike devkitPro portlibs.
export LIBPATHS += -L$(FRBUILD)/libfreerdp -L$(FRBUILD)/winpr/libwinpr -L$(SSLBUILD)
endif
export LD := $(CC)
export _3DSXDEPS := $(OUTPUT).smdh
export _3DSXFLAGS += --smdh=$(OUTPUT).smdh
.PHONY: all clean test
all: $(BUILD)
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile
$(BUILD):
	@mkdir -p $@
clean:
	@rm -rf $(BUILD) $(TARGET).3dsx $(TARGET).elf $(TARGET).smdh $(TARGET).map
test:
	@$(MAKE) -C tests test
else
$(OUTPUT).3dsx: $(OUTPUT).elf $(_3DSXDEPS)
$(OUTPUT).elf: $(OFILES)
-include $(DEPSDIR)/*.d
endif
