CC ?= cc
STD := -std=c11
WARN := -Wall -Wextra -Wpedantic -Werror

MODE ?= release
ifeq ($(MODE),debug)
  OPT := -g -O0 -fsanitize=address,undefined
else
  OPT := -O2
endif

CFLAGS := $(STD) $(WARN) $(OPT) -Icore
OBJDIR := build/obj/$(MODE)
BINDIR := bin

LIB_SRCS := $(wildcard core/*.c)
LIB_OBJS := $(patsubst %.c,$(OBJDIR)/%.o,$(LIB_SRCS))

TEST_SRCS := $(wildcard tests/test_*.c)
TEST_BINS := $(patsubst tests/%.c,$(BINDIR)/tests/%,$(TEST_SRCS))

.PHONY: all debug test test-run clean

all: $(LIB_OBJS)

debug:
	$(MAKE) MODE=debug all

# Tests always run under the sanitizer build. Re-invoke as a sub-make so
# every immediately-expanded variable above is recomputed for MODE=debug.
test:
	$(MAKE) MODE=debug test-run

test-run: $(TEST_BINS)
	@status=0; \
	for t in $(TEST_BINS); do \
		echo "== $$t =="; \
		./$$t || status=1; \
	done; \
	exit $$status

$(OBJDIR)/%.o: %.c $(wildcard core/*.h) tests/test_util.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BINDIR)/tests/%: $(OBJDIR)/tests/%.o $(LIB_OBJS)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -o $@ $^

clean:
	rm -rf build bin
