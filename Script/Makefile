# ============================================================
#  Codeforces Rating Crawler - Makefile (MinGW-w64)
# ============================================================

CC       = gcc
CFLAGS   = -Wall -Wextra -O2
TARGET   = cf_crawler.exe
SRCDIR   = .
OBJDIR   = obj

# 源文件
SRCS     = main.c http_client.c cf_api.c analyzer.c data_model.c utils.c cJSON.c
OBJS     = $(patsubst %.c, $(OBJDIR)/%.o, $(SRCS))

# 头文件搜索路径
INCLUDES = -I. -I"$(MINGW_HOME)/include"
LDFLAGS  = -L"$(MINGW_HOME)/lib"
LDLIBS   = -lcurl -lws2_32

# 若 MINGW_HOME 未设置，默认使用 VSCplug-in 下的 MinGW
MINGW_HOME ?= D:/VSCode/VSCplug-in/mingw64

# ========================= 默认目标 =========================

.PHONY: all clean run dist test

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) $(LDLIBS)
	@echo ">>> Build complete: $(TARGET)"
	@if exist "$(MINGW_HOME)/bin/libcurl-x64.dll" copy /Y "$(MINGW_HOME)/bin/libcurl-x64.dll" . >nul 2>&1

$(OBJDIR)/%.o: $(SRCDIR)/%.c | $(OBJDIR)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

$(OBJDIR):
	@if not exist "$(OBJDIR)" mkdir "$(OBJDIR)"

# ========================= 运行测试 =========================

test:
	$(TARGET) tourist

# ========================= 清理 =========================

clean:
	@if exist "$(OBJDIR)" rmdir /S /Q "$(OBJDIR)"
	@if exist "$(TARGET)" del /Q "$(TARGET)"
	@if exist data.js  del /Q data.js
	@if exist index.html del /Q index.html

# ========================= 分发打包 =========================

dist: all
	@echo ">>> Creating distribution package..."
	@if not exist dist mkdir dist
	copy /Y $(TARGET) dist\
	@if exist "libcurl-x64.dll" copy /Y "libcurl-x64.dll" dist\
	copy /Y template.html dist\
	copy /Y ..\..\README.md dist\ 2>nul
	@echo ">>> Package ready in dist/"
