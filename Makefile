export LC_ALL := C

SOURCES := gcc-wrapper.c util.c
OBJECTS := $(patsubst %.c,%.o,$(SOURCES))
HEADERS := common.h
PROGRAM := gcc-wrapper

CC := gcc
CFLAGS := -O2 -Wall -Wextra
RM := rm

.PHONY: all clean test

all: $(PROGRAM)

clean:
	$(RM) -v -f $(PROGRAM) $(OBJECTS)

test:
	$(MAKE) -C tests/ test

$(PROGRAM): $(OBJECTS)
	$(CC) $(CFLAGS) -o $@ $^

$(OBJECTS): %.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

$(SOURCES): $(HEADERS)

$(HEADERS):
