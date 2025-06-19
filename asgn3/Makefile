CC = clang
CFLAGS = -Wall -Wextra -Werror -pedantic

# Object files
OBJS = queue.o rwlock.o

all: $(OBJS)

queue.o: queue.c queue.h
	$(CC) $(CFLAGS) -c queue.c -o queue.o

rwlock.o: rwlock.c rwlock.h
	$(CC) $(CFLAGS) -c rwlock.c -o rwlock.o

clean:
	rm -f $(OBJS)

format:
	clang-format -i *.c *.h


