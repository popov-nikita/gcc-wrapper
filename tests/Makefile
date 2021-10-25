TESTS := test-linemarkers \
         test-dbuf \
         test-run-cmd

CC := gcc
CFLAGS := -O2 -Wall -Wextra
RM := rm

SOURCES := $(patsubst %,%.c,$(TESTS))
test-linemarkers_DEPS := ../util.c ../parse.c
test-dbuf_DEPS := ../util.c
test-run-cmd_DEPS := ../util.c

.PHONY: test $(TESTS)

test: $(TESTS)

$(TESTS):
	@echo '  $(@) >>>>'
	$(CC) $(CFLAGS) -o $(@) $(@).c $($(@)_DEPS)
	@set +e; ./$(@); rc="$$?"; $(RM) -f $(@); echo '  <<<< $(@)'; exit "$$rc"
