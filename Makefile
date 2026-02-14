# Makefile

# Compiler and flags
CC = gcc
AS = as
CFLAGS = -Wall -Wextra -pthread
ASFLAGS =

# --- Main Shell Build ---
SRCS = common.c util.c terminal.c disk.c disk_asm.c cluster.c fs.c threadpool.c \
       priority_queue.c fs_provider.c fs_command.c fs_events.c fs_policy.c \
       fs_chain.c fs_facade.c fs_service_glue.c interpreter.c main.c
ASMSRCS = mem_asm.s
OBJS = $(SRCS:.c=.o) $(ASMSRCS:.s=.o)
TARGET = BPForbes_Flinstone_Shell

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJS) -Wl,-z,noexecstack

# --- Test Build ---
# For tests, interpreter.c is directly included in BPForbes_Flinstone_Tests.c.
TEST_SRCS = BPForbes_Flinstone_Tests.c common.c util.c terminal.c disk.c disk_asm.c cluster.c fs.c threadpool.c \
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

clean:
	rm -f $(OBJS) $(TEST_OBJS) $(TEST_ASMOBJS) mem_asm.o $(TARGET) $(TEST_TARGET)
