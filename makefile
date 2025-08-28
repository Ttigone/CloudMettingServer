CC := g++
BUILD_DIR := build
SRC := $(wildcard *.cpp)
OBJ := $(addprefix $(BUILD_DIR)/, $(SRC:%.cpp=%.o))
HEADER := $(wildcard *.h)
TARGET := $(BUILD_DIR)/app

# 默认目标改为 run
all: run

# 编译目标
build: $(TARGET)

# 创建 build 目录并编译可执行文件
$(TARGET): $(OBJ) | $(BUILD_DIR)
	$(CC) -pthread $(OBJ) -o $@ $(addprefix -I, $(HEADER))

# 编译目标文件到 build 目录
$(BUILD_DIR)/%.o: %.cpp | $(BUILD_DIR)
	$(CC) $< -c -o $@

# 创建 build 目录
$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

.PHONY: clean all build run

clean:
	rm -rf $(BUILD_DIR)

# 运行程序（依赖于编译）
run: $(TARGET)
	cd $(BUILD_DIR) && ./app 0.0.0.0 8080 30 10