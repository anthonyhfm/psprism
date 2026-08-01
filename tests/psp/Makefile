PSPSDK := $(shell psp-config --pspsdk-path)
TARGET = roundtrip
OBJS = main.o generated.o
BUILD_PRX = 1
EXTRA_TARGETS = EBOOT.PBP
PSP_EBOOT_TITLE = PSPRecomp Roundtrip
VPATH = $(SOURCE_DIR)/tests/psp

CFLAGS = -O2 -Wall -I$(SOURCE_DIR)/include
CXXFLAGS = $(CFLAGS) -std=c++20 -fno-exceptions -fno-rtti

include $(PSPSDK)/lib/build.mak

