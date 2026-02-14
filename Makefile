# Makefile

# Compiler and flags
CC = gcc
AS = as
CFLAGS = -Wall -Wextra -pthread
ASFLAGS =

# --- Main Shell Build ---
# DRIVERS_BAREMETAL=1 for bare-metal (port I/O, VGA). Omit for host (stdin/printf).
DRIVER_CFLAGS = $(CFLAGS)
DRIVER_SRCS = drivers/block_driver.c drivers/keyboard_driver.c drivers/display_driver.c \
              drivers/timer_driver.c drivers/pic_driver.c drivers/drivers.c
SRCS = common.c util.c terminal.c disk.c disk_asm.c dir_asm.c path_log.c cluster.c fs.c threadpool.c \
       priority_queue.c fs_provider.c fs_command.c fs_events.c fs_policy.c \
       fs_chain.c fs_facade.c fs_service_glue.c interpreter.c main.c
SRCS += $(DRIVER_SRCS)
ASMSRCS = mem_asm.s drivers/port_io.s
OBJS = $(SRCS:.c=.o) $(ASMSRCS:.s=.o)
TARGET = BPForbes_Flinstone_Shell

all: $(TARGET)

# Bare-metal: use port I/O and VGA (for kernel build, not userspace)
baremetal: CFLAGS += -DDRIVERS_BAREMETAL=1
baremetal: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJS) -Wl,-z,noexecstack

# --- Test Build ---
# For tests, interpreter.c is directly included in BPForbes_Flinstone_Tests.c.
TEST_SRCS = BPForbes_Flinstone_Tests.c common.c util.c terminal.c disk.c disk_asm.c dir_asm.c path_log.c cluster.c fs.c threadpool.c \
            priority_queue.c fs_provider.c fs_command.c fs_events.c fs_policy.c fs_chain.c fs_facade.c fs_service_glue.c
TEST_OBJS = $(TEST_SRCS:.c=.o)
TEST_ASMOBJS = mem_asm.o
TEST_TARGET = BPForbes_Flinstone_Tests

$(TEST_TARGET): $(TEST_OBJS) $(TEST_ASMOBJS)
	$(CC) $(CFLAGS) -DUNIT_TEST -o $(TEST_TARGET) $(TEST_OBJS) $(TEST_ASMOBJS) -Wl,-z,noexecstack -lcunit

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

%.o: %.s
	$(AS) $(ASFLAGS) -o $@ $<

drivers/%.o: drivers/%.c
	$(CC) $(CFLAGS) -I. -c $< -o $@

drivers/port_io.o: drivers/port_io.s
	$(AS) $(ASFLAGS) -o $@ $<

clean:
	rm -f $(OBJS) $(TEST_OBJS) $(TEST_ASMOBJS) mem_asm.o drivers/*.o $(TARGET) $(TEST_TARGET)
