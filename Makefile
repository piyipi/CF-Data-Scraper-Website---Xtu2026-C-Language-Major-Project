# ============================================================
#  Codeforces Rating Crawler - Makefile (MinGW-w64)
# ============================================================

CC       = gcc
CFLAGS   = -Wall -Wextra -O2
TARGET   = cf_crawler.exe
SRCDIR   = src
OBJDIR   = obj

# 源文件（src/ 下的 C 源码）
SRCS     = main.c http_client.c cf_api.c analyzer.c data_model.c utils.c

# 目标文件
OBJS     = $(addprefix $(OBJDIR)/, $(SRCS:.c=.o)) $(OBJDIR)/cJSON.o

# 头文件搜索路径
INCLUDES = -Isrc -Ilib/cJSON -I"$(MINGW_HOME)/include"
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
	-@if exist "$(MINGW_HOME)/bin/libcurl-x64.dll" copy /Y "$(MINGW_HOME)\bin\libcurl-x64.dll" lib\curl\ >nul 2>&1

# src/ 下的 .o 文件
$(OBJDIR)/%.o: $(SRCDIR)/%.c | $(OBJDIR)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

# lib/cJSON/ 下的 cJSON.o
$(OBJDIR)/cJSON.o: lib/cJSON/cJSON.c | $(OBJDIR)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

$(OBJDIR):
	@if not exist "$(OBJDIR)" mkdir "$(OBJDIR)"

# ========================= 运行测试 =========================

test: all
	@if not exist output mkdir output
	$(TARGET) tourist

# ========================= 清理 =========================

clean:
	@if exist "$(OBJDIR)" rmdir /S /Q "$(OBJDIR)"
	@if exist "$(TARGET)" del /Q "$(TARGET)"
	@if exist output\data.js     del /Q output\data.js     2>nul
	@if exist output\index.html   del /Q output\index.html   2>nul
	@if exist output\*_data.js    del /Q output\*_data.js    2>nul
	@if exist output\*.html       del /Q output\*.html       2>nul

# ========================= 分发打包 =========================

dist: all
	@echo ">>> Creating distribution package..."
	@if not exist dist mkdir dist
	copy /Y $(TARGET) dist\
	@if exist "lib\curl\libcurl-x64.dll" copy /Y "lib\curl\libcurl-x64.dll" dist\
	copy /Y web\template.html dist\
	@if not exist dist\output mkdir dist\output
	copy /Y ..\..\README.md dist\ 2>nul
	@echo ">>> Package ready in dist/"
