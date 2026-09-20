.SUFFIXES:
ifeq ($(strip $(DEVKITPRO)),)
$(error DEVKITPRO is required)
endif
TOPDIR ?= $(CURDIR)
APP_NAME := uFlow Setup
APP_SHORTNAME := uFlow Setup
APP_AUTHOR := Haim Bilia and contributors
# Escape the space for both make prerequisites and wuhbtool's shell argument.
# wuhbtool scales the original artwork to the required 128x128 menu icon.
APP_ICON := $(TOPDIR)/../Installer\ Icon.png
APP_CONTENT := $(TOPDIR)/content
include $(DEVKITPRO)/wut/share/wut_rules
TARGET := uFlow-Setup
BUILD := build
CFLAGS := -g -Wall -Wextra -O2 -ffunction-sections $(MACHDEP) $(INCLUDE) -D__WIIU__ -D__WUT__
CXXFLAGS := $(CFLAGS) -std=c++20
LDFLAGS = -g $(ARCH) $(RPXSPECS) -Wl,--gc-sections,-Map,$(notdir $*.map)
LIBS := -lmocha -lmbedcrypto -lSDL2_ttf -lSDL2 -lharfbuzz -lfreetype -lbrotlidec -lbrotlicommon -lbz2 -lpng -lz -lm -lwut
LIBDIRS := $(PORTLIBS) $(WUT_ROOT)
ifneq ($(BUILD),$(notdir $(CURDIR)))
export TOPDIR := $(CURDIR)
export OUTPUT := $(CURDIR)/$(TARGET)
export VPATH := $(CURDIR)/source $(CURDIR)/vendor/compat
export DEPSDIR := $(CURDIR)/$(BUILD)
export OFILES := main.o vwii.o d2x_patcher.o ios_common.o tinyxml2.o
export INCLUDE := -I$(CURDIR)/source -I$(CURDIR)/vendor/compat -I$(CURDIR)/vendor/libmocha/include $(foreach dir,$(LIBDIRS),-I$(dir)/include)
export LIBPATHS := -L$(CURDIR)/vendor/libmocha/lib $(foreach dir,$(LIBDIRS),-L$(dir)/lib)
export LD := $(CXX)
.PHONY: all $(BUILD) check-payload
all: $(BUILD)
check-payload:
	@python3 $(CURDIR)/../tools/sync-native-payload.py
$(BUILD): check-payload
	@[ -d $@ ] || mkdir -p $@
	@$(MAKE) --no-print-directory -C vendor/libmocha
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile
else
all: $(OUTPUT).wuhb
# Manifest stamp changes whenever embedded content is prepared.
$(OUTPUT).wuhb: $(OUTPUT).rpx ../source/generated_files.hpp ../content/font.ttf $(wildcard ../content/console/*)
$(OUTPUT).rpx: $(OUTPUT).elf
$(OUTPUT).elf: $(OFILES)
-include $(OFILES:.o=.d)
endif
