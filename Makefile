CC      := gcc
BUILD   := build
PKGS    := gtk4 epoxy libqrencode libcurl json-glib-1.0 librsvg-2.0 libavformat libavcodec libswresample libavutil

CFLAGS  := -std=gnu17 -O2 -Wall -Wextra -MMD -MP -I./src -I./src/core -I./src/ui
# 内置的 miniaudio.h 当系统头文件：不报它自己的警告
CFLAGS  += -isystem ./src/models
CFLAGS  += $(shell pkg-config --cflags $(PKGS))
LIBS    := $(shell pkg-config --libs $(PKGS))

# Windows 11：去掉 DWM 默认的 1px 边框 / 圆角（命令行面板和墙都用 CSD）
ifeq ($(OS),Windows_NT)
LIBS    += -ldwmapi
endif

SRCS    := $(wildcard src/*.c src/core/*.c src/ui/*.c)
OBJS    := $(patsubst src/%.c,$(BUILD)/%.o,$(SRCS))
DEPS    := $(OBJS:.o=.d)

# 临时目录：MSYS2 的 make 不把 TMP/TEMP 传给孩子进程，GCC 一旦需要落临时文件
# 就会退到 C:\WINDOWS（没权限）而构建失败。显式指到 build/tmp，不污染系统目录。
TMPDIR := $(CURDIR)/$(BUILD)/tmp
TMP    := $(TMPDIR)
TEMP   := $(TMPDIR)
export TMPDIR TMP TEMP

main.exe: $(OBJS)
	$(CC) -o $@ $^ $(LIBS)

$(BUILD)/%.o: src/%.c
	@mkdir -p $(dir $@) $(TMPDIR)
	$(CC) $(CFLAGS) -c -o $@ $<

# miniaudio 里的原子操作在 -O2 + 内联时会被误报成 stringop-overflow
$(BUILD)/core/media.o: CFLAGS += -Wno-stringop-overflow

.PHONY: clean
clean:
	rm -rf $(BUILD) main.exe

-include $(DEPS)
