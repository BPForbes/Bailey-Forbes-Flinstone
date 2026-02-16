# Makefile
# ARCH: x86_64_gas (default), x86_64_nasm, arm
ARCH ?= x86_64_gas

# Compiler and flags
CC = gcc
AS = as
CFLAGS = -Wall -Wextra -pthread -I. -Ikernel/include -Ikernel/core/vfs -Ikernel/core/mm -Ikernel/core/sched -Ikernel/core/sys -Iuserland/shell -Ikernel/arch/x86_64 -Ikernel/arch/aarch64
LDFLAGS = -Wl,-z,noexecstack
ASFLAGS =

# --- Arch-specific assembly ---
ifeq ($(ARCH),x86_64_nasm)
AS = nasm
ASFLAGS = -f elf64
ASMSRCS_BASE = arch/x86_64/nasm/mem_asm.asm arch/x86_64/nasm/port_io.asm
ASMSRCS_ALLOC = arch/x86_64/nasm/alloc_core.asm arch/x86_64/nasm/alloc_malloc.asm arch/x86_64/nasm/alloc_free.asm
ASM_SRC_DIR = arch/x86_64/nasm
KERNEL_DRIVERS = kernel/arch/x86_64/drivers
else ifeq ($(ARCH),arm)
CC = aarch64-linux-gnu-gcc
AS = aarch64-linux-gnu-as
ASMSRCS_BASE = arch/arm/gas/mem_asm.s arch/arm/gas/port_io.s
ASMSRCS_ALLOC = arch/arm/gas/alloc_core.s arch/arm/gas/alloc_malloc.s arch/arm/gas/alloc_free.s
ASM_SRC_DIR = arch/arm/gas
KERNEL_DRIVERS = kernel/arch/aarch64/drivers
else
# x86_64_gas (default)
ASMSRCS_BASE = arch/x86_64/gas/mem_asm.s arch/x86_64/gas/port_io.s
ASMSRCS_ALLOC = arch/x86_64/gas/alloc/alloc_core.s arch/x86_64/gas/alloc/alloc_malloc.s arch/x86_64/gas/alloc/alloc_free.s
ASM_SRC_DIR = arch/x86_64/gas
KERNEL_DRIVERS = kernel/arch/x86_64/drivers
endif

# --- Main Shell Build ---
# DRIVERS_BAREMETAL=1 for bare-metal (port I/O, VGA). Omit for host (stdin/printf).
DRIVER_CFLAGS = $(CFLAGS)
UNIFIED_DRIVER_SRCS = kernel/drivers/block/block_driver.c kernel/drivers/block/block_transport_host.c \
                     kernel/drivers/keyboard_driver.c kernel/drivers/display_driver.c \
                     kernel/drivers/timer_driver.c kernel/drivers/pic_driver.c kernel/drivers/drivers.c
DRIVER_SRCS = $(UNIFIED_DRIVER_SRCS)
# PCI only in x86_64 (aarch64 has no pci.c)
ifneq ($(ARCH),arm)
DRIVER_SRCS += $(KERNEL_DRIVERS)/pci.c
endif
# HAL: ioport (x86 real, arm stubs)
HAL_SRCS = $(KERNEL_DRIVERS)/../hal/ioport.c
CORE_SRCS = kernel/core/vfs/disk.c kernel/core/vfs/path_log.c kernel/core/vfs/cluster.c kernel/core/vfs/fs.c \
            kernel/core/sched/threadpool.c priority_queue.c kernel/core/vfs/fs_provider.c kernel/core/vfs/fs_command.c \
            kernel/core/vfs/fs_events.c kernel/core/vfs/fs_policy.c kernel/core/vfs/fs_chain.c kernel/core/vfs/fs_facade.c \
            kernel/core/vfs/fs_service_glue.c kernel/core/mm/mem_domain.c kernel/core/sys/vrt.c kernel/core/vfs/vfs.c
