# ISLANDERS simulator. `make` builds the library, tests and greedy example; `make test` runs the tests.
CC ?= cc
CFLAGS ?= -std=c11 -O2 -Wall -Wextra -Wno-missing-braces -Wno-missing-field-initializers -Iinclude
SRC = src/sim.c src/rules_data.c
BUILD = build

all: $(BUILD)/libislanders.dylib $(BUILD)/test_sim $(BUILD)/greedy

src/rules_data.c include/islanders_types.h: gen/gen_rules.py ../data/extracted/buildings.json ../data/extracted/variants.json ../data/extracted/packs.json
	python3 gen/gen_rules.py > /dev/null

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/libislanders.dylib: $(SRC) include/islanders.h | $(BUILD)
	$(CC) $(CFLAGS) -shared -fPIC $(SRC) -lm -o $@

$(BUILD)/test_sim: tests/test_sim.c $(SRC) include/islanders.h | $(BUILD)
	$(CC) $(CFLAGS) tests/test_sim.c $(SRC) -lm -o $@

$(BUILD)/greedy: examples/greedy.c $(SRC) include/islanders.h | $(BUILD)
	$(CC) $(CFLAGS) examples/greedy.c $(SRC) -lm -o $@

test: $(BUILD)/test_sim
	./$(BUILD)/test_sim

clean:
	rm -rf $(BUILD)

.PHONY: all test clean
