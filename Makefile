# Makefile
# ARCH: x86_64_gas (default on typical desktops), x86_64_nasm, arm (AArch64).
# On Linux arm64 (e.g. Raspberry Pi OS 64-bit), default ARCH is arm so plain `make`
# does not feed x86-64 assembly to the AArch64 assembler (errors like unknown mnemonic `cli`).
UNAME_S := $(shell uname -s 2>/dev/null)
UNAME_M := $(shell uname -m 2>/dev/null)
ARCH ?= $(if $(and $(filter Linux,$(UNAME_S)),$(filter aarch64 arm64,$(UNAME_M))),arm,x86_64_gas)
ARM64_LINUX_HOST := $(and $(filter Linux,$(UNAME_S)),$(filter aarch64 arm64,$(UNAME_M)))

# Compiler and flags
CC = gcc
AS = as
CFLAGS = -Wall -Wextra -pthread -I. -Icontracts/foundations -Icontracts/runtime -Icontracts/identity -Icontracts/networking -Icontracts/drivers -Icontracts/storage -Icontracts/observability -Icontracts/operations -Icontracts/virtualization -Icontracts/hardening -Ikernel/include -Ikernel/core/vfs -Ikernel/core/mm -Ikernel/core/sched -Ikernel/core/sys -Iuserland/shell -Iuserland/command -Ikernel/arch/x86_64 -Ikernel/arch/aarch64
LDFLAGS = -Wl,-z,noexecstack
ASFLAGS =

# --- Arch-specific assembly ---
ifeq ($(ARCH),x86_64_nasm)
AS = nasm
ASFLAGS = -f elf64
CFLAGS += -DDISK_HOST_USE_LIBC_PREADV=1
ASMSRCS_BASE = arch/x86_64/nasm/mem_asm.asm arch/x86_64/nasm/port_io.asm arch/x86_64/nasm/usb_xhci_mmio_asm.asm
ASMSRCS_ALLOC = arch/x86_64/nasm/alloc_core.asm arch/x86_64/nasm/alloc_malloc.asm arch/x86_64/nasm/alloc_free.asm
ASM_SRC_DIR = arch/x86_64/nasm
KERNEL_DRIVERS = kernel/arch/x86_64/drivers
else ifeq ($(ARCH),arm)
ifeq ($(ARM64_LINUX_HOST),)
CC = aarch64-linux-gnu-gcc
AS = aarch64-linux-gnu-as
else
# Same triplet as aarch64-linux-gnu-*; avoids requiring the cross package on the device.
CC = gcc
AS = as
endif
ASMSRCS_BASE = arch/arm/gas/mem_asm.s arch/arm/gas/port_io.s arch/arm/gas/disk_host_io.s arch/arm/gas/shell_history_host_asm.s kernel/arch/aarch64/boot/spinlock.s kernel/arch/aarch64/drivers/ramdisk.s \
               kernel/arch/aarch64/drivers/usb_xhci_mmio_asm.s \
               kernel/arch/aarch64/boot/vectors.s
ASMSRCS_ALLOC = arch/arm/gas/alloc_core.s arch/arm/gas/alloc_malloc.s arch/arm/gas/alloc_free.s
ASM_SRC_DIR = arch/arm/gas
KERNEL_DRIVERS = kernel/arch/aarch64/drivers
else
# x86_64_gas (default)
ASMSRCS_BASE = arch/x86_64/gas/mem_asm.s arch/x86_64/gas/port_io.s arch/x86_64/gas/disk_host_io.s arch/x86_64/gas/shell_history_host_asm.s kernel/arch/x86_64/boot/spinlock.s kernel/arch/x86_64/drivers/ata_pio.s \
               kernel/arch/x86_64/drivers/usb_xhci_mmio_asm.s \
               kernel/arch/x86_64/boot/gdt.s kernel/arch/x86_64/boot/idt.s
