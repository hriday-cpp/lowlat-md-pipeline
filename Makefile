CXX      ?= g++
BUILD    ?= build
OPT      ?= -O3 -march=native
CXXFLAGS ?= -std=c++20 $(OPT) -Wall -Wextra -Iinclude -pthread
SAN      :=

APPS  := gen replay bench_book bench_spsc
TESTS := test_parser test_book_diff test_hash test_spsc test_noalloc

all: $(addprefix $(BUILD)/,$(APPS) $(TESTS))

$(BUILD)/alloc_counter.o: src/alloc_counter.cpp include/ll/alloc_counter.hpp | $(BUILD)
	$(CXX) $(CXXFLAGS) $(SAN) -c $< -o $@

$(BUILD)/%: src/%.cpp $(BUILD)/alloc_counter.o $(wildcard include/ll/*.hpp) | $(BUILD)
	$(CXX) $(CXXFLAGS) $(SAN) $< $(BUILD)/alloc_counter.o -o $@

$(BUILD)/test_%: tests/test_%.cpp $(BUILD)/alloc_counter.o $(wildcard include/ll/*.hpp) tests/check.hpp | $(BUILD)
	$(CXX) $(CXXFLAGS) $(SAN) $< $(BUILD)/alloc_counter.o -o $@

$(BUILD):
	mkdir -p $(BUILD)

test: all
	@for t in $(TESTS); do ./$(BUILD)/$$t || exit 1; done
	@echo "ALL TESTS PASSED"

asan:
	$(MAKE) BUILD=build-asan SAN="-fsanitize=address,undefined -fno-omit-frame-pointer" OPT="-O1 -g" test
tsan:
	$(MAKE) BUILD=build-tsan SAN="-fsanitize=thread" OPT="-O1 -g" build-tsan/test_spsc
	./build-tsan/test_spsc

bench: all
	./scripts/run_all.sh

clean:
	rm -rf build build-asan build-tsan

.PHONY: all test asan tsan bench clean
