export LC_ALL := C

SOURCES := gcc-wrapper.c util.c parse.c
OBJECTS := $(patsubst %.c,%.o,$(SOURCES))
HEADERS := common.h
PROGRAM := gcc-wrapper

CC := gcc
CFLAGS := -O0 -Wall -Wextra -g -fsanitize=undefined
RM := rm

.PHONY: all clean test

all: $(PROGRAM)

clean:
	$(RM) -f -v $(PROGRAM) $(OBJECTS)

test:
	$(MAKE) -C tests/ test

$(PROGRAM): $(OBJECTS)
	$(CC) $(CFLAGS) -o $@ $^

$(OBJECTS): %.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

$(SOURCES): $(HEADERS)

$(HEADERS):
