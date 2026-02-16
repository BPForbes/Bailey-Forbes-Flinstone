# Makefile
# ARCH: x86_64_gas (default), x86_64_nasm, arm
ARCH ?= x86_64_gas

# Compiler and flags
CC = gcc
AS = as
CFLAGS = -Wall -Wextra -pthread
LDFLAGS = -Wl,-z,noexecstack
ASFLAGS =

# --- Arch-specific assembly ---
ifeq ($(ARCH),x86_64_nasm)
AS = nasm
ASFLAGS = -f elf64
ASMSRCS_BASE = arch/x86_64/nasm/mem_asm.asm arch/x86_64/nasm/port_io.asm
ASMSRCS_ALLOC = arch/x86_64/nasm/alloc_core.asm arch/x86_64/nasm/alloc_malloc.asm arch/x86_64/nasm/alloc_free.asm
else ifeq ($(ARCH),arm)
CC = aarch64-linux-gnu-gcc
AS = aarch64-linux-gnu-as
ASMSRCS_BASE = arch/arm/gas/mem_asm.s arch/arm/gas/port_io.s
ASMSRCS_ALLOC = arch/arm/gas/alloc_core.s arch/arm/gas/alloc_malloc.s arch/arm/gas/alloc_free.s
else
# x86_64_gas (default) - use arch layout for consistency
ASMSRCS_BASE = arch/x86_64/gas/mem_asm.s arch/x86_64/gas/port_io.s
ASMSRCS_ALLOC = arch/x86_64/gas/alloc/alloc_core.s arch/x86_64/gas/alloc/alloc_malloc.s arch/x86_64/gas/alloc/alloc_free.s
endif

# --- Main Shell Build ---
# DRIVERS_BAREMETAL=1 for bare-metal (port I/O, VGA). Omit for host (stdin/printf).
DRIVER_CFLAGS = $(CFLAGS)
DRIVER_SRCS = drivers/block_driver.c drivers/keyboard_driver.c drivers/display_driver.c \
              drivers/timer_driver.c drivers/pic_driver.c drivers/pci.c drivers/drivers.c
SRCS = common.c util.c terminal.c disk.c disk_asm.c dir_asm.c path_log.c cluster.c fs.c threadpool.c \
       priority_queue.c fs_provider.c fs_command.c fs_events.c fs_policy.c \
       fs_chain.c fs_facade.c fs_service_glue.c mem_domain.c vrt.c vfs.c interpreter.c main.c
SRCS += $(DRIVER_SRCS)
VM_SRCS = VM/vm.c VM/vm_cpu.c VM/vm_mem.c VM/vm_decode.c VM/vm_io.c VM/vm_loader.c VM/vm_display.c VM/vm_host.c VM/vm_font.c VM/vm_disk.c VM/vm_snapshot.c
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
ASMSRCS = $(ASMSRCS_BASE)
# Set USE_ASM_ALLOC=1 to use thread-safe ASM malloc/calloc/free
# When enabled, batch mode runs single-threaded to avoid allocator/pthread issues
ifeq ($(USE_ASM_ALLOC),1)
ASMSRCS += $(ASMSRCS_ALLOC)
CFLAGS += -DUSE_ASM_ALLOC=1 -DBATCH_SINGLE_THREAD=1
endif
# Object names: .s/.asm -> .o (strip arch path for .o in obj list)
ASMOBJS = $(patsubst %.s,%.o,$(patsubst %.asm,%.o,$(ASMSRCS)))
OBJS = $(SRCS:.c=.o) $(ASMOBJS)
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
MEM_ASM_OBJ = $(patsubst %.s,%.o,$(patsubst %.asm,%.o,$(firstword $(ASMSRCS_BASE))))
PORT_IO_OBJ = $(patsubst %.s,%.o,$(patsubst %.asm,%.o,$(word 2,$(ASMSRCS_BASE))))
TEST_ASMOBJS = $(MEM_ASM_OBJ)
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

# Arch ASM: .s (GAS) or .asm (NASM)
%.o: %.s
	$(AS) $(ASFLAGS) -o $@ $<

%.o: %.asm
	$(AS) $(ASFLAGS) -o $@ $<

drivers/%.o: drivers/%.c
	$(CC) $(CFLAGS) -I. -c $< -o $@

