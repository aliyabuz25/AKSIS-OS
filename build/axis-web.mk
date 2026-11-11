# Native AXIS build. Only pure C/math routines from musl are linked: no Linux
# startup code, syscall layer, dynamic loader, pthreads, or host JS library.
AW_ROOT := vendor/axis-web
AW_OUT := $(OUT)/axis-native
AW_MUSL := $(AW_ROOT)/musl
AW_HEADERS := $(AW_OUT)/include/bits/alltypes.h
AW_INC := -I$(AW_OUT)/include -I$(AW_MUSL)/arch/x86_64 -I$(AW_MUSL)/arch/generic -I$(AW_MUSL)/include
AW_FLAGS = -target x86_64-elf -std=c11 -O2 -ffreestanding -fno-builtin -fno-stack-protector -fno-pic -mno-red-zone -mcmodel=kernel -msse2 -nostdinc -ffunction-sections -fdata-sections -D_GNU_SOURCE $(AW_INC) -MMD -MP
AW_LEXBOR := $(filter-out %/core/fs.c %/core/perf.c,$(shell rg --files $(AW_ROOT)/lexbor/source/lexbor -g '*.c' -g '!ports/**'))
# Explicitly exclude other operating-system ports.
AW_LEXBOR := $(filter-out $(AW_ROOT)/lexbor/source/lexbor/ports/%,$(AW_LEXBOR)) $(AW_ROOT)/lexbor/source/lexbor/ports/posix/lexbor/core/memory.c
AW_QJS := $(addprefix $(AW_ROOT)/quickjs/,quickjs.c dtoa.c libregexp.c libunicode.c cutils.c)
AW_PURE := $(wildcard $(AW_MUSL)/src/math/*.c) $(addprefix $(AW_MUSL)/src/string/,memchr.c memrchr.c stpcpy.c strcpy.c strncpy.c strcat.c strncat.c strchr.c strchrnul.c strrchr.c strstr.c strncmp.c strspn.c strcspn.c strtok_r.c strdup.c strndup.c strlen.c memcpy.c memmove.c memset.c memcmp.c strnlen.c stpncpy.c) $(addprefix $(AW_MUSL)/src/,stdlib/qsort.c stdlib/qsort_nr.c stdlib/atoi.c ctype/isspace.c time/__secs_to_tm.c)
AW_SRCS := $(AW_LEXBOR) $(AW_QJS) $(AW_PURE)
AW_OBJS := $(patsubst $(AW_ROOT)/%.c,$(AW_OUT)/%.o,$(AW_SRCS))
AW_LOCAL := $(AW_OUT)/runtime.o $(AW_OUT)/port.o $(AW_OUT)/allocator.o
AW_LIB := $(AW_OUT)/libaxis-web.a
$(patsubst $(AW_ROOT)/%.c,$(AW_OUT)/%.o,$(AW_PURE)): AW_FLAGS += -include $(AW_MUSL)/src/include/features.h
$(patsubst $(AW_ROOT)/%.c,$(AW_OUT)/%.o,$(AW_PURE)): AW_INC := -I$(AW_MUSL)/src/include $(AW_INC)

$(AW_OUT)/bootstrap.h: kernel/axis_web/bootstrap.js tools/embed-axis-js.py
	python3 tools/embed-axis-js.py $< $@

$(AW_OUT)/runtime.o: $(AW_OUT)/bootstrap.h

$(AW_HEADERS): $(AW_MUSL)/tools/mkalltypes.sed $(AW_MUSL)/include/alltypes.h.in $(AW_MUSL)/arch/x86_64/bits/alltypes.h.in
	@mkdir -p $(@D)
	sed -f $< $(AW_MUSL)/arch/x86_64/bits/alltypes.h.in $(AW_MUSL)/include/alltypes.h.in > $@

$(AW_OUT)/%.o: $(AW_ROOT)/%.c $(AW_HEADERS)
	@mkdir -p $(@D)
	$(CC) $(AW_FLAGS) -DNDEBUG -DAXIS_FREESTANDING -DCONFIG_VERSION=\"2026-06-04\" -I$(AW_ROOT)/lexbor/source -I$(AW_MUSL)/src/internal -c $< -o $@

$(AW_OUT)/%.o: kernel/axis_web/%.c $(AW_HEADERS)
	@mkdir -p $(@D)
	$(CC) $(AW_FLAGS) -I$(AW_OUT) -I$(AW_ROOT)/lexbor/source -I$(AW_ROOT)/quickjs -c $< -o $@

$(AW_LIB): $(AW_OBJS) $(AW_LOCAL)
	x86_64-elf-ar rcs $@ $^

-include $(AW_OBJS:.o=.d) $(AW_LOCAL:.o=.d)
