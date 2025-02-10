IOP_CFLAGS += -mno-check-zero-division
IOP_OBJS = main.o smap.o xfer.o ministack.o imports.o exports.o
HAS_BDM ?= 1
IOP_BIN = smap_udpbd.irx
BIN2C = $(PS2SDK)/bin/bin2c
BIN2S = $(PS2SDK)/bin/bin2s
BIN2O = $(PS2SDK)/bin/bin2o
IOP_SRC_DIR = src/
IOP_OBJS_DIR = obj/
IOP_INC_DIR = $(IOP_SRC_DIR)include

ifeq ($(HAS_BDM), 0)
  IOP_CFLAGS += -DNO_BDM
endif

ifeq ($(DEBUG),1)
  IOP_CFLAGS += -DDEBUG
endif

ifeq ($(NO_UDPBD),1)
  IOP_CFLAGS += -DNO_UDPBD
else
  IOP_OBJS += udpbd.o
endif

ifeq ($(NO_TTY),1)
  IOP_CFLAGS += -DNO_TTY
else
  IOP_OBJS += udptty.o
endif

all: $(IOP_BIN)

clean:
	rm -r -f $(IOP_BIN) $(IOP_OBJS) $(IOP_OBJS_DIR) $(IOP_BIN_DIR) $(IOP_LIB_DIR)

rebuild: clean all


include $(PS2SDK)/Defs.make
include $(PS2SDK)/samples/Makefile.iopglobal