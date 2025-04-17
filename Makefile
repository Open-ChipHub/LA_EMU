TARGET_CPU ?= loongarch64

ifeq ($(TARGET_CPU),loongarch64)
TARGET_ABBR := la
TARGET_MACRO := -DTARGET_LOONGARCH64
else
$(error Unsupported TARGET_CPU $(TARGET_CPU))
endif

BUILD_DIR := build

CC=gcc
OPT_FLAG = -O2 -flto=auto
ifeq (${DEBUG},1)
	OPT_FLAG = -Og
endif
# CFLAGS ?= -g -O3 -flto=auto -march=native -mtune=native -MMD -MP -I. -Iinclude -DCONFIG_INT128
CFLAGS ?= -g ${OPT_FLAG} -MMD -MP -I. -Iinclude -I${TARGET_CPU} -Idevice -I$(BUILD_DIR) -Wall -Werror $(TARGET_MACRO)
LDFLAGS ?= -lm -lrt -rdynamic ${OPT_FLAG}
ifeq (${GDB},1)
	CFLAGS += -DCONFIG_GDB
	GDB_SOURCES := gdbserver.c
endif

ifeq (${CLI},1)
	CFLAGS += -DCONFIG_CLI
endif

ifeq (${DIFF},1)
	CFLAGS += -DCONFIG_DIFF -fPIC
endif

ifeq (${COSIM},1)
	CFLAGS += -DCONFIG_DIFF -DCONFIG_COSIM -fPIC
endif

ifeq (${PERF},1)
	CFLAGS += -DCONFIG_PERF
endif

ifeq (${CORE},)
	CFLAGS += -D__CORE__=la464
else
	CFLAGS += -D__CORE__=$(CORE)
endif

ifeq (${PLUGIN},1)
	LDFLAGS += -ldl
	CFLAGS += -DCONFIG_PLUGIN
endif

arch := $(shell gcc -dumpmachine)
ifeq ($(arch),loongarch64-linux-gnu)
   LDFLAGS+=-Wl,-Tlink_script/loongarch64.lds
endif
ifeq ($(filter 1,$(DIFF) $(COSIM)),1)
	LDFLAGS += -shared -fPIC -Wl,--no-undefined
endif

SRC_DIRS := ./

TARGET_COMMON_SOURCE := $(addprefix ${TARGET_CPU}/, cpu.c fpu_helper.c interpreter.c vec_helper.c lbt_helper.c)
USER_KERNEL_COMMON_SOURCES := ${TARGET_COMMON_SOURCE} ${GDB_SOURCES} host-utils.c  int128.c main.c softfloat.c tcg-runtime-gvec.c checkpoint.c

ifeq (${CLI},1)
	USER_KERNEL_COMMON_SOURCES += debug_cli.c
endif

USER_SOURCES := ${USER_KERNEL_COMMON_SOURCES} syscall.c
USER_OBJS := $(addprefix $(BUILD_DIR)/, $(patsubst %.c,%_user.o,$(USER_SOURCES)))
USER_DEPS := $(USER_OBJS:.o=.d)

DEVICE_SOURCES := $(wildcard device/*.c)

KERNEL_SOURCES := ${USER_KERNEL_COMMON_SOURCES} ${DEVICE_SOURCES} ${TARGET_CPU}/tlb_helper.c ${TARGET_CPU}/cpu_helper.c  fifo.c
KERNEL_OBJS := $(addprefix $(BUILD_DIR)/, $(patsubst %.c,%_kernel.o,$(KERNEL_SOURCES)))
KERNEL_DEPS := $(KERNEL_OBJS:.o=.d)

DIFF_SOURCES := $(KERNEL_SOURCES) difftest.c
DIFF_OBJS := $(addprefix $(BUILD_DIR)/, $(patsubst %.c,%_diff.o,$(DIFF_SOURCES)))
DIFF_DEPS := $(DIFF_OBJS:.o=.d)

$(info $$USER_SOURCES is [${USER_SOURCES}])
$(info $$USER_OBJS is [${USER_OBJS}])
$(info $$USER_DEPS is [${USER_DEPS}])

$(info $$KERNEL_SOURCES is [${KERNEL_SOURCES}])
$(info $$KERNEL_OBJS is [${KERNEL_OBJS}])
$(info $$KERNEL_DEPS is [${KERNEL_DEPS}])

$(info $$DIFF_SOURCES is [${DIFF_SOURCES}])
$(info $$DIFF_OBJS is [${DIFF_OBJS}])
$(info $$DIFF_DEPS is [${DIFF_DEPS}])

ifeq ($(filter 1,$(DIFF) $(COSIM)),1)
	TARGETS = $(BUILD_DIR)/$(TARGET_ABBR)_emu_ref.so
else
	TARGETS = $(BUILD_DIR)/$(TARGET_ABBR)_emu_user $(BUILD_DIR)/$(TARGET_ABBR)_emu_kernel
endif

all: $(TARGETS)
	make -C plugins -j

${USER_OBJS} ${KERNEL_OBJS} ${DIFF_OBJS} : $(BUILD_DIR)/trans_la.c.inc

$(BUILD_DIR)/trans_la.c.inc: ${TARGET_CPU}/insns.decode
	@mkdir -p $(BUILD_DIR)
	python3 ./scripts/decodetree.py ${TARGET_CPU}/insns.decode -o $(BUILD_DIR)/decode-insns.c.inc
	python3 ./scripts/emu_cpu_put_ic.py $(BUILD_DIR)/decode-insns.c.inc > $(BUILD_DIR)/trans_la.c.inc

$(BUILD_DIR)/$(TARGET_ABBR)_emu_user : ${USER_OBJS}
	$(CC) $(USER_OBJS) -o $@ $(LDFLAGS)

$(BUILD_DIR)/%_user.o : %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -DCONFIG_USER_ONLY=1 -c -o $@ $<

$(BUILD_DIR)/$(TARGET_ABBR)_emu_kernel : ${KERNEL_OBJS}
	$(CC) $(KERNEL_OBJS) -o $@ $(LDFLAGS)

$(BUILD_DIR)/%_kernel.o : %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/$(TARGET_ABBR)_emu_ref.so : ${DIFF_OBJS}
	$(CC) $(DIFF_OBJS) -o $@ $(LDFLAGS)

$(BUILD_DIR)/%_diff.o : %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -rf $(BUILD_DIR) $(BUILD_DIR)/trans_la.c.inc

.EXTRA_PREREQS = Makefile
-include $(USER_DEPS)
-include $(KERNEL_DEPS)