ASMSRCS_ALLOC = arch/x86_64/gas/alloc/alloc_core.s arch/x86_64/gas/alloc/alloc_malloc.s arch/x86_64/gas/alloc/alloc_free.s
ASM_SRC_DIR = arch/x86_64/gas
KERNEL_DRIVERS = kernel/arch/x86_64/drivers
endif

# --- Main Shell Build ---
# DRIVERS_BAREMETAL=1 for bare-metal (port I/O, VGA). Omit for host (stdin/printf).
DRIVER_CFLAGS = $(CFLAGS)
UNIFIED_DRIVER_SRCS = kernel/drivers/bus.c kernel/drivers/driver_model.c \
                     kernel/drivers/block/block_driver.c kernel/drivers/block/block_transport_host.c kernel/drivers/block/block_transport_baremetal.c \
                     kernel/drivers/keyboard_driver.c kernel/drivers/display_driver.c \
                     kernel/drivers/timer_driver.c kernel/drivers/pic_driver.c kernel/drivers/drivers.c \
                     kernel/drivers/usb_xhci_mmio_glue.c \
                     kernel/drivers/p4_irq_lifecycle.c kernel/drivers/p4_pcie_lab.c \
                     kernel/drivers/p4_virtio.c kernel/drivers/p4_usb_xhci_lab.c \
                     kernel/drivers/p4_fdt_discovery.c kernel/drivers/p4_psci.c
DRIVER_SRCS = $(UNIFIED_DRIVER_SRCS)
# PCI: x86_64 real impl, aarch64 ECAM real
DRIVER_SRCS += $(KERNEL_DRIVERS)/pci.c
# x86: ATA IDENTIFY + helpers, IDT dispatcher
ifneq ($(ARCH),arm)
ifneq ($(ARCH),x86_64_nasm)
DRIVER_SRCS += $(KERNEL_DRIVERS)/ata_pio_baremetal.c
DRIVER_SRCS += kernel/arch/x86_64/boot/idt_dispatch.c
endif
endif
# HAL: ioport (x86 real, arm stubs) + ARM MMIO HAL (arm only)
HAL_SRCS = $(KERNEL_DRIVERS)/../hal/ioport.c
ifeq ($(ARCH),arm)
HAL_SRCS += kernel/arch/aarch64/hal/arm_plat.c kernel/arch/aarch64/hal/arm_uart.c \
            kernel/arch/aarch64/hal/arm_timer.c kernel/arch/aarch64/hal/arm_gic.c \
            kernel/arch/aarch64/boot/exc_dispatch.c
endif
CORE_SRCS = kernel/core/vfs/disk.c kernel/core/vfs/fat32_host.c kernel/core/vfs/fat32_host_files.c kernel/core/vfs/path_log.c kernel/core/vfs/cluster.c kernel/core/vfs/fs.c \
            disk_host_io.c \
            kernel/core/sched/threadpool.c priority_queue.c kernel/core/vfs/fs_jail.c kernel/core/vfs/fs_provider.c kernel/core/vfs/fs_command.c \
            kernel/core/vfs/fs_events.c kernel/core/vfs/fs_policy.c kernel/core/vfs/fs_chain.c kernel/core/vfs/fs_facade.c \
            kernel/core/vfs/fs_service_glue.c kernel/core/mm/mem_domain.c kernel/core/mm/kmalloc.c kernel/core/mm/pmm.c \
            kernel/core/sys/vrt.c kernel/core/sys/ipc.c kernel/core/sys/syscall.c kernel/core/vfs/vfs.c
