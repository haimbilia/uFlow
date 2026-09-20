#-------------------------------------------------------------------------------
.SUFFIXES:
#-------------------------------------------------------------------------------
ifeq ($(strip $(DEVKITPRO)),)
$(error "Please set DEVKITPRO")
endif

TOPDIR ?= $(CURDIR)
APP_NAME := uFlow
APP_SHORTNAME := uFlow
APP_AUTHOR := Haim Bilia and contributors
APP_ICON := $(TOPDIR)/data/icon.png
APP_CONTENT := $(TOPDIR)/content

include $(DEVKITPRO)/wut/share/wut_rules

WUMS_ROOT := $(DEVKITPRO)/wums
TARGET := uFlow
BUILD := build
SOURCES := source vendor/monocypher vendor/fatfs
DATA := data
INCLUDES :=

CFLAGS := -g -Wall -Wextra -O2 -ffunction-sections $(MACHDEP)
CFLAGS += $(INCLUDE) -D__WIIU__ -D__WUT__
CXXFLAGS := $(CFLAGS) -std=c++20
ifneq ($(UFLOW_UPDATE_SEQUENCE),)
CXXFLAGS += -DUFLOW_UPDATE_SEQUENCE=$(UFLOW_UPDATE_SEQUENCE)ULL
endif
ASFLAGS := -g $(ARCH)
LDFLAGS = -g $(ARCH) $(RPXSPECS) -Wl,-Map,$(notdir $*.map)
LIBS := -lrpxloader -lmocha -lavformat -lavcodec -lswscale -lswresample -lavutil -lSDL2_ttf -lSDL2_mixer -lmodplug -lmpg123 -lvorbisfile -lvorbis -logg -lSDL2 -lharfbuzz -lfreetype -lbrotlidec -lbrotlicommon -lbz2 -lpng -lturbojpeg -lz -lm -lwut
LIBDIRS := $(PORTLIBS) $(WUT_ROOT) $(WUT_ROOT)/usr $(WUMS_ROOT)
LIBS := -lcurl -lmbedtls -lmbedx509 -lmbedcrypto $(LIBS)

ifneq ($(BUILD),$(notdir $(CURDIR)))
export OUTPUT := $(CURDIR)/$(TARGET)
export TOPDIR := $(CURDIR)
export VPATH := $(foreach dir,$(SOURCES),$(CURDIR)/$(dir)) \
                $(foreach dir,$(DATA),$(CURDIR)/$(dir))
export DEPSDIR := $(CURDIR)/$(BUILD)
CPPFILES := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp)))
CFILES := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
BINFILES := glass.ttf
export OFILES_BIN := $(addsuffix .o,$(BINFILES))
export OFILES_SOURCES := $(CPPFILES:.cpp=.o) $(CFILES:.c=.o)
export OFILES := $(OFILES_BIN) $(OFILES_SOURCES)
export HFILES := $(addsuffix .h,$(subst .,_,$(BINFILES)))
export INCLUDE := $(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) \
                  $(foreach dir,$(LIBDIRS),-I$(dir)/include) \
                  -I$(CURDIR)/$(BUILD)
export LIBPATHS := $(foreach dir,$(LIBDIRS),-L$(dir)/lib)
export LD := $(CXX)

.PHONY: $(BUILD) clean all update-helper
all: $(BUILD)
update-helper:
	@$(MAKE) -C ../update-helper
$(BUILD): update-helper
	@[ -d $@ ] || mkdir -p $@
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile
clean:
	@echo clean ...
	@rm -fr $(BUILD) $(TARGET).wuhb $(TARGET).rpx $(TARGET).elf
else
.PHONY: all
DEPENDS := $(OFILES:.o=.d)
all: $(OUTPUT).wuhb
$(OUTPUT).wuhb: $(OUTPUT).rpx $(wildcard ../content/skins/glass/background.mp4) ../content/updates/cacert.pem ../content/updates/install-helper.rpx
$(OUTPUT).rpx: $(OUTPUT).elf
$(OUTPUT).elf: $(OFILES)
$(OFILES_SOURCES): $(HFILES)
%.ttf.o %_ttf.h: %.ttf
	@echo $(notdir $<)
	$(bin2o)
-include $(DEPENDS)
endif
