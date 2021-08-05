SOURCES := gcc-wrapper.c util.c
OBJECTS := $(patsubst %.c,%.o,$(SOURCES))
HEADERS := common.h
PROGRAM := gcc-wrapper

CC := gcc
CFLAGS := -O2 -Wall -Wextra
RM := rm

all: $(PROGRAM)
clean:
	$(RM) -v $(PROGRAM) $(OBJECTS)
.PHONY: all clean

$(PROGRAM): $(OBJECTS)
	$(CC) $(CFLAGS) -o $@ $^

$(OBJECTS): %.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

$(SOURCES): $(HEADERS)

$(HEADERS):