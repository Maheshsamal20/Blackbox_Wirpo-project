# BlackBox top-level Makefile
#   make            build user-space tools (blackboxd, bbctl, crash_demo)
#   make driver     build the kernel module (needs kernel headers)
#   make test       unit tests + mock-device integration test
#   make kernel-test  (root, in a VM) smoke + stress tests against the loaded module
#   make asan       unit tests under AddressSanitizer + UBSan
#   make tsan       unit tests under ThreadSanitizer
CXX      ?= g++
CXXFLAGS ?= -std=c++17 -O2 -g -Wall -Wextra -pthread
CPPFLAGS += -Iinclude -Isrc/common
BUILD    := build

COMMON_SRC := $(wildcard src/common/*.cpp)
COMMON_OBJ := $(patsubst src/%.cpp,$(BUILD)/%.o,$(COMMON_SRC))
DAEMON_OBJ := $(BUILD)/daemon/Daemon.o

BINS := $(BUILD)/blackboxd $(BUILD)/bbctl $(BUILD)/crash_demo $(BUILD)/stress

.PHONY: all driver test unit integration kernel-test asan tsan clean
all: $(BINS)

$(BUILD)/%.o: src/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@

$(BUILD)/blackboxd: src/daemon/main.cpp $(DAEMON_OBJ) $(COMMON_OBJ)
	@mkdir -p $(BUILD)
	$(CXX) $(CPPFLAGS) -Isrc/daemon $(CXXFLAGS) $^ -o $@

$(BUILD)/daemon/Daemon.o: src/daemon/Daemon.cpp src/daemon/Daemon.hpp
	@mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) -Isrc/daemon $(CXXFLAGS) -c $< -o $@

$(BUILD)/bbctl: src/cli/bbctl.cpp $(COMMON_OBJ)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $^ -o $@

$(BUILD)/crash_demo: src/demo/crash_demo.cpp $(COMMON_OBJ)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $^ -o $@

$(BUILD)/stress: tests/stress.cpp $(COMMON_OBJ)
	@mkdir -p $(BUILD)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $^ -o $@

$(BUILD)/unit_tests: tests/test_main.cpp $(COMMON_SRC)
	@mkdir -p $(BUILD)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $^ -o $@

unit: $(BUILD)/unit_tests
	./$(BUILD)/unit_tests

integration: all
	bash tests/integration_mock.sh

test: unit integration

kernel-test: all driver
	sudo bash tests/kernel_smoke.sh
	sudo bash tests/kernel_stress.sh

asan: tests/test_main.cpp $(COMMON_SRC)
	@mkdir -p $(BUILD)
	$(CXX) $(CPPFLAGS) -std=c++17 -g -O1 -Wall -Wextra -pthread -fsanitize=address,undefined -fno-omit-frame-pointer $^ -o $(BUILD)/unit_tests_asan
	./$(BUILD)/unit_tests_asan

tsan: tests/test_main.cpp $(COMMON_SRC)
	@mkdir -p $(BUILD)
	$(CXX) $(CPPFLAGS) -std=c++17 -g -O1 -Wall -Wextra -pthread -fsanitize=thread $^ -o $(BUILD)/unit_tests_tsan
	./$(BUILD)/unit_tests_tsan

driver:
	$(MAKE) -C driver

clean:
	rm -rf $(BUILD)
	-$(MAKE) -C driver clean
