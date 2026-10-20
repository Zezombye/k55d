# Cross-compile from Linux with: make CROSS=x86_64-w64-mingw32-
CROSS ?=
CC = $(CROSS)gcc
WINDRES = $(CROSS)windres
VERSION := $(strip $(file < version.txt))
EXE = k55d-$(VERSION).exe
CFLAGS = -std=c11 -O2 -Wall -Wextra -DK55D_VERSION=$(VERSION)
LDLIBS = -lhid -lsetupapi

$(EXE): src/*.c src/*.h k55d.rc k55d.ico version.txt
	$(WINDRES) k55d.rc -O coff -o k55d.res
	$(CC) $(CFLAGS) -mwindows -o $@ src/*.c k55d.res $(LDLIBS)

clean:
	rm -f $(TOOLS)
	rm -f k55d-*.exe k55d.res

.PHONY: clean

# Reverse engineering tools (see README.md, section "Tools").
TOOLS = tools/probe/hidlist.exe tools/probe/bragi.exe tools/probe/bsh.exe tools/probe/listen.exe \
	tools/sniff/hook.dll tools/sniff/inject.exe tools/sniff/injectpid.exe

tools: $(TOOLS)

tools/probe/%.exe: tools/probe/%.c
	$(CC) -O2 -o $@ $< -lhid -lsetupapi

tools/sniff/hook.dll: tools/sniff/hook.c
	$(CC) -O2 -shared -o $@ $< -lpsapi

tools/sniff/%.exe: tools/sniff/%.c
	$(CC) -O2 -o $@ $<

.PHONY: tools
