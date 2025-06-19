CC = clang
CFLAGS = -Wall -Wextra -Werror -pedantic
TARGET = memory
SRCS = memory.c
OBJS = $(SRCS:.c=.o)


.PHONY: all clean format check-format

# Default target
all: format $(TARGET)

# Rule to produce the memory binary
$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^

# Rule to produce object files
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Rule to clean up build files
clean:
	rm -f $(OBJS) $(TARGET)

# Rule to format all .c and .h files using clang-format
format:
	clang-format -i $(SRCS)

# Rule to check if all files are properly formatted
check-format:
	clang-format --dry-run --Werror $(SRCS)


