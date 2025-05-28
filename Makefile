# Makefile

# Compiler and flags
CC = gcc
CFLAGS = -Wall -Wextra -pthread

# --- Main Shell Build ---
SRCS = common.c util.c terminal.c disk.c cluster.c fs.c threadpool.c interpreter.c main.c
OBJS = $(SRCS:.c=.o)
TARGET = BPForbes_Flinstone_Shell

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJS) -Wl,-z,noexecstack

# --- Test Build ---
# For tests, do NOT include interpreter.c since it is directly included in BPForbes_Flinstone_Tests.c.
TEST_SRCS = BPForbes_Flinstone_Tests.c common.c util.c terminal.c disk.c cluster.c fs.c threadpool.c
TEST_OBJS = $(TEST_SRCS:.c=.o)
TEST_TARGET = BPForbes_Flinstone_Tests

$(TEST_TARGET): $(TEST_OBJS)
	$(CC) $(CFLAGS) -DUNIT_TEST -o $(TEST_TARGET) $(TEST_OBJS) -Wl,-z,noexecstack -lcunit

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET) $(TEST_OBJS) $(TEST_TARGET)