VM/%.o: VM/%.c
	$(CC) $(CFLAGS) -I. -IVM -c $< -o $@

# --- ASM + Alloc + PQ unit tests (no CUnit) ---
# Use -fsanitize when NOT using ASM allocator (libc tests only)
TEST_SANITIZE = -fsanitize=address,undefined -fno-omit-frame-pointer
.PHONY: test_mem_asm test_alloc test_priority_queue test_core test_invariants check-layers
test_mem_asm: $(MEM_ASM_OBJ)
	$(CC) $(CFLAGS) $(TEST_SANITIZE) -I. -o tests/test_mem_asm tests/test_mem_asm.c $(MEM_ASM_OBJ)
	./tests/test_mem_asm

test_alloc_libc: tests/test_alloc.c
	$(CC) $(CFLAGS) $(TEST_SANITIZE) -o tests/test_alloc tests/test_alloc.c
	./tests/test_alloc

ALLOC_OBJS = $(patsubst %.s,%.o,$(patsubst %.asm,%.o,$(ASMSRCS_ALLOC)))
test_alloc_asm: $(ALLOC_OBJS)
	$(CC) $(CFLAGS) -I. -o tests/test_alloc tests/test_alloc.c $(ALLOC_OBJS)
	./tests/test_alloc

test_priority_queue: priority_queue.o $(MEM_ASM_OBJ)
	$(CC) $(CFLAGS) $(TEST_SANITIZE) -I. -o tests/test_priority_queue tests/test_priority_queue.c priority_queue.o $(MEM_ASM_OBJ)
	./tests/test_priority_queue

test_core: test_mem_asm test_priority_queue
	@echo "Core tests done. Run 'make test_alloc_libc' or 'make test_alloc_asm' for allocator."

test_invariants: common.o util.o $(MEM_ASM_OBJ)
	$(CC) $(CFLAGS) $(TEST_SANITIZE) -I. -o tests/test_invariants tests/test_invariants.c common.o util.o $(MEM_ASM_OBJ)
	./tests/test_invariants

check-layers:
	@./scripts/check_layers.sh

test_vm_mem: mem_domain.o $(MEM_ASM_OBJ) VM/vm_mem.o
	$(CC) $(CFLAGS) $(TEST_SANITIZE) -I. -IVM -o tests/test_vm_mem tests/test_vm_mem.c mem_domain.o $(MEM_ASM_OBJ) VM/vm_mem.o
	./tests/test_vm_mem

.PHONY: test_replay
test_replay:
	$(MAKE) VM_ENABLE=1 BPForbes_Flinstone_Shell
	$(CC) $(CFLAGS) -DVM_ENABLE=1 -I. -IVM -o tests/test_replay tests/test_replay.c \
	  common.o util.o terminal.o disk.o disk_asm.o dir_asm.o path_log.o cluster.o fs.o priority_queue.o \
	  fs_provider.o fs_command.o fs_events.o fs_policy.o fs_chain.o fs_facade.o fs_service_glue.o mem_domain.o vrt.o vfs.o \
	  drivers/block_driver.o drivers/keyboard_driver.o drivers/display_driver.o drivers/timer_driver.o drivers/pic_driver.o drivers/drivers.o \
	  VM/vm.o VM/vm_cpu.o VM/vm_mem.o VM/vm_decode.o VM/vm_io.o VM/vm_loader.o VM/vm_display.o VM/vm_host.o VM/vm_font.o VM/vm_disk.o VM/vm_snapshot.o \
	  $(MEM_ASM_OBJ) $(PORT_IO_OBJ) -Wl,-z,noexecstack
	./tests/test_replay

# Debug build: ASM contract asserts enabled
debug: CFLAGS += -DMEM_ASM_DEBUG -g
debug: $(TARGET)

clean:
	rm -f $(OBJS) $(TEST_OBJS) $(TEST_ASMOBJS) drivers/*.o VM/*.o $(TARGET) $(TEST_TARGET)
	rm -f arch/*/*/*.o arch/*/*/alloc/*.o
	rm -f tests/test_mem_asm tests/test_alloc tests/test_priority_queue tests/test_vm_mem tests/test_replay tests/test_invariants
