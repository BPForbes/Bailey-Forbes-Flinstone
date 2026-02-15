# Makefile

# Compiler and flags
CC = gcc
AS = as
CFLAGS = -Wall -Wextra -pthread
LDFLAGS = -Wl,-z,noexecstack
ASFLAGS =

# --- Main Shell Build ---
# DRIVERS_BAREMETAL=1 for bare-metal (port I/O, VGA). Omit for host (stdin/printf).
DRIVER_CFLAGS = $(CFLAGS)
DRIVER_SRCS = drivers/block_driver.c drivers/keyboard_driver.c drivers/display_driver.c \
              drivers/timer_driver.c drivers/pic_driver.c drivers/drivers.c
SRCS = common.c util.c terminal.c disk.c disk_asm.c dir_asm.c path_log.c cluster.c fs.c threadpool.c \
       priority_queue.c fs_provider.c fs_command.c fs_events.c fs_policy.c \
       fs_chain.c fs_facade.c fs_service_glue.c mem_domain.c vrt.c vfs.c interpreter.c main.c
SRCS += $(DRIVER_SRCS)
VM_SRCS = VM/vm.c VM/vm_cpu.c VM/vm_mem.c VM/vm_decode.c VM/vm_io.c VM/vm_loader.c VM/vm_display.c VM/vm_host.c VM/vm_font.c
ifeq ($(VM_ENABLE),1)
SRCS += $(VM_SRCS)
CFLAGS += -DVM_ENABLE=1 -I. -IVM
endif
VM_SDL_SRCS = VM/vm_sdl.c
DEPS_PREFIX = $(shell [ -d deps/install ] && echo deps/install)
ifneq ($(DEPS_PREFIX),)
CFLAGS += -I$(DEPS_PREFIX)/include
endif
ifeq ($(VM_SDL),1)
SRCS += $(VM_SDL_SRCS)
CFLAGS += -DVM_SDL=1
ifneq ($(DEPS_PREFIX),)
LDFLAGS += -L$(DEPS_PREFIX)/lib -lSDL2 -Wl,-rpath,'$$ORIGIN/deps/install/lib'
else
CFLAGS += $(shell pkg-config --cflags sdl2 2>/dev/null)
LDFLAGS += $(shell pkg-config --libs sdl2 2>/dev/null)
endif
endif
ASMSRCS = mem_asm.s drivers/port_io.s
# Set USE_ASM_ALLOC=1 to use thread-safe ASM malloc/calloc/free
# When enabled, batch mode runs single-threaded to avoid allocator/pthread issues
ifeq ($(USE_ASM_ALLOC),1)
ASMSRCS += alloc/alloc_core.s alloc/alloc_malloc.s alloc/alloc_free.s
CFLAGS += -DUSE_ASM_ALLOC=1 -DBATCH_SINGLE_THREAD=1
endif
OBJS = $(SRCS:.c=.o) $(ASMSRCS:.s=.o)
TARGET = BPForbes_Flinstone_Shell

all: $(TARGET)

# Bare-metal: use port I/O and VGA (for kernel build, not userspace)
baremetal: CFLAGS += -DDRIVERS_BAREMETAL=1
baremetal: $(TARGET)

# With embedded x86 VM: make vm && ./shell -Virtualization -y -vm
.PHONY: vm
vm:
	$(MAKE) VM_ENABLE=1 $(TARGET)

# VM with SDL2 window (WSLg-friendly popup): make vm-sdl
.PHONY: vm-sdl
vm-sdl:
	$(MAKE) VM_ENABLE=1 VM_SDL=1 $(TARGET)

# Fetch and build external libs (SDL2, CUnit) into deps/install.
.PHONY: deps deps-sdl2 deps-cunit
deps: deps-sdl2 deps-cunit

deps-sdl2:
	@chmod +x deps/fetch-sdl2.sh 2>/dev/null || true
	@./deps/fetch-sdl2.sh

deps-cunit:
	@chmod +x deps/fetch-cunit.sh 2>/dev/null || true
	@./deps/fetch-cunit.sh

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJS) $(LDFLAGS)

