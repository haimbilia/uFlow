include $(DEVKITPPC)/wii_rules
CXXFLAGS := $(MACHDEP) -std=c++17 -O2 -Wall -Wextra -I$(LIBOGC_INC)
LDFLAGS := $(MACHDEP) -L$(LIBOGC_LIB)
.PHONY: all
all: boot.dol
boot.elf: source/main.cpp packet.hpp transport.hpp
	$(CXX) $(CXXFLAGS) source/main.cpp $(LDFLAGS) -lfat -logc -lm -o $@
boot.dol: boot.elf
	elf2dol $< $@
