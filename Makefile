CC = clang
CFLAGS = -O2 -Wall -Wextra -Iinclude -MMD -MP
LDFLAGS = -lm

SRC_DIR = src
BUILD_DIR = build
TARGET = myrun

SOURCES = $(SRC_DIR)/ast.c $(SRC_DIR)/ast_parser.c \
          $(SRC_DIR)/value.c $(SRC_DIR)/lexer.c $(SRC_DIR)/interpreter.c \
          $(SRC_DIR)/parser.c $(SRC_DIR)/builtin.c $(SRC_DIR)/main.c
OBJECTS = $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(SOURCES))
DEPS = $(OBJECTS:.o=.d)

.PHONY: all debug test clean

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CC) $(OBJECTS) -o $@ $(LDFLAGS)

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

debug:
	$(MAKE) BUILD_DIR=build-debug TARGET=myrun_debug \
		CFLAGS="-g -O0 -Wall -Wextra -Iinclude -MMD -MP -fsanitize=address,undefined" \
		LDFLAGS="-lm -fsanitize=address,undefined"

test: all
	./$(TARGET) --parse tes_split.nv

clean:
	rm -rf $(BUILD_DIR) build-debug $(TARGET) myrun_debug

-include $(DEPS)