.SUFFIXES:
ifeq ($(strip $(DEVKITPRO)),)
$(error DEVKITPRO is required)
endif
include $(DEVKITPRO)/wut/share/wut_rules
BUILD := build
CFLAGS := -g -Wall -O2 -ffunction-sections $(MACHDEP) $(INCLUDE) -D__WIIU__ -D__WUT__
CXXFLAGS := $(CFLAGS) -std=c++20
LDFLAGS = -g $(ARCH) $(RPXSPECS) -Wl,--gc-sections
LIBS := -lrpxloader -lSDL2_ttf -lSDL2 -lharfbuzz -lfreetype -lbrotlidec -lbrotlicommon -lbz2 -lpng -lz -lm -lwut
LIBDIRS := $(PORTLIBS) $(WUT_ROOT) $(WUT_ROOT)/usr $(DEVKITPRO)/wums
ifneq ($(BUILD),$(notdir $(CURDIR)))
export OUTPUT := $(CURDIR)/install-helper
export VPATH := $(CURDIR)/source $(CURDIR)/../app/vendor/monocypher $(CURDIR)/../app/data
export DEPSDIR := $(CURDIR)/build
export OFILES := main.o monocypher.o monocypher-ed25519.o glass.ttf.o
export INCLUDE := -I$(CURDIR)/../app/source -I$(CURDIR)/build $(foreach dir,$(LIBDIRS),-I$(dir)/include)
export LIBPATHS := $(foreach dir,$(LIBDIRS),-L$(dir)/lib)
export LD := $(CXX)
.PHONY: all
all:
	@mkdir -p build ../app/content/updates
	@$(MAKE) --no-print-directory -C build -f $(CURDIR)/Makefile
	@cmp -s install-helper.rpx ../app/content/updates/install-helper.rpx || cp install-helper.rpx ../app/content/updates/install-helper.rpx
else
all: $(OUTPUT).rpx
$(OUTPUT).rpx: $(OUTPUT).elf
$(OUTPUT).elf: $(OFILES)
main.o: glass_ttf.h
%.ttf.o %_ttf.h: %.ttf
	$(bin2o)
-include $(OFILES:.o=.d)
endif