SHELL_SRCS = userland/shell/common.c userland/shell/util.c userland/shell/terminal.c userland/shell/interpreter.c userland/shell/sh.c
SRCS = $(SHELL_SRCS) $(CORE_SRCS) disk_asm.c dir_asm.c
SRCS += $(DRIVER_SRCS) $(HAL_SRCS)
CFLAGS += -I$(ASM_SRC_DIR) -I$(KERNEL_DRIVERS) -Ikernel -Ikernel/drivers
VM_SRCS = VM/devices/vm.c VM/devices/vm_cpu.c VM/devices/vm_mem.c VM/devices/vm_decode.c VM/devices/vm_io.c VM/devices/vm_loader.c \
          VM/devices/vm_display.c VM/devices/vm_host.c VM/devices/vm_font.c VM/devices/vm_disk.c VM/devices/vm_snapshot.c
ifeq ($(VM_ENABLE),1)
SRCS += $(VM_SRCS)
CFLAGS += -DVM_ENABLE=1 -IVM -IVM/devices
endif
VM_SDL_SRCS = VM/devices/vm_sdl.c
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
TEST_SRCS = BPForbes_Flinstone_Tests.c userland/shell/common.c userland/shell/util.c userland/shell/terminal.c \
            kernel/core/vfs/disk.c kernel/core/vfs/path_log.c kernel/core/vfs/cluster.c kernel/core/vfs/fs.c \
            kernel/core/sched/threadpool.c priority_queue.c kernel/core/vfs/fs_provider.c kernel/core/vfs/fs_command.c \
            kernel/core/vfs/fs_events.c kernel/core/vfs/fs_policy.c kernel/core/vfs/fs_chain.c kernel/core/vfs/fs_facade.c \
            kernel/core/vfs/fs_service_glue.c kernel/core/mm/mem_domain.c kernel/core/sys/vrt.c
TEST_SRCS += disk_asm.c dir_asm.c
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

# Assembly file compilation
%.o: %.s
	$(AS) $(ASFLAGS) -o $@ $<

# Arch ASM: .s (GAS) or .asm (NASM)
%.o: %.s
	$(AS) $(ASFLAGS) -o $@ $<

%.o: %.asm
	$(AS) $(ASFLAGS) -o $@ $<

$(KERNEL_DRIVERS)/%.o: $(KERNEL_DRIVERS)/%.c
	$(CC) $(CFLAGS) -I$(KERNEL_DRIVERS) -c $< -o $@

kernel/drivers/%.o: kernel/drivers/%.c
	$(CC) $(CFLAGS) -c $< -o $@

kernel/drivers/block/%.o: kernel/drivers/block/%.c
	$(CC) $(CFLAGS) -c $< -o $@

kernel/arch/x86_64/hal/%.o: kernel/arch/x86_64/hal/%.c
	$(CC) $(CFLAGS) -Ikernel/arch/x86_64 -c $< -o $@

kernel/arch/aarch64/hal/%.o: kernel/arch/aarch64/hal/%.c
	$(CC) $(CFLAGS) -Ikernel/arch/aarch64 -c $< -o $@

VM/devices/%.o: VM/devices/%.c
	$(CC) $(CFLAGS) -IVM -IVM/devices -c $< -o $@

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

test_invariants: userland/shell/common.o userland/shell/util.o $(MEM_ASM_OBJ)
	$(CC) $(CFLAGS) $(TEST_SANITIZE) -o tests/test_invariants tests/test_invariants.c userland/shell/common.o userland/shell/util.o $(MEM_ASM_OBJ)
	./tests/test_invariants

check-layers:
	@./scripts/check_layers.sh

test_vm_mem: kernel/core/mm/mem_domain.o $(MEM_ASM_OBJ) VM/devices/vm_mem.o
	$(CC) $(CFLAGS) $(TEST_SANITIZE) -IVM -IVM/devices -o tests/test_vm_mem tests/test_vm_mem.c kernel/core/mm/mem_domain.o $(MEM_ASM_OBJ) VM/devices/vm_mem.o
	./tests/test_vm_mem