COMMAND_SRCS := $(wildcard userland/command/cmd_*.c)
SHELL_SRCS = userland/shell/common.c userland/shell/util.c userland/shell/history_record.c userland/shell/audit_log.c userland/shell/authz_subsystem.c userland/shell/contract_log_dispatch.c userland/shell/terminal.c userland/shell/interpreter.c userland/shell/sh.c $(COMMAND_SRCS)
# GitHub Actions (or explicit opt-in) may generate userland/shell/version_changelog.c; see scripts/gen_version_changelog.c
ifeq ($(CHANGELOG_CI),1)
SHELL_SRCS += userland/shell/version_changelog.c
endif
SRCS = $(SHELL_SRCS) $(CORE_SRCS) disk_asm.c dir_asm.c
SRCS += $(DRIVER_SRCS) $(HAL_SRCS)
CFLAGS += -I$(ASM_SRC_DIR) -I$(KERNEL_DRIVERS) -Ikernel -Ikernel/drivers
ifeq ($(ARCH),arm)
CFLAGS += -Ikernel/arch/aarch64
endif
VM_SRCS = VM/devices/vm.c VM/devices/vm_cpu.c VM/devices/vm_mem.c VM/devices/vm_decode.c VM/devices/vm_io.c VM/devices/vm_loader.c \
          VM/devices/vm_display.c VM/devices/vm_host.c VM/devices/vm_font.c VM/devices/vm_disk.c VM/devices/vm_snapshot.c VM/devices/vm_arch.c
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
# P4-5 xHCI: arch MMIO object (see kernel/drivers/usb_xhci_mmio_glue.c).
USB_XHCI_MMIO_ASM_OBJ = $(patsubst %.s,%.o,$(patsubst %.asm,%.o,$(filter %usb_xhci_mmio_asm.s %usb_xhci_mmio_asm.asm,$(ASMSRCS))))
# Object names: .s/.asm -> .o (strip arch path for .o in obj list)
ASMOBJS = $(patsubst %.s,%.o,$(patsubst %.asm,%.o,$(ASMSRCS)))
OBJS = $(SRCS:.c=.o) $(ASMOBJS)
TARGET = BPForbes_Flinstone_Shell
.DEFAULT_GOAL := all

# version_def.h is generated from version/locked/**/*.ver (shipped A.B.C) plus
# a checksum of version/entries/**/*.ver for prerelease VERSION_LINE (paths may contain spaces).
VERSION_DEF := userland/shell/version_def.h
VER_LOCKED_FILES := $(shell find version/locked -name '*.ver' -print 2>/dev/null)
VERSION_ENTRIES_VER_SUM := .version_entries_ver.sum
$(VERSION_ENTRIES_VER_SUM): FORCE
	@find version/entries -name '*.ver' -print0 2>/dev/null | sort -z | xargs -0 md5sum 2>/dev/null | md5sum | awk '{print $$1}' >$(VERSION_ENTRIES_VER_SUM).tmp
	@if ! cmp -s $(VERSION_ENTRIES_VER_SUM).tmp $(VERSION_ENTRIES_VER_SUM) 2>/dev/null; then mv $(VERSION_ENTRIES_VER_SUM).tmp $(VERSION_ENTRIES_VER_SUM); else rm -f $(VERSION_ENTRIES_VER_SUM).tmp; fi

$(VERSION_DEF): scripts/gen_version_def.sh $(VER_LOCKED_FILES) $(VERSION_ENTRIES_VER_SUM)
	@./scripts/gen_version_def.sh

all: $(TARGET)

# Bare-metal: use port I/O and VGA (for kernel build, not userspace)
baremetal: CFLAGS += -DDRIVERS_BAREMETAL=1
baremetal: LDFLAGS += -no-pie
baremetal: $(TARGET)

# With embedded x86 VM: make vm && ./shell -Virtualization -y -vm
.PHONY: version-record gen-version-def FORCE
version-record: $(VERSION_DEF)
	@./scripts/export_version_record.sh

gen-version-def:
	@./scripts/gen_version_def.sh

# Edit version/entries/*.ver. To publish: make finalize-version-locked (copies entries → locked), then make (refreshes version_def.h).
.PHONY: finalize-version-locked sync-version-locked
finalize-version-locked sync-version-locked:
	@./scripts/finalize_version_locked.sh

# Merge GM=1 preproduction */ trees into one root GA .ver and delete those dirs from entries + locked.
.PHONY: promote-preproduction-for-main version-merge-sim-status bump-dev-version relocate-root-prerelease-ver
promote-preproduction-for-main:
	@./scripts/promote_preproduction_for_main.sh

