CC = gcc
CFLAGS = -O2 -mwindows -std=c99
CFLAGS_TEST = -O0 -std=c99 -Wall
LIBS = -luser32 -lkernel32
SRC = switchy.c charmap.c
TARGET = switchy.exe
VERSION ?= 0.0.0

.PHONY: all build msi clean test test-charmap release-notes release

all: clean test build msi

build: $(TARGET)

$(TARGET): $(SRC)
	-taskkill //F //IM $(TARGET) > /dev/null 2>&1 || true
	sleep 0.5
	$(CC) $(SRC) -o $(TARGET) $(CFLAGS) $(LIBS)

msi: $(TARGET) switchy.wxs
	wix build -acceptEula wix7 switchy.wxs -d Version=$(VERSION) -o switchy.msi

test: test-charmap
	./tests/test_charmap.exe

test-charmap: charmap.c tests/test_charmap.c
	$(CC) charmap.c tests/test_charmap.c -o tests/test_charmap.exe $(CFLAGS_TEST) $(LIBS)

clean:
	rm -f $(TARGET) tests/test_charmap.exe switchy.msi

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