# --- Test Build ---
# For tests, interpreter.c is directly included in BPForbes_Flinstone_Tests.c.
TEST_SRCS = BPForbes_Flinstone_Tests.c common.c util.c terminal.c disk.c disk_asm.c dir_asm.c path_log.c cluster.c fs.c threadpool.c \
            priority_queue.c fs_provider.c fs_command.c fs_events.c fs_policy.c fs_chain.c fs_facade.c fs_service_glue.c mem_domain.c vrt.c
TEST_OBJS = $(TEST_SRCS:.c=.o)
TEST_ASMOBJS = mem_asm.o
TEST_TARGET = BPForbes_Flinstone_Tests

DEPS_RPATH = -Wl,-rpath='$$ORIGIN/deps/install/lib'
TEST_LDFLAGS = $(if $(DEPS_PREFIX),-L$(DEPS_PREFIX)/lib $(DEPS_RPATH),)
$(TEST_TARGET): $(TEST_OBJS) $(TEST_ASMOBJS)
	$(CC) $(CFLAGS) -DUNIT_TEST -o $(TEST_TARGET) $(TEST_OBJS) $(TEST_ASMOBJS) -Wl,-z,noexecstack \
		$(TEST_LDFLAGS) -lcunit

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

%.o: %.s
	$(AS) $(ASFLAGS) -o $@ $<

alloc/%.o: alloc/%.s
	$(AS) $(ASFLAGS) -o $@ $<

drivers/%.o: drivers/%.c
	$(CC) $(CFLAGS) -I. -c $< -o $@

VM/%.o: VM/%.c
	$(CC) $(CFLAGS) -I. -IVM -c $< -o $@

drivers/port_io.o: drivers/port_io.s
	$(AS) $(ASFLAGS) -o $@ $<

# --- ASM + Alloc + PQ unit tests (no CUnit) ---
# Use -fsanitize when NOT using ASM allocator (libc tests only)
TEST_SANITIZE = -fsanitize=address,undefined -fno-omit-frame-pointer
.PHONY: test_mem_asm test_alloc test_priority_queue test_core
test_mem_asm: mem_asm.o
	$(CC) $(CFLAGS) $(TEST_SANITIZE) -I. -o tests/test_mem_asm tests/test_mem_asm.c mem_asm.o
	./tests/test_mem_asm

test_alloc_libc: tests/test_alloc.c
	$(CC) $(CFLAGS) $(TEST_SANITIZE) -o tests/test_alloc tests/test_alloc.c
	./tests/test_alloc

test_alloc_asm: alloc/alloc_core.o alloc/alloc_malloc.o alloc/alloc_free.o
	$(CC) $(CFLAGS) -I. -o tests/test_alloc tests/test_alloc.c alloc/alloc_core.o alloc/alloc_malloc.o alloc/alloc_free.o
	./tests/test_alloc

test_priority_queue: priority_queue.o mem_asm.o
	$(CC) $(CFLAGS) $(TEST_SANITIZE) -I. -o tests/test_priority_queue tests/test_priority_queue.c priority_queue.o mem_asm.o
	./tests/test_priority_queue

test_core: test_mem_asm test_priority_queue
	@echo "Core tests done. Run 'make test_alloc_libc' or 'make test_alloc_asm' for allocator."

test_vm_mem: mem_domain.o mem_asm.o VM/vm_mem.o
	$(CC) $(CFLAGS) $(TEST_SANITIZE) -I. -IVM -o tests/test_vm_mem tests/test_vm_mem.c mem_domain.o mem_asm.o VM/vm_mem.o
	./tests/test_vm_mem

# Debug build: ASM contract asserts enabled
debug: CFLAGS += -DMEM_ASM_DEBUG -g
debug: $(TARGET)

clean:
	rm -f $(OBJS) $(TEST_OBJS) $(TEST_ASMOBJS) mem_asm.o drivers/*.o alloc/*.o VM/*.o $(TARGET) $(TEST_TARGET)
	rm -f tests/test_mem_asm tests/test_alloc tests/test_priority_queue tests/test_vm_mem