# Report version/ state for develop→main merge sims (refs and/or working tree). See scripts/version_merge_sim_status.sh --help
version-merge-sim-status:
	@./scripts/version_merge_sim_status.sh $(ARGS)

# Usage: make bump-dev-version VER=version/entries/preproduction\ 4.0.0/foo.ver
bump-dev-version:
	@test -n "$(VER)" || (echo "usage: make bump-dev-version VER=path/to/file.ver" >&2; exit 1)
	@./scripts/bump_dev_version.sh "$(VER)"

# Stamp missing RELEASE_DATE on root PRERELEASE=1 *.ver, then move into preproduction A.B.C/ (see docs/versioning.md).
relocate-root-prerelease-ver:
	@./scripts/relocate_root_prerelease_ver_to_preproduction.sh

# Optional release build: changelog + CHANGELOG_CI=1 (version/locked is synced on merge to main/develop in CI; use finalize-version-locked locally if needed).
.PHONY: deploy
deploy:
	@gcc -std=c11 -Wall -Wextra -O2 -o gen_version_changelog scripts/gen_version_changelog.c && ./gen_version_changelog
	@$(MAKE) CHANGELOG_CI=1 all

.PHONY: vm baremetal
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

$(TARGET): $(VERSION_DEF) $(OBJS)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJS) $(LDFLAGS)

# Rebuild objects that embed VERSION when the generated header changes.
$(filter userland/shell/%.o userland/command/%.o,$(OBJS)): $(VERSION_DEF)

# --- Test Build ---
# interpreter.c is built as interpreter_unit.o with -DUNIT_TEST (stub interactive_shell).
# Shell builtins live in userland/command/*.c (same as main shell link).
TEST_SRCS = BPForbes_Flinstone_Tests.c userland/shell/common.c userland/shell/util.c userland/shell/history_record.c userland/shell/audit_log.c userland/shell/authz_subsystem.c userland/shell/contract_log_dispatch.c userland/shell/terminal.c \
            $(COMMAND_SRCS) \
            kernel/core/vfs/disk.c kernel/core/vfs/fat32_host.c kernel/core/vfs/fat32_host_files.c kernel/core/vfs/path_log.c kernel/core/vfs/cluster.c kernel/core/vfs/fs.c \
            disk_host_io.c \
            kernel/core/sched/threadpool.c priority_queue.c kernel/core/vfs/fs_jail.c kernel/core/vfs/fs_provider.c kernel/core/vfs/fs_command.c \
            kernel/core/vfs/fs_events.c kernel/core/vfs/fs_policy.c kernel/core/vfs/fs_chain.c kernel/core/vfs/fs_facade.c \
            kernel/core/vfs/fs_service_glue.c kernel/core/mm/mem_domain.c kernel/core/mm/kmalloc.c kernel/core/mm/pmm.c \
            kernel/core/sys/vrt.c kernel/core/sys/ipc.c kernel/core/sys/syscall.c
TEST_SRCS += disk_asm.c dir_asm.c
ifeq ($(CHANGELOG_CI),1)
TEST_SRCS += userland/shell/version_changelog.c
endif
TEST_UNIT_INTERPRETER_OBJ = userland/shell/interpreter_unit.o
TEST_OBJS = $(TEST_SRCS:.c=.o) $(TEST_UNIT_INTERPRETER_OBJ)

userland/shell/interpreter_unit.o: userland/shell/interpreter.c
	$(CC) $(CFLAGS) -DUNIT_TEST -c $< -o $@