.PHONY: test_replay
test_replay:
	$(MAKE) VM_ENABLE=1 ARCH=$(ARCH) BPForbes_Flinstone_Shell
	$(CC) $(CFLAGS) -DVM_ENABLE=1 -I$(ASM_SRC_DIR) -I$(KERNEL_DRIVERS) -Ikernel -Ikernel/drivers -IVM -IVM/devices -o tests/test_replay tests/test_replay.c \
	  userland/shell/common.o userland/shell/util.o userland/shell/terminal.o kernel/core/vfs/disk.o disk_asm.o dir_asm.o \
	  kernel/core/vfs/path_log.o kernel/core/vfs/cluster.o kernel/core/vfs/fs.o priority_queue.o \
	  kernel/core/vfs/fs_provider.o kernel/core/vfs/fs_command.o kernel/core/vfs/fs_events.o kernel/core/vfs/fs_policy.o \
	  kernel/core/vfs/fs_chain.o kernel/core/vfs/fs_facade.o kernel/core/vfs/fs_service_glue.o kernel/core/mm/mem_domain.o kernel/core/sys/vrt.o kernel/core/vfs/vfs.o \
	  kernel/drivers/block/block_driver.o kernel/drivers/block/block_transport_host.o kernel/drivers/keyboard_driver.o kernel/drivers/display_driver.o \
	  kernel/drivers/timer_driver.o kernel/drivers/pic_driver.o kernel/drivers/drivers.o \
	  $(KERNEL_DRIVERS)/../hal/ioport.o \
	  $(if $(filter-out arm,$(ARCH)),$(KERNEL_DRIVERS)/pci.o,) \
	  VM/devices/vm.o VM/devices/vm_cpu.o VM/devices/vm_mem.o VM/devices/vm_decode.o VM/devices/vm_io.o VM/devices/vm_loader.o \
	  VM/devices/vm_display.o VM/devices/vm_host.o VM/devices/vm_font.o VM/devices/vm_disk.o VM/devices/vm_snapshot.o \
	  $(MEM_ASM_OBJ) $(PORT_IO_OBJ) -Wl,-z,noexecstack
	./tests/test_replay

# Debug build: ASM contract asserts enabled
debug: CFLAGS += -DMEM_ASM_DEBUG -g
debug: $(TARGET)

clean:
	rm -f $(OBJS) $(TEST_OBJS) $(TEST_ASMOBJS) $(TARGET) $(TEST_TARGET)
	rm -f kernel/arch/*/drivers/*.o kernel/arch/*/hal/*.o kernel/drivers/*.o kernel/drivers/block/*.o VM/devices/*.o
	rm -f arch/*/*/*.o arch/*/*/alloc/*.o
	rm -f tests/test_mem_asm tests/test_alloc tests/test_priority_queue tests/test_vm_mem tests/test_replay tests/test_invariants

# Architecture-specific build targets
.PHONY: arm x86-64-nasm x86_64_nasm parity
arm:
	$(MAKE) ARCH=arm

x86-64-nasm x86_64_nasm:
	$(MAKE) ARCH=x86_64_nasm

# Prove parity: all platforms must build the same driver set
parity:
	@echo "=== Building x86_64_gas ==="
	$(MAKE) clean && $(MAKE) ARCH=x86_64_gas
	@echo "=== Building x86_64_nasm ==="
	$(MAKE) clean && $(MAKE) ARCH=x86_64_nasm
	@echo "=== Building arm ==="
	$(MAKE) clean && $(MAKE) ARCH=arm
	@echo "=== Building VM (x86_64_gas) ==="
	$(MAKE) clean && $(MAKE) ARCH=x86_64_gas VM_ENABLE=1
	@echo "Parity: all platforms built successfully."
