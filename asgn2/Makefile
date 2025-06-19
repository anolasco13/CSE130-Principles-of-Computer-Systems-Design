EXECBIN = httpserver
SOURCES = $(wildcard *.c)
OBJECTS = $(SOURCES:%.c=%.o)
FORMATS = $(SOURCES:%.c=%.fmt)
LIBRARIES = asgn2_helper_funcs.a 

CC = clang
FORMAT = clang-format 
CFLAGS = -Wall -Wextra -Werror -Wpedantic -Wstrict-prototypes
CPPFLAGS = -DDEBUG

.PHONY: all clean format

all: $(EXECBIN)

$(EXECBIN): $(OBJECTS) $(LIBRARIES)
	$(CC) -o $@ $^

%.o: %.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(EXECBIN) $(OBJECTS)

format: $(FORMATS)

%.fmt: %.c
	$(FORMAT) -i $<
	touch $@