MEM_ASM_OBJ = $(patsubst %.s,%.o,$(patsubst %.asm,%.o,$(firstword $(ASMSRCS_BASE))))
PORT_IO_OBJ = $(patsubst %.s,%.o,$(patsubst %.asm,%.o,$(word 2,$(ASMSRCS_BASE))))
DISK_HOST_ASM_OBJ = $(patsubst %.s,%.o,$(filter %/disk_host_io.s,$(ASMSRCS_BASE)))
HISTORY_ASM_OBJ = $(patsubst %.s,%.o,$(filter %/shell_history_host_asm.s,$(ASMSRCS_BASE)))
# util.c references host FAT32 helpers; any link of util.o outside the full shell must include these.
UTIL_HISTORY_HOST_OBJS = kernel/core/vfs/fat32_host.o kernel/core/vfs/fat32_host_files.o disk_host_io.o $(DISK_HOST_ASM_OBJ)
UTIL_SHELL_LINK_OBJS = userland/shell/util.o userland/shell/history_record.o
TEST_ASMOBJS = $(MEM_ASM_OBJ) $(PORT_IO_OBJ) $(DISK_HOST_ASM_OBJ) $(HISTORY_ASM_OBJ)
TEST_TARGET = BPForbes_Flinstone_Tests

DEPS_RPATH = -Wl,-rpath='$$ORIGIN/deps/install/lib'
TEST_LDFLAGS = $(if $(DEPS_PREFIX),-L$(DEPS_PREFIX)/lib $(DEPS_RPATH),)
$(TEST_TARGET): $(VERSION_DEF) $(TEST_OBJS) $(TEST_ASMOBJS)
	$(CC) $(CFLAGS) -DUNIT_TEST -o $(TEST_TARGET) $(TEST_OBJS) $(TEST_ASMOBJS) -Wl,-z,noexecstack \
		$(TEST_LDFLAGS) -lcunit

$(filter userland/shell/%.o userland/command/%.o,$(TEST_OBJS)): $(VERSION_DEF)
BPForbes_Flinstone_Tests.o: $(VERSION_DEF)
userland/shell/interpreter_unit.o: $(VERSION_DEF)

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
.PHONY: test_mem_asm test_alloc test_priority_queue test_drivers test_core test_invariants test_audit_log test_vm_layer_warning check-layers
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

# Stub gate runs before driver tests (CI / make test_drivers)
test_drivers: check-stubs
TEST_DRIVER_HAL_OBJS = $(KERNEL_DRIVERS)/../hal/ioport.o
ifeq ($(ARCH),arm)
TEST_DRIVER_HAL_OBJS += kernel/arch/aarch64/hal/arm_plat.o kernel/arch/aarch64/hal/arm_uart.o \
	kernel/arch/aarch64/hal/arm_timer.o kernel/arch/aarch64/hal/arm_gic.o
