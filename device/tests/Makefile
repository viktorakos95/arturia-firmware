CC = clang
CFLAGS ?= -std=c11 -O1 -g -Wall -Wextra -Werror
SANITIZERS ?= address,undefined
CPPFLAGS += -I../src
ifneq ($(strip $(SANITIZERS)),)
CFLAGS += -fsanitize=$(SANITIZERS)
endif
BUILD = build
HEADERS = $(wildcard ../src/*.h)
CONTROL_SRC = ../src/generator_core.c ../src/generator_gesture.c ../src/generator_controller.c ../src/generator_panel.c ../src/generator_keyled.c

.PHONY: test clean

test: $(BUILD)/test-core $(BUILD)/test-controls
	./$(BUILD)/test-core
	./$(BUILD)/test-controls

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/test-core: test_core.c ../src/generator_core.c $(HEADERS) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) test_core.c ../src/generator_core.c -o $@

$(BUILD)/test-controls: test_controls.c $(CONTROL_SRC) $(HEADERS) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) test_controls.c $(CONTROL_SRC) -o $@

clean:
	rm -rf $(BUILD)
