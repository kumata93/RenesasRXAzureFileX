# Makefile for Unity Unit Testing (Host GCC / MinGW / Linux)
# Target: Renesas RX65N FileX + LevelX NOR Flash Simulator Tests

CC = gcc
CFLAGS ?= -Wall -Wextra -O2

# FileX/LevelX assume ULONG is 32-bit. On LP64 hosts (Linux x86_64) 'unsigned long' is
# 64-bit, which corrupts boot-record/metadata layouts, so build 32-bit there.
# (Windows MinGW already has a 32-bit 'long'.)
ifneq ($(OS),Windows_NT)
    CFLAGS += -m32
endif

# Include paths
INCLUDES = -Iunity \
           -Imocks \
           -I. \
           -I../libs/filex/common/inc \
           -I../libs/filex/ports/generic/inc \
           -I../libs/levelx/common/inc

# Standalone FileX and LevelX definitions
DEFINES = -DFX_SINGLE_THREAD \
          -DFX_NO_TIMER \
          -DFX_NO_LOCAL_PATH \
          -DFX_STANDALONE_ENABLE \
          -DLX_STANDALONE_ENABLE

ALL_CFLAGS = $(CFLAGS) $(INCLUDES) $(DEFINES)

# Unity sources
UNITY_SRCS = unity/unity.c

# Common test sources
SIM_DF_SRCS = sim_dataflash.c

# Core LevelX and FileX sources
LX_SRCS = $(wildcard ../libs/levelx/common/src/*.c)
FX_SRCS = $(wildcard ../libs/filex/common/src/*.c)

# Test 1: Simulated Data Flash driver & aggregation test
TEST1_BIN = test_dataflash_sim
TEST1_SRCS = test_dataflash_sim.c $(SIM_DF_SRCS) $(UNITY_SRCS)

# Test 2: LevelX NOR + FileX format reproduction & resolution test
TEST2_BIN = test_levelx_filex_format
TEST2_SRCS = test_levelx_filex_format.c $(SIM_DF_SRCS) $(UNITY_SRCS) $(LX_SRCS) $(FX_SRCS)

.PHONY: all clean test

all: $(TEST1_BIN) $(TEST2_BIN)

$(TEST1_BIN): $(TEST1_SRCS)
	$(CC) $(ALL_CFLAGS) $(TEST1_SRCS) -o $@

$(TEST2_BIN): $(TEST2_SRCS)
	$(CC) $(ALL_CFLAGS) $(TEST2_SRCS) -o $@

test: all
	@echo "=================================================="
	@echo " Running Test 1: Data Flash Simulator & Aggregation"
	@echo "=================================================="
	./$(TEST1_BIN)
	@echo ""
	@echo "=================================================="
	@echo " Running Test 2: LevelX NOR + FileX Format Verification"
	@echo "=================================================="
	./$(TEST2_BIN)

ifeq ($(OS),Windows_NT)
    RM_CMD = del /F /Q $(TEST1_BIN).exe $(TEST2_BIN).exe *.o 2>nul || exit 0
else
    RM_CMD = rm -f $(TEST1_BIN) $(TEST1_BIN).exe $(TEST2_BIN) $(TEST2_BIN).exe *.o
endif

clean:
	$(RM_CMD)