endif
test_drivers: userland/shell/common.o $(UTIL_SHELL_LINK_OBJS) kernel/core/vfs/disk.o kernel/core/vfs/fat32_host.o kernel/core/vfs/fat32_host_files.o disk_host_io.o disk_asm.o kernel/core/mm/mem_domain.o kernel/core/mm/kmalloc.o $(MEM_ASM_OBJ) $(PORT_IO_OBJ) $(DISK_HOST_ASM_OBJ) $(HISTORY_ASM_OBJ) \
	  kernel/drivers/bus.o kernel/drivers/driver_model.o \
	  kernel/drivers/block/block_driver.o kernel/drivers/block/block_transport_host.o \
	  kernel/drivers/keyboard_driver.o kernel/drivers/display_driver.o kernel/drivers/timer_driver.o kernel/drivers/pic_driver.o kernel/drivers/drivers.o \
	  kernel/drivers/usb_xhci_mmio_glue.o kernel/drivers/p4_irq_lifecycle.o \
	  kernel/drivers/p4_pcie_lab.o kernel/drivers/p4_virtio.o \
	  kernel/drivers/p4_usb_xhci_lab.o kernel/drivers/p4_fdt_discovery.o \
	  kernel/drivers/p4_psci.o $(USB_XHCI_MMIO_ASM_OBJ) \
	  $(KERNEL_DRIVERS)/pci.o $(TEST_DRIVER_HAL_OBJS)
	$(CC) $(CFLAGS) $(TEST_SANITIZE) -I. -Ikernel -Ikernel/include -Ikernel/drivers -Iuserland/shell -I$(ASM_SRC_DIR) -I$(KERNEL_DRIVERS) -Ikernel/arch/aarch64 -o tests/test_drivers tests/test_drivers.c \
	  userland/shell/common.o $(UTIL_SHELL_LINK_OBJS) kernel/core/vfs/disk.o kernel/core/vfs/fat32_host.o kernel/core/vfs/fat32_host_files.o disk_host_io.o disk_asm.o kernel/core/mm/mem_domain.o kernel/core/mm/kmalloc.o $(MEM_ASM_OBJ) $(PORT_IO_OBJ) $(DISK_HOST_ASM_OBJ) $(HISTORY_ASM_OBJ) \
	  kernel/drivers/bus.o kernel/drivers/driver_model.o \
	  kernel/drivers/block/block_driver.o kernel/drivers/block/block_transport_host.o \
	  kernel/drivers/keyboard_driver.o kernel/drivers/display_driver.o kernel/drivers/timer_driver.o kernel/drivers/pic_driver.o kernel/drivers/drivers.o \
	  kernel/drivers/usb_xhci_mmio_glue.o kernel/drivers/p4_irq_lifecycle.o \
	  kernel/drivers/p4_pcie_lab.o kernel/drivers/p4_virtio.o \
	  kernel/drivers/p4_usb_xhci_lab.o kernel/drivers/p4_fdt_discovery.o \
	  kernel/drivers/p4_psci.o $(USB_XHCI_MMIO_ASM_OBJ) \
	  $(KERNEL_DRIVERS)/pci.o $(TEST_DRIVER_HAL_OBJS) -Wl,-z,noexecstack
	./tests/test_drivers

test_core: test_mem_asm test_priority_queue
	@echo "Core tests done. Run 'make test_alloc_libc' or 'make test_alloc_asm' for allocator."


.PHONY: test_userspace_connection
test_userspace_connection: kernel/core/sys/vrt.o kernel/core/sys/ipc.o kernel/core/sys/syscall.o $(MEM_ASM_OBJ)
	$(CC) $(CFLAGS) $(TEST_SANITIZE) -I. -Ikernel -Ikernel/include -Ikernel/core/sys -o tests/test_userspace_connection tests/test_userspace_connection.c \
	  kernel/core/sys/vrt.o kernel/core/sys/ipc.o kernel/core/sys/syscall.o $(MEM_ASM_OBJ) -Wl,-z,noexecstack
	./tests/test_userspace_connection

# Invariant tests (property + contract headers). Uses $(CFLAGS), which already
# includes -Icontracts/virtualization and -Icontracts/hardening for P8/P9 shards.
test_invariants: userland/shell/common.o userland/shell/authz_subsystem.o $(UTIL_SHELL_LINK_OBJS) $(MEM_ASM_OBJ) $(HISTORY_ASM_OBJ) $(UTIL_HISTORY_HOST_OBJS)
	$(CC) $(CFLAGS) $(TEST_SANITIZE) -o tests/test_invariants tests/test_invariants.c userland/shell/common.o userland/shell/authz_subsystem.o $(UTIL_SHELL_LINK_OBJS) $(MEM_ASM_OBJ) $(HISTORY_ASM_OBJ) $(UTIL_HISTORY_HOST_OBJS)
	./tests/test_invariants

# audit_log unit tests (standalone, no CUnit required)
.PHONY: test_audit_log
test_audit_log: userland/shell/common.o userland/shell/audit_log.o userland/shell/contract_log_dispatch.o $(UTIL_SHELL_LINK_OBJS) kernel/core/vfs/fs_jail.o kernel/core/mm/mem_domain.o $(MEM_ASM_OBJ) $(HISTORY_ASM_OBJ) $(UTIL_HISTORY_HOST_OBJS)
	$(CC) $(CFLAGS) $(TEST_SANITIZE) -o tests/test_audit_log tests/test_audit_log.c \
	  userland/shell/common.o userland/shell/audit_log.o userland/shell/contract_log_dispatch.o $(UTIL_SHELL_LINK_OBJS) kernel/core/vfs/fs_jail.o \
	  kernel/core/mm/mem_domain.o $(MEM_ASM_OBJ) $(HISTORY_ASM_OBJ) $(UTIL_HISTORY_HOST_OBJS) -Wl,-z,noexecstack
	./tests/test_audit_log

