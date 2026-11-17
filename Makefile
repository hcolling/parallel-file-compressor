CC = cc
CFLAGS = -std=c11 -Wall -Wextra -Wpedantic -Werror -O2 -Iinclude
BUILD_DIR = build
DICTIONARY_SRC = src/shared/dictionary.c

.PHONY: all test test-monitor demo-monitor demo demo-one demo-two clean
MONITOR_SRC = src/shared/protocol.c src/monitor/statistics.c
MONITOR_BIN = $(BUILD_DIR)/monitor
SIMULATOR_BIN = $(BUILD_DIR)/monitor_simulator
STATISTICS_TEST_BIN = $(BUILD_DIR)/test_statistics
DECOMPRESSOR_SRC = $(DICTIONARY_SRC) src/shared/protocol.c src/decompressor/decompressor.c
DECOMPRESSOR_BIN = $(BUILD_DIR)/decompressor
COMPRESSOR_SRC = $(DICTIONARY_SRC) src/shared/protocol.c src/compressor/compressor.c
COMPRESSOR_BIN = $(BUILD_DIR)/compressor
all: $(BUILD_DIR)/test_dictionary $(BUILD_DIR)/test_corpus $(MONITOR_BIN) $(SIMULATOR_BIN) $(STATISTICS_TEST_BIN) $(DECOMPRESSOR_BIN) $(COMPRESSOR_BIN)

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

$(COMPRESSOR_BIN): $(COMPRESSOR_SRC) src/compressor/main.c include/compressor.h include/dictionary.h include/protocol.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(COMPRESSOR_SRC) src/compressor/main.c -o $@ -lrt -lpthread

test-monitor: all
	./$(STATISTICS_TEST_BIN)
	./$(SIMULATOR_BIN) compress
	./$(SIMULATOR_BIN) decompress

demo-monitor: all
	./$(SIMULATOR_BIN) compress

demo: demo-one demo-two

demo-one: all
	./$(COMPRESSOR_BIN) data/dictionary.txt tests/fixtures/input_one.txt $(BUILD_DIR)/input_one.mzp
	./$(DECOMPRESSOR_BIN) data/dictionary.txt $(BUILD_DIR)/input_one.mzp $(BUILD_DIR)/input_one.out
	cmp tests/fixtures/input_one.txt $(BUILD_DIR)/input_one.out

demo-two: all
	./$(COMPRESSOR_BIN) data/dictionary.txt tests/fixtures/input_two.txt $(BUILD_DIR)/input_two.mzp
	./$(DECOMPRESSOR_BIN) data/dictionary.txt $(BUILD_DIR)/input_two.mzp $(BUILD_DIR)/input_two.out
	cmp tests/fixtures/input_two.txt $(BUILD_DIR)/input_two.out

test: all
	./$(STATISTICS_TEST_BIN)
	./$(SIMULATOR_BIN) compress
	./$(SIMULATOR_BIN) decompress
	./$(BUILD_DIR)/test_dictionary
	./$(BUILD_DIR)/test_corpus data/dictionary.txt tests/fixtures/input_one.txt tests/fixtures/input_two.txt
	./$(COMPRESSOR_BIN) data/dictionary.txt tests/fixtures/input_one.txt $(BUILD_DIR)/input_one.mzp
	./$(DECOMPRESSOR_BIN) data/dictionary.txt $(BUILD_DIR)/input_one.mzp $(BUILD_DIR)/input_one.out
	cmp tests/fixtures/input_one.txt $(BUILD_DIR)/input_one.out
	./$(COMPRESSOR_BIN) data/dictionary.txt tests/fixtures/input_two.txt $(BUILD_DIR)/input_two.mzp
	./$(DECOMPRESSOR_BIN) data/dictionary.txt $(BUILD_DIR)/input_two.mzp $(BUILD_DIR)/input_two.out
	cmp tests/fixtures/input_two.txt $(BUILD_DIR)/input_two.out

clean:
	rm -rf $(BUILD_DIR)
