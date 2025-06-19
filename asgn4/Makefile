EXECBIN = httpserver
SOURCES = $(wildcard *.c)
HEADERS = $(wildcard *.h)
OBJECTS = $(SOURCES:%.c=%.o)
FORMATS = $(SOURCES:%.c=%.fmt)
LIBRARIES = asgn4_helper_funcs.a

CC = clang
FORMAT = clang-format
CFLAGS = -Wall -Werror -Wextra -Wpedantic -Wstrict-prototypes

.PHONY: all clean format

all: $(EXECBIN)

$(EXECBIN): $(OBJECTS) $(LIBRARIES)
	$(CC) -o $@ $^

%.o: %.c $(HEADERS)
	$(CC) $(CFLAGS) -c $<

clean: 
	rm -f $(EXECBIN) $(OBJECTS)