# fs_jail unit tests (standalone, no CUnit required)
.PHONY: test_fs_jail
test_fs_jail: userland/shell/common.o $(UTIL_SHELL_LINK_OBJS) kernel/core/vfs/fs_jail.o kernel/core/mm/mem_domain.o $(MEM_ASM_OBJ) $(HISTORY_ASM_OBJ) $(UTIL_HISTORY_HOST_OBJS)
	$(CC) $(CFLAGS) $(TEST_SANITIZE) -o tests/test_fs_jail tests/test_fs_jail.c \
	  userland/shell/common.o $(UTIL_SHELL_LINK_OBJS) kernel/core/vfs/fs_jail.o \
	  kernel/core/mm/mem_domain.o $(MEM_ASM_OBJ) $(HISTORY_ASM_OBJ) $(UTIL_HISTORY_HOST_OBJS) -Wl,-z,noexecstack
	./tests/test_fs_jail

check-layers:
	@./scripts/check_layers.sh

.PHONY: check-stubs
check-stubs:
	@bash scripts/check_no_stubs.sh

test_vm_mem: kernel/core/mm/mem_domain.o $(MEM_ASM_OBJ) VM/devices/vm_mem.o
	$(CC) $(CFLAGS) $(TEST_SANITIZE) -IVM -IVM/devices -o tests/test_vm_mem tests/test_vm_mem.c kernel/core/mm/mem_domain.o $(MEM_ASM_OBJ) VM/devices/vm_mem.o
	./tests/test_vm_mem

test_vm_syscall_bridge: kernel/core/mm/mem_domain.o kernel/core/sys/vrt.o kernel/core/sys/ipc.o kernel/core/sys/syscall.o VM/devices/vm_io.o $(MEM_ASM_OBJ)
	$(CC) $(CFLAGS) $(TEST_SANITIZE) -I. -Ikernel -Ikernel/include -IVM -IVM/devices -o tests/test_vm_syscall_bridge tests/test_vm_syscall_bridge.c \
	  kernel/core/mm/mem_domain.o kernel/core/sys/vrt.o kernel/core/sys/ipc.o kernel/core/sys/syscall.o VM/devices/vm_io.o $(MEM_ASM_OBJ) -Wl,-z,noexecstack
	./tests/test_vm_syscall_bridge

test_vm_arch_readiness: kernel/core/mm/mem_domain.o kernel/core/sys/vrt.o kernel/core/sys/ipc.o kernel/core/sys/syscall.o VM/devices/vm_io.o VM/devices/vm_arch.o $(MEM_ASM_OBJ)
	$(CC) $(CFLAGS) $(TEST_SANITIZE) -I. -Ikernel -Ikernel/include -IVM -IVM/devices -o tests/test_vm_arch_readiness tests/test_vm_arch_readiness.c \
	  kernel/core/mm/mem_domain.o kernel/core/sys/vrt.o kernel/core/sys/ipc.o kernel/core/sys/syscall.o VM/devices/vm_io.o VM/devices/vm_arch.o $(MEM_ASM_OBJ) -Wl,-z,noexecstack
	./tests/test_vm_arch_readiness

