CC = cc
CFLAGS = -std=c11 -Wall -Wextra -Wpedantic -Werror -O2 -Iinclude
BUILD_DIR = build
DICTIONARY_SRC = src/shared/dictionary.c

.PHONY: all test test-monitor demo-monitor clean
MONITOR_SRC = src/shared/protocol.c src/monitor/statistics.c
MONITOR_BIN = $(BUILD_DIR)/monitor
SIMULATOR_BIN = $(BUILD_DIR)/monitor_simulator
STATISTICS_TEST_BIN = $(BUILD_DIR)/test_statistics
DECOMPRESSOR_SRC = $(DICTIONARY_SRC) src/shared/protocol.c src/decompressor/decompressor.c
DECOMPRESSOR_BIN = $(BUILD_DIR)/decompressor
all: $(BUILD_DIR)/test_dictionary $(BUILD_DIR)/test_corpus $(MONITOR_BIN) $(SIMULATOR_BIN) $(STATISTICS_TEST_BIN) $(DECOMPRESSOR_BIN)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/test_dictionary: $(DICTIONARY_SRC) tests/test_dictionary.c include/dictionary.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(DICTIONARY_SRC) tests/test_dictionary.c -o $@

$(BUILD_DIR)/test_corpus: $(DICTIONARY_SRC) tests/test_corpus.c include/dictionary.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(DICTIONARY_SRC) tests/test_corpus.c -o $@

$(MONITOR_BIN): $(MONITOR_SRC) src/monitor/monitor.c src/monitor/main.c include/protocol.h include/statistics.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(MONITOR_SRC) src/monitor/monitor.c src/monitor/main.c -o $@

$(SIMULATOR_BIN): src/shared/protocol.c tests/monitor_simulator.c include/protocol.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) src/shared/protocol.c tests/monitor_simulator.c -o $@

$(STATISTICS_TEST_BIN): src/monitor/statistics.c tests/test_statistics.c include/statistics.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) src/monitor/statistics.c tests/test_statistics.c -o $@

$(DECOMPRESSOR_BIN): $(DECOMPRESSOR_SRC) src/decompressor/main.c include/decompressor.h include/dictionary.h include/protocol.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(DECOMPRESSOR_SRC) src/decompressor/main.c -o $@ -lrt

test-monitor: all
	./$(STATISTICS_TEST_BIN)
	./$(SIMULATOR_BIN) compress
	./$(SIMULATOR_BIN) decompress

demo-monitor: all
	./$(SIMULATOR_BIN) compress

test: all
	./$(STATISTICS_TEST_BIN)
	./$(SIMULATOR_BIN) compress
	./$(SIMULATOR_BIN) decompress
	./$(BUILD_DIR)/test_dictionary
	./$(BUILD_DIR)/test_corpus data/dictionary.txt tests/fixtures/input_one.txt tests/fixtures/input_two.txt

clean:
	rm -rf $(BUILD_DIR)
