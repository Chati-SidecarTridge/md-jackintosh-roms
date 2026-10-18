# src/glue/Makefile: the glue of one image (EPIC-00 STORY-01). Called by
# tools/dev/build.sh, which passes ROM_BASE_ADDR_UL, BUILD_ID and BUILD_DIR.

DEBUG ?= 0
TEST ?= 0
BUILD_ID ?= unknown
BUILD_DIR ?= ../../build/glue
ROM_BASE_ADDR_LD := $(patsubst %UL,%,$(patsubst %ul,%,$(ROM_BASE_ADDR_UL)))

ifeq ($(filter clean,$(MAKECMDGOALS)),)
ifndef ROM_BASE_ADDR_UL
$(error ROM_BASE_ADDR_UL is required: 0x00FC0000UL (st) or 0x00E00000UL (ste))
endif
endif

ifneq ($(strip $(LIBS)$(LDLIBS)),)
$(error External libraries are forbidden: LIBS/LDLIBS must be empty)
endif

ifeq ($(DEBUG),1)
OPT_FLAGS := -O0 -g
DEBUG_DEFS := -D_DEBUG=1
else
OPT_FLAGS := -O2
DEBUG_DEFS :=
endif

ifeq ($(TEST),1)
TEST_DEFS := -D_TEST=1
else
TEST_DEFS :=
endif

CC := m68k-atari-mint-gcc
AS := m68k-atari-mint-gcc

APP_VERSION := $(strip $(shell tr -d '\r\n' < ../../version.txt | sed 's/^[vV]//'))
NO_EXT_LIB_FLAGS := -nostdlib -nodefaultlibs -nostartfiles

CFLAGS := -m68000 -std=gnu99 -ffreestanding -fomit-frame-pointer $(OPT_FLAGS) -Wall -Wextra -Werror $(NO_EXT_LIB_FLAGS) $(DEBUG_DEFS) $(TEST_DEFS) -DAPP_VERSION_STR=\"$(APP_VERSION)\" -DBUILD_ID_STR=\"$(BUILD_ID)\" -DROM_BASE_ADDR_UL=$(ROM_BASE_ADDR_UL) -MMD -MP
# The assembler gets the base without the UL suffix: it is used in address
# arithmetic there.
ASFLAGS := -m68000 -x assembler-with-cpp $(NO_EXT_LIB_FLAGS) $(DEBUG_DEFS) $(TEST_DEFS) -DROM_BASE_ADDR_UL=$(ROM_BASE_ADDR_LD) -MMD -MP

TARGET_BIN := $(BUILD_DIR)/GLUE.BIN
TARGET_MAP := $(BUILD_DIR)/GLUE.MAP
LDSCRIPT := $(BUILD_DIR)/rom.ld
BUILD_CONFIG_STAMP := $(BUILD_DIR)/.build_config
LDFLAGS := $(NO_EXT_LIB_FLAGS) -Wl,-Map=$(TARGET_MAP),--entry,_os_entry,-T,$(LDSCRIPT),--oformat,binary

# startup.s must stay first: the image starts with its reset header.
SRCS_S := startup.s lib.s shadows.s macglue.s
SRCS_C := main.c screen.c text.c mac.c
ifeq ($(DEBUG),1)
SRCS_S += natfeat.s
SRCS_C += trace.c
endif
OBJS := $(SRCS_S:%=$(BUILD_DIR)/%.o) $(SRCS_C:%=$(BUILD_DIR)/%.o)

all: $(TARGET_BIN)

.PHONY: all clean FORCE
FORCE:

# Rebuild everything when the configuration, the version or the build ID
# changes.
$(BUILD_CONFIG_STAMP): FORCE | $(BUILD_DIR)
	@printf '%s\n' "ROM_BASE_ADDR_UL=$(ROM_BASE_ADDR_UL) DEBUG=$(DEBUG) TEST=$(TEST) APP_VERSION=$(APP_VERSION) BUILD_ID=$(BUILD_ID)" > $@.tmp
	@if [ ! -f $@ ] || ! cmp -s $@.tmp $@; then mv $@.tmp $@; else rm -f $@.tmp; fi

$(BUILD_DIR)/%.c.o: %.c $(BUILD_CONFIG_STAMP) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.s.o: %.s $(BUILD_CONFIG_STAMP) | $(BUILD_DIR)
	$(AS) $(ASFLAGS) -c $< -o $@

# Generated through a .tmp file, so a failed preprocess never leaves a stale
# script behind.
$(LDSCRIPT): rom.ld.S mem.h Makefile $(BUILD_CONFIG_STAMP) | $(BUILD_DIR)
	$(CC) -E -P -x c -D__ASSEMBLER__=1 -DROM_BASE_ADDR_UL=$(ROM_BASE_ADDR_LD) $< -o $@.tmp
	mv $@.tmp $@

$(TARGET_BIN): $(OBJS) $(LDSCRIPT)
	$(CC) $(LDFLAGS) -o $@ $(OBJS)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

clean:
	rm -rf $(BUILD_DIR)

# Header dependencies written by the compiler (-MMD -MP). Last, so no rule
# from a .d file becomes the default goal.
-include $(OBJS:.o=.d)
