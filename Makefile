CC           = gcc
WINDRES      = windres
CFLAGS       = -O2 -mwindows -std=c99
CFLAGS_TEST  = -O0 -std=c99 -Wall
LIBS         = -luser32 -lkernel32
SRC          = switchy.c charmap.c
RES_OBJ      = switchy_res.o
TARGET       = switchy.exe
FORMAT_SRCS  = switchy.c charmap.c charmap.h tests/test_charmap.c

VERSION      ?= 0.0.0
CLANG_FORMAT ?= clang-format
CLANG_TIDY   ?= clang-tidy

.PHONY: all build msi clean test test-charmap fmt fmt-check lint wix release-notes release

all: clean test build msi

build: $(TARGET)

$(RES_OBJ): switchy.rc switchy.ico
	$(WINDRES) switchy.rc -o $(RES_OBJ)

$(TARGET): $(SRC) $(RES_OBJ)
	-taskkill //F //IM $(TARGET) > /dev/null 2>&1 || true
	sleep 0.5
	$(CC) $(SRC) $(RES_OBJ) -o $(TARGET) $(CFLAGS) $(LIBS)

msi: $(TARGET) switchy.wxs
	wix build -acceptEula wix7 -ext WixToolset.Util.wixext switchy.wxs -d Version=$(VERSION) -o switchy.msi

test: test-charmap
	./tests/test_charmap.exe

test-charmap: charmap.c tests/test_charmap.c
	$(CC) charmap.c tests/test_charmap.c -o tests/test_charmap.exe $(CFLAGS_TEST) $(LIBS)

fmt:
	$(CLANG_FORMAT) -i $(FORMAT_SRCS)

fmt-check:
	$(CLANG_FORMAT) --dry-run --Werror $(FORMAT_SRCS)

lint:
	$(CLANG_TIDY) -quiet $(SRC) -- -std=c99

clean:
	rm -f $(TARGET) $(RES_OBJ) tests/test_charmap.exe switchy.msi

wix:
	dotnet tool install --global wix
	wix -acceptEula wix7 extension add WixToolset.Util.wixext

release-notes:
	@awk '\
	/^<!--/,/^-->/ { next } \
	/^## \[[0-9]+\.[0-9]+\.[0-9]+\]/ { if (found) exit; found=1; next } \
	found { \
		if (/^## \[/) { exit } \
		if (/^$$/) { flush(); print; next } \
		if (/^\* / || /^- /) { flush(); buf=$$0; next } \
		if (/^###/ || /^\[/) { flush(); print; next } \
		sub(/^[ \t]+/, ""); sub(/[ \t]+$$/, ""); \
		if (buf != "") { buf = buf " " $$0 } else { buf = $$0 } \
		next \
	} \
	function flush() { if (buf != "") { print buf; buf = "" } } \
	END { flush() } \
	' CHANGELOG.md
