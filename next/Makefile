UNAME_S := $(shell uname -s)
UNAME_P := $(shell uname -m)

ifeq ($(UNAME_P),aarch64)
    ARCH = arm64
    CC = gcc
else ifeq ($(findstring arm,$(UNAME_P)),arm)
    ARCH = arm
    CC = gcc
else
    ARCH = x86_64
    CC = gcc
endif

CFLAGS = -Wall -Wextra -std=c11 -Iinclude -g
LDFLAGS = -lm

SRC_DIR = src
INC_DIR = include
OBJ_DIR = obj

SRCS = $(wildcard $(SRC_DIR)/*.c)
OBJS = $(SRCS:$(SRC_DIR)/%.c=$(OBJ_DIR)/%.o)

TARGET = wee-next

.PHONY: all clean arch

all: arch $(TARGET)

arch:
	@echo "Building for architecture: $(ARCH)"

$(TARGET): $(OBJS)
	$(CC) $(OBJS) -o $@ $(LDFLAGS)

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c | $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)

clean:
	rm -rf $(OBJ_DIR) $(TARGET)