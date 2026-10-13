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

CLI_SRCS := $(wildcard host/cli/*.c)
CLI_OBJS := $(patsubst %.c,$(OBJDIR)/%.o,$(CLI_SRCS))
ifeq ($(MODE),debug)
  CLI_BIN := $(BINDIR)/debug/ibm650
else
  CLI_BIN := $(BINDIR)/ibm650
endif

TEST_SRCS := $(wildcard tests/test_*.c)
TEST_BINS := $(patsubst tests/%.c,$(BINDIR)/tests/%,$(TEST_SRCS))

.PHONY: all debug test test-run clean

all: $(CLI_BIN)

debug:
	$(MAKE) MODE=debug all

# Tests always run under the sanitizer build. Re-invoke as a sub-make so
# every immediately-expanded variable above is recomputed for MODE=debug.
test:
	$(MAKE) MODE=debug test-run

test-run: $(TEST_BINS) $(CLI_BIN)
	@status=0; \
	for t in $(TEST_BINS); do \
		echo "== $$t =="; \
		./$$t || status=1; \
	done; \
	echo "== examples =="; \
	./$(CLI_BIN) --run examples/grosspay.650 | grep -q '0000000000 0000085355+' \
		&& echo "PASS: grosspay.650 gives 85355" \
		|| { echo "FAIL: grosspay.650"; status=1; }; \
	for f in examples/*.650; do \
		deck=$${f%.650}.cards; \
		./$(CLI_BIN) --run $$( [ -f $$deck ] && echo --hopper $$deck ) $$f > /dev/null \
			&& echo "PASS: $$f halts" || { echo "FAIL: $$f"; status=1; }; \
	done; \
	mkdir -p build; \
	./$(CLI_BIN) --run --hopper examples/payroll.cards --punch build/payroll.out \
		examples/payroll.650 > /dev/null \
		&& grep -q '^205830000{000000039G000008535E' build/payroll.out \
		&& [ $$(wc -l < build/payroll.out) -eq 4 ] \
		&& echo "PASS: payroll punches 4 pay cards" \
		|| { echo "FAIL: payroll.cards"; status=1; }; \
	./$(CLI_BIN) --run --entry 7019518000 --hopper examples/chain_selfload.cards \
		| grep -q '0000000000 0000000120+' \
		&& echo "PASS: chain_selfload.cards boots from the console, gives 120" \
		|| { echo "FAIL: chain_selfload.cards"; status=1; }; \
	exit $$status

$(CLI_BIN): $(CLI_OBJS) $(LIB_OBJS)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -o $@ $^

$(OBJDIR)/host/cli/%.o: CFLAGS += -Ihost/cli

$(OBJDIR)/%.o: %.c $(wildcard core/*.h) $(wildcard host/cli/*.h) tests/test_util.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BINDIR)/tests/%: $(OBJDIR)/tests/%.o $(LIB_OBJS)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -o $@ $^

clean:
	rm -rf build bin