test_vm_layer_warning: userland/shell/common.o kernel/core/vfs/fs_jail.o kernel/core/vfs/path_log.o kernel/core/mm/mem_domain.o $(MEM_ASM_OBJ)
	$(CC) $(CFLAGS) $(TEST_SANITIZE) -I. -Ikernel/core/vfs -Ikernel/core/mm -Iuserland/shell -o tests/test_vm_layer_warning tests/test_vm_layer_warning.c \
	  userland/shell/common.o kernel/core/vfs/fs_jail.o kernel/core/vfs/path_log.o kernel/core/mm/mem_domain.o $(MEM_ASM_OBJ) -Wl,-z,noexecstack
	./tests/test_vm_layer_warning

.PHONY: test_replay
test_replay:
	$(MAKE) clean
	$(MAKE) VM_ENABLE=1 ARCH=$(ARCH) BPForbes_Flinstone_Shell
	$(CC) $(CFLAGS) -DVM_ENABLE=1 -I$(ASM_SRC_DIR) -I$(KERNEL_DRIVERS) -Ikernel -Ikernel/drivers -IVM -IVM/devices -o tests/test_replay tests/test_replay.c \
	  userland/shell/common.o $(UTIL_SHELL_LINK_OBJS) userland/shell/terminal.o kernel/core/vfs/disk.o kernel/core/vfs/fat32_host.o kernel/core/vfs/fat32_host_files.o disk_host_io.o disk_asm.o dir_asm.o \
	  kernel/core/vfs/path_log.o kernel/core/vfs/cluster.o kernel/core/vfs/fs.o priority_queue.o \
	  kernel/core/vfs/fs_provider.o kernel/core/vfs/fs_command.o kernel/core/vfs/fs_events.o kernel/core/vfs/fs_policy.o \
	  kernel/core/vfs/fs_chain.o kernel/core/vfs/fs_facade.o kernel/core/vfs/fs_service_glue.o kernel/core/vfs/fs_jail.o kernel/core/mm/mem_domain.o kernel/core/mm/kmalloc.o \
	  kernel/core/sys/vrt.o kernel/core/sys/ipc.o kernel/core/sys/syscall.o kernel/core/vfs/vfs.o \
	  kernel/drivers/bus.o kernel/drivers/driver_model.o \
	  kernel/drivers/block/block_driver.o kernel/drivers/block/block_transport_host.o kernel/drivers/keyboard_driver.o kernel/drivers/display_driver.o \
	  kernel/drivers/timer_driver.o kernel/drivers/pic_driver.o kernel/drivers/drivers.o \
	  $(KERNEL_DRIVERS)/../hal/ioport.o \
	  $(KERNEL_DRIVERS)/pci.o \
	  VM/devices/vm.o VM/devices/vm_cpu.o VM/devices/vm_mem.o VM/devices/vm_decode.o VM/devices/vm_io.o VM/devices/vm_loader.o \
		  VM/devices/vm_display.o VM/devices/vm_host.o VM/devices/vm_font.o VM/devices/vm_disk.o VM/devices/vm_snapshot.o \
		  VM/devices/vm_arch.o \
		  $(MEM_ASM_OBJ) $(PORT_IO_OBJ) $(HISTORY_ASM_OBJ) -Wl,-z,noexecstack
	./tests/test_replay

# Debug build: ASM contract asserts enabled
debug: CFLAGS += -DMEM_ASM_DEBUG -g
debug: $(TARGET)

clean:
	rm -f $(OBJS) $(TEST_OBJS) $(TEST_ASMOBJS) $(TARGET) $(TEST_TARGET)
	rm -f $(VERSION_ENTRIES_VER_SUM)
	rm -f kernel/arch/*/drivers/*.o kernel/arch/*/hal/*.o kernel/drivers/*.o kernel/drivers/block/*.o VM/devices/*.o
	rm -f arch/*/*/*.o arch/*/*/alloc/*.o
	rm -f tests/test_mem_asm tests/test_alloc tests/test_priority_queue tests/test_drivers tests/test_vm_mem tests/test_replay tests/test_invariants tests/test_userspace_connection tests/test_vm_syscall_bridge tests/test_vm_arch_readiness

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

# Recompute version/entries checksum (paths may contain spaces; not used as make prerequisites).
FORCE:
