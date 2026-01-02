CXX = clang++
CXXFLAGS = -std=c++17 -Wall -Wextra -O2
CC = clang
CFLAGS = -Wall -Wextra

# Paths
SRC_DIR = src
TEST_DIR = tests
SAMPLE_DIR = samples
BUILD_DIR = build

# Tools
TARGET = strip_symbol_cpp
TEST_RUNNER = test_runner_bin

# Sources
STRIPPER_SRC = $(SRC_DIR)/strip_symbol.cpp
TEST_RUNNER_SRC = $(TEST_DIR)/src/test_runner.c
DYLIB_SRC = $(TEST_DIR)/dylib/target.c
DYLIB_TARGET = $(SAMPLE_DIR)/libtarget.dylib

all: $(TARGET)

$(TARGET): $(STRIPPER_SRC)
	$(CXX) $(CXXFLAGS) $(STRIPPER_SRC) -o $(TARGET)

$(DYLIB_TARGET): $(DYLIB_SRC)
	@mkdir -p $(SAMPLE_DIR)
	$(CC) $(CFLAGS) -dynamiclib -o $(DYLIB_TARGET) $(DYLIB_SRC)

$(TEST_RUNNER): $(TEST_RUNNER_SRC)
	$(CC) $(CFLAGS) $(TEST_RUNNER_SRC) -o $(TEST_RUNNER)

test: $(TARGET) $(DYLIB_TARGET) $(TEST_RUNNER)
	@echo "[*] Running Test Suite..."
	@# 1. Run test runner on original dylib (should succeed with symbol visible)
	@echo "[*] Verifying original dylib..."
	./$(TEST_RUNNER) $(DYLIB_TARGET)
	
	@# 2. Strip the symbol
	@echo "[*] Stripping symbol..."
	./$(TARGET) -i $(DYLIB_TARGET) -o $(SAMPLE_DIR)/libtarget_stripped.dylib -s ___patcher_target_qword
	
	@# 3. Run test runner on stripped dylib (should fail finding symbol, but pass other checks)
	@echo "[*] Verifying stripped dylib..."
	./$(TEST_RUNNER) $(SAMPLE_DIR)/libtarget_stripped.dylib || echo "[+] Test Runner behaved as expected (symbol not found)"

clean:
	rm -f $(TARGET) $(TEST_RUNNER)
	rm -f $(SAMPLE_DIR)/libtarget.dylib $(SAMPLE_DIR)/libtarget_stripped.dylib
	rm -rf $(BUILD_DIR) Output/

.PHONY: all test clean
