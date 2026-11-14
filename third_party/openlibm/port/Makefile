REPO_ROOT := $(abspath ../../..)
OPENLIBM_ROOT := $(abspath ..)
SDK_DIR ?= $(REPO_ROOT)/build/liwus-sdk
OUT_DIR := $(REPO_ROOT)/build/openlibm
ARCHIVE := $(SDK_DIR)/lib/libopenlibm.a

override CUR_SRCS = liwus_openlibm_src_SRCS
include $(OPENLIBM_ROOT)/src/Make.files
override CUR_SRCS = liwus_openlibm_bsd_SRCS
include $(OPENLIBM_ROOT)/bsdsrc/Make.files

SOURCES := $(addprefix $(OPENLIBM_ROOT)/src/,$(liwus_openlibm_src_SRCS)) \
           $(addprefix $(OPENLIBM_ROOT)/bsdsrc/,$(liwus_openlibm_bsd_SRCS)) \
           $(OPENLIBM_ROOT)/amd64/fenv.c
OBJECTS := $(patsubst $(OPENLIBM_ROOT)/%.c,$(OUT_DIR)/%.o,$(SOURCES))
CFLAGS := -std=gnu99 -O2 -ffreestanding -fno-builtin -fno-pic -fno-pie \
          -fno-stack-protector -m64 -mno-red-zone \
          -D__BSD_VISIBLE \
          -I$(OPENLIBM_ROOT)/port/include -I$(SDK_DIR)/include \
          -I$(OPENLIBM_ROOT)/include -I$(OPENLIBM_ROOT)/src

.PHONY: all clean
all: $(ARCHIVE)

$(ARCHIVE): $(OBJECTS)
	@mkdir -p $(dir $@)
	$(AR) rcs $@ $^

$(OUT_DIR)/%.o: $(OPENLIBM_ROOT)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	find $(OUT_DIR) -type f -name '*.o' -delete
	@test ! -f $(ARCHIVE) || rm $(ARCHIVE)
